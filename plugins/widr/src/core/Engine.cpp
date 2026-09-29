#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace widr {

namespace {

// Per Character: the blend of the four generators (normalised, then scaled by `level`), the Haas
// delay at Size 50 %, the micro pitch spread, the early reflections (taps, spacing), the reverb send
// and how much contrast is baked in. They are meant to sound clearly different.
struct CharacterMix
{
    float haas, decor, pitch, er;
    double haasMs, cents;
    int taps;
    double erSpacing;
    float reverb, level, contrast;
};
constexpr CharacterMix kCharacters[kNumCharacters] = {
    // Tight: decorrelation only, no audible delay or room; a clean, close widening
    {0.10f, 1.00f, 0.00f, 0.00f, 0.4, 0.0, 8, 0.6, 0.3f, 0.80f, 0.6f},
    // Wide: a Haas pair up front, some decorrelation: the classic wide
    {1.00f, 0.35f, 0.15f, 0.20f, 11.0, 5.0, 16, 1.0, 0.7f, 1.00f, 1.0f},
    // Epic: big reflections, a detuned spread, long Haas, a louder side and the strongest contrast
    {0.55f, 0.30f, 0.65f, 0.95f, 17.0, 12.0, 16, 1.3, 1.4f, 1.35f, 1.7f},
    // Surround: the reverb and a large room lead, the source seems to sit inside it
    {0.25f, 0.50f, 0.25f, 0.70f, 24.0, 7.0, 16, 1.7, 2.6f, 1.20f, 1.2f},
};
// Side level of the generators at Width 100 % relative to the mid: about -3 dB.
constexpr float kSideScale = 0.7f;

// A fixed stereo pattern of early reflections (ms at Size 50 %, sign = which side).
constexpr int kErTaps = 16;
constexpr double kErMs[kErTaps] = {3.1, 5.3, 7.9, 11.2, 13.7, 17.3, 19.9, 23.4, 29.1, 31.7, 37.2, 41.9, 47.3, 53.6, 61.2, 67.9};
constexpr float kErSign[kErTaps] = {1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1, -1, -1, 1, -1, 1};

// The decorrelator's all-pass delays (samples at 48 kHz): mutually prime, 2 to 7 ms.
constexpr int kDecorDelays[4] = {113, 173, 241, 331};

// Butterworth-4 section Qs: an LR8 crossover is two of each.
constexpr double kQ1 = 0.54119610, kQ2 = 1.30656296;

int idx (double v) { return (int)std::lround (v); }
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

Engine::Engine ()
{
    gCur.fill (1.0f);
    gYieldCur.fill (1.0f);
}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    haasLine.prepare ((int)(0.03 * sr) + 8);
    erLine.prepare ((int)(0.16 * sr) + 8);
    for (int i = 0; i < 4; ++i)
        decor[(size_t)i].prepare ((int)std::lround (kDecorDelays[i] * sr / 48000.0), 0.5f);
    shiftDown.prepare (sr);
    shiftUp.prepare (sr);
    reverb.prepare (sr);
    haasHpC = BiquadCoeffs::highPass (350.0, M_SQRT1_2, sr);
    haasLpC = BiquadCoeffs::lowPass (12000.0, M_SQRT1_2, sr);
    ringM.assign (kFft, 0.0f);
    ringS.assign (kFft, 0.0f);
    ringG.assign (kFft, 0.0f);
    frame.assign (kFft, 0.0f);
    spec.assign (kFft / 2 + 1, {});
    window.resize (kFft);
    for (int i = 0; i < kFft; ++i)
        window[(size_t)i] = 0.5f - 0.5f * (float)std::cos (2.0 * M_PI * i / kFft);
    envFastA = (float)(1.0 - std::exp (-1.0 / (0.005 * sr)));
    envSlowA = (float)(1.0 - std::exp (-1.0 / (0.3 * sr)));
    duckAtt = (float)(1.0 - std::exp (-1.0 / (0.001 * sr)));
    duckRel = (float)(1.0 - std::exp (-1.0 / (0.12 * sr)));
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    slow = (float)(1.0 - std::exp (-1.0 / (0.08 * sr)));
    tail.prepare (sr, maxBlock);
    for (uint32_t f = 0; f < pk::kTailFields; ++f)
        tail.setParam (f, p[kTailBase + f]);
    if (meters)
        meters->sampleRate.store ((float)sr);
    xHz = airDb = beyondAmt = -1.0;
    reset ();
}

void Engine::reset ()
{
    haasHp.reset ();
    haasLp.reset ();
    haasLine.reset ();
    erLine.reset ();
    for (auto& a : decor)
        a.reset ();
    shiftDown.reset ();
    shiftUp.reset ();
    erDamp.reset ();
    reverb.reset ();
    for (auto& b : bank)
        b.reset ();
    airZ.reset ();
    beyondZ.reset ();
    for (auto* v : {&sideHp, &midLp, &midHp})
        for (auto& b : *v)
            b.reset ();
    std::fill (ringM.begin (), ringM.end (), 0.0f);
    std::fill (ringS.begin (), ringS.end (), 0.0f);
    std::fill (ringG.begin (), ringG.end (), 0.0f);
    ringPos = hopCount = 0;
    eM.fill (0.0f);
    eS.fill (0.0f);
    eG.fill (0.0f);
    pubSide.fill (0.0f);
    sLR = sLL = sRR = 0.0;
    corr = 1.0f;
    envFast = envSlow = 0.0;
    duck = duckT = 1.0f;
    duckCount = 0;
    tail.reset ();
    // settle the smoothed controls on the current settings
    blockSetup ();
    gen = genT;
    haasD = haasDT;
    erScale = erScaleT;
    width = (float)(p[kWidth] * mix.roleScale);
    space = (float)p[kSpace];
    wet = p[kWidth] > 1e-6 ? 1.0f : 0.0f;
    outGain = dbToGain (p[kOutput]);
    mirror = mix.mirror;
    bypassed = false;
}

void Engine::blockSetup ()
{
    const auto& c = kCharacters[std::clamp (idx (p[kCharacter]), 0, kNumCharacters - 1)];
    const double size = std::clamp (p[kSize], 0.0, 1.0);
    // the blend, normalised to the same power whatever the Character (the pitch spread is quieter
    // than the others as a side signal, so it counts for less)
    const float norm = std::sqrt (c.haas * c.haas + c.decor * c.decor + 0.5f * c.pitch * c.pitch + c.er * c.er);
    const float gain = kSideScale * c.level / norm;
    genT = {c.haas * gain, c.decor * gain, c.pitch * gain, c.er * gain};
    haasDT = std::clamp (c.haasMs * (0.5 + size), 0.1, 25.0) * 0.001 * sr;
    // ms -> samples: the pattern at 0.4x to 2x by Size, times the Character's spacing (at most 150 ms)
    erScaleT = std::min ((0.4 + 1.6 * size) * c.erSpacing, 150.0 / 68.0) * 0.001 * sr;
    level = c.level;
    contrast = (float)(std::clamp (p[kContrast], 0.0, 1.0) * c.contrast);
    erTaps = c.taps;
    // the reflections fade with time; their total power is 1 whatever the tap count
    float erPower = 0.0f;
    for (int t = 0; t < kErTaps; ++t)
    {
        erGain[(size_t)t] = t < erTaps ? (float)std::exp (-kErMs[t] / 35.0) : 0.0f;
        erPower += erGain[(size_t)t] * erGain[(size_t)t];
    }
    for (auto& g : erGain)
        g /= std::sqrt (erPower);
    reverbSend = c.reverb;
    reverb.set (p[kDecay], p[kPreDelay], p[kDamping]);
    erDampA = OnePole::coeff (p[kDamping], sr);
    // micro pitch: a few cents down (left) and up (right), each drifting on its own slow LFO
    shiftDown.setCents (-c.cents * (1.0 + 0.3 * std::sin (2.0 * M_PI * 0.11 * lfoPhase)));
    shiftUp.setCents (c.cents * (1.0 + 0.3 * std::sin (2.0 * M_PI * 0.17 * lfoPhase + 1.0)));
    if (std::fabs (p[kAir] - airDb) > 1e-3)
    {
        airDb = p[kAir];
        airC = BiquadCoeffs::highShelf (6000.0, airDb, sr);
    }
    if (std::fabs (p[kBeyond] - beyondAmt) > 1e-4)
    {
        beyondAmt = p[kBeyond];
        beyondC = BiquadCoeffs::peaking (4000.0, 0.9, 9.0 * beyondAmt, sr);
    }
    if (std::fabs (p[kMonoBelow] - xHz) > 1e-3)
    {
        xHz = p[kMonoBelow];
        xLp = {BiquadCoeffs::lowPass (xHz, kQ1, sr), BiquadCoeffs::lowPass (xHz, kQ2, sr)};
        xHp = {BiquadCoeffs::highPass (xHz, kQ1, sr), BiquadCoeffs::highPass (xHz, kQ2, sr)};
    }
}

void Engine::analyse ()
{
    hopCount = 0;
    const double binHz = sr / kFft;
    const float a = (float)(1.0 - std::exp (-(double)kHop / (0.15 * sr)));
    auto bands = [&] (const std::vector<float>& ring, std::array<float, kBands>& e) {
        for (int i = 0; i < kFft; ++i)
            frame[(size_t)i] = ring[(size_t)((ringPos + i) & (kFft - 1))] * window[(size_t)i];
        fft.forward (frame.data (), spec.data ());
        const float scale = 4.0f / ((float)kFft * (float)kFft); // relative levels are what count
        for (int k = 0; k < kBands; ++k)
        {
            float sum = 0.0f;
            if (bandHz (k) < 0.45 * sr)
            {
                const int b0 = std::max (1, (int)std::ceil (bandLowHz (k) / binHz));
                const int b1 = std::max (b0, std::min (kFft / 2, (int)std::floor (bandHighHz (k) / binHz)));
                for (int b = b0; b <= b1; ++b)
                    sum += std::norm (spec[(size_t)b]);
            }
            e[(size_t)k] += (sum * scale - e[(size_t)k]) * a;
        }
    };
    bands (ringM, eM);
    bands (ringS, eS);
    bands (ringG, eG);

    // Mono Guard: a floor under each band's correlation, (M - S) / (M + S) >= c0, which caps the side
    // energy at ratio * mid; the generated side gives way first (the input's own side is kept).
    const double guard = std::clamp (p[kGuard], 0.0, 1.0);
    const double c0 = -1.0 + 1.2 * guard;
    const double ratio = (1.0 - c0) / std::max (1e-6, 1.0 + c0);
    const float ag = (float)(1.0 - std::exp (-(double)kHop / (0.2 * sr))); // 200 ms: nothing pumps
    // spectral contrast: the side gives way where the mid stands out from its neighbouring bands and
    // fills where it is thin (-9 .. +6 dB at full contrast)
    std::array<float, kBands> gContrast;
    gContrast.fill (1.0f);
    if (contrast > 0.0f)
        for (int k = 0; k < kBands; ++k)
        {
            double sum = 0.0;
            int n = 0;
            for (int j = std::max (0, k - 3); j <= std::min (kBands - 1, k + 3); ++j)
                if (bandHz (j) < 0.45 * sr)
                {
                    sum += eM[(size_t)j];
                    ++n;
                }
            const double hood = sum / std::max (1, n);
            if (hood > 1e-14 && eM[(size_t)k] > 1e-16)
                gContrast[(size_t)k] = (float)std::clamp (std::pow (eM[(size_t)k] / hood, -0.35 * contrast), 0.35, 2.0);
        }
    bool flat = true;
    for (int k = 0; k < kBands; ++k)
    {
        const size_t i = (size_t)k;
        // Mono Guard: the most this band's generated side may be scaled by (up to 2x, for the contrast)
        float gGuard = 2.0f;
        if (guard > 0.0 && eG[i] > 1e-12f)
        {
            const double g2 = (ratio * eM[i] - eS[i]) / eG[i];
            gGuard = (float)std::sqrt (std::clamp (g2, 0.0, 4.0));
        }
        const float target = std::min (gGuard, gContrast[i]) * std::clamp (mix.yield[i], 0.0f, 1.0f);
        gCur[i] += (target - gCur[i]) * ag;
        gYieldCur[i] += (mix.yield[i] - gYieldCur[i]) * ag;
        pubSide[i] = eG[i] * gCur[i] * gCur[i];
        float db = std::min (6.0f, 20.0f * std::log10 (std::max (gCur[i], 0.0316f))); // -30 .. +6 dB
        if (bandHz (k) >= 0.45 * sr)
            db = 0.0f;
        if (std::fabs (db - bankDb[i]) > 0.05f)
        {
            bankDb[i] = db;
            bankC[i] = BiquadCoeffs::peaking (bandHz (k), 4.3, db, sr);
        }
        flat &= std::fabs (bankDb[i]) < 0.05f;
    }
    if (flat && !bankFlat)
        for (auto& b : bank)
            b.reset ();
    bankFlat = flat;
    if (meters)
        for (int k = 0; k < kBands; ++k)
        {
            meters->bandGain[(size_t)k].store (gCur[(size_t)k], std::memory_order_relaxed);
            meters->bandYield[(size_t)k].store (gYieldCur[(size_t)k], std::memory_order_relaxed);
            meters->bandSide[(size_t)k].store (10.0f * std::log10 (std::max (1e-12f, pubSide[(size_t)k])),
                                               std::memory_order_relaxed);
        }
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    NoDenormals noDenormals;
    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int m = std::min (maxBlock, n - pos);
        processBlock (inL + pos, inR + pos, outL + pos, outR + pos, m);
    }
}

void Engine::processBlock (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    blockSetup ();
    lfoPhase = std::fmod (lfoPhase + n / sr, 1000.0);
    const float wetT = p[kWidth] > 1e-6 ? 1.0f : 0.0f;
    const float widthT = (float)(p[kWidth] * mix.roleScale), spaceT = (float)p[kSpace];
    const float outT = dbToGain (p[kOutput]);
    const bool monoCheck = p[kMonoCheck] >= 0.5;
    const float ac = (float)(1.0 - std::exp (-1.0 / (0.3 * sr)));

    auto finish = [&] (int i, float l, float r) {
        if (monoCheck)
            l = r = 0.5f * (l + r);
        outGain += (outT - outGain) * smooth;
        l *= outGain;
        r *= outGain;
        sLR += ((double)l * r - sLR) * ac;
        sLL += ((double)l * l - sLL) * ac;
        sRR += ((double)r * r - sRR) * ac;
        if (meters)
            meters->scope.push (l, r);
        outL[i] = l;
        outR[i] = r;
    };

    if (wetT == 0.0f && wet < 1e-4f)
    {
        // Width 0: bypass (the mid and side still feed the analysis, so the group sees this
        // instance's mid; it generates nothing)
        wet = 0.0f;
        bypassed = true;
        for (int i = 0; i < n; ++i)
        {
            const float l = inL[i], r = inR[i];
            ringM[(size_t)ringPos] = 0.5f * (l + r);
            ringS[(size_t)ringPos] = 0.5f * (l - r);
            ringG[(size_t)ringPos] = 0.0f;
            ringPos = (ringPos + 1) & (kFft - 1);
            if (++hopCount == kHop)
                analyse ();
            finish (i, l, r);
        }
    }
    else
    {
        if (bypassed)
        {
            // coming back from the bypass: the generators start from silence
            bypassed = false;
            haasLine.reset ();
            erLine.reset ();
            for (auto& a : decor)
                a.reset ();
            shiftDown.reset ();
            shiftUp.reset ();
            reverb.reset ();
            for (auto* v : {&sideHp, &midLp, &midHp})
                for (auto& b : *v)
                    b.reset ();
        }
        for (int i = 0; i < n; ++i)
        {
            wet += (wetT - wet) * smooth;
            width += (widthT - width) * smooth;
            space += (spaceT - space) * smooth;
            for (size_t g = 0; g < gen.size (); ++g)
                gen[g] += (genT[g] - gen[g]) * smooth;
            haasD += (haasDT - haasD) * slow;
            erScale += (erScaleT - erScale) * slow;
            mirror += (mix.mirror - mirror) * slow;

            const float l = inL[i], r = inR[i];
            const float m = 0.5f * (l + r), s = 0.5f * (l - r);

            // the generators, from the mid
            const float h = (float)haasLp.tick (haasLpC, haasHp.tick (haasHpC, m));
            haasLine.push (h);
            const float haas = haasLine.at (haasD);
            float d = m;
            for (auto& a : decor)
                d = a.tick (d);
            const float pitch = 0.75f * (shiftDown.tick (m) - shiftUp.tick (m));
            erLine.push (m);
            float er = 0.0f;
            for (int t = 0; t < erTaps; ++t)
                er += kErSign[t] * erGain[(size_t)t] * erLine.at (kErMs[t] * erScale);
            er = erDamp.lp (erDampA, er);
            float g = width * (gen[0] * haas + gen[1] * d + gen[2] * pitch + gen[3] * er);
            g += space * reverbSend * level * reverb.tick (0.5f * m + s);
            // temporal contrast: a transient in the mid (its short-term level above its long-term one)
            // ducks the generated side, which blooms back in the gaps
            envFast += ((double)m * m - envFast) * envFastA;
            envSlow += ((double)m * m - envSlow) * envSlowA;
            if (++duckCount >= 16)
            {
                duckCount = 0;
                duckT = 1.0f;
                if (contrast > 0.0f && envSlow > 1e-12)
                {
                    const double ratioDb = 10.0 * std::log10 (std::max (1e-6, envFast / envSlow));
                    duckT = (float)std::clamp (std::pow (10.0, -0.9 * contrast * ratioDb / 20.0), 0.18, 1.6);
                }
            }
            duck += (duckT - duck) * (duckT < duck ? duckAtt : duckRel);
            g *= duck * mirror;

            ringM[(size_t)ringPos] = m;
            ringS[(size_t)ringPos] = s;
            ringG[(size_t)ringPos] = g;
            ringPos = (ringPos + 1) & (kFft - 1);
            if (++hopCount == kHop)
                analyse ();

            // per-band gains (Mono Guard and the group), then the cues on the whole side
            double side = g;
            if (!bankFlat)
                for (int k = 0; k < kBands; ++k)
                    side = bank[(size_t)k].tick (bankC[(size_t)k], side);
            side += s;
            side = airZ.tick (airC, side);
            side = beyondZ.tick (beyondC, side);

            // Mono Below: LR8 high-pass on the side, the matching all-pass on the mid
            double lo = m, hi = m;
            for (int j = 0; j < 4; ++j)
            {
                side = sideHp[(size_t)j].tick (xHp[(size_t)(j & 1)], side);
                lo = midLp[(size_t)j].tick (xLp[(size_t)(j & 1)], lo);
                hi = midHp[(size_t)j].tick (xHp[(size_t)(j & 1)], hi);
            }
            const double mid = lo + hi;
            float L = (float)(mid + side), R = (float)(mid - side);
            if (wet < 1.0f)
            {
                L = l + (L - l) * wet;
                R = r + (R - r) * wet;
            }
            finish (i, L, R);
        }
    }
    corr = sLL * sRR > 1e-14 ? (float)std::clamp (sLR / std::sqrt (sLL * sRR), -1.0, 1.0) : 1.0f;
    if (meters)
        meters->correlation.store (corr, std::memory_order_relaxed);
    tail.process (outL, outR, n);
}

} // namespace widr

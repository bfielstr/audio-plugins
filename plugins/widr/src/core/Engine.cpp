#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace widr {

namespace {

// Per Character: the blend of the four generators in each voice (normalised, then scaled by
// `level`), the left voice's delay at Size 50 % (the right one's is 1.37 times longer), the micro
// pitch spread, how far the delays wander (ms), the early reflections (taps per voice, spacing),
// the reverb send and how much contrast is baked in. They are meant to sound clearly different.
struct CharacterMix
{
    float haas, decor, pitch, er;
    double haasMs, cents, driftMs;
    int taps;
    double erSpacing;
    float reverb, level, contrast;
};
constexpr CharacterMix kCharacters[kNumCharacters] = {
    // Tight: decorrelated voices right beside the centre, no audible delay or room
    {0.15f, 1.00f, 0.10f, 0.00f, 0.8, 3.0, 0.2, 4, 0.6, 0.3f, 0.85f, 0.6f},
    // Wide: a second take on each side, 12 and 16 ms late, drifting a little
    {1.00f, 0.30f, 0.25f, 0.20f, 12.0, 6.0, 1.2, 8, 1.0, 0.6f, 1.00f, 1.0f},
    // Epic: later, detuned, wandering takes with big reflections and the strongest contrast
    {0.80f, 0.25f, 0.70f, 0.60f, 18.0, 12.0, 2.5, 8, 1.3, 1.0f, 1.25f, 1.6f},
    // Surround: the room and the reverb lead, the voices far out and late
    {0.45f, 0.50f, 0.35f, 0.80f, 27.0, 8.0, 1.8, 8, 1.7, 2.0f, 1.15f, 1.2f},
};
// Each voice's level at Width 100 % relative to the (band-limited) mid: about -1 dB.
constexpr float kVoiceScale = 0.9f;
constexpr double kRightDelay = 1.37; // the right voice's delay relative to the left one's

// A fixed pattern of early reflections (ms at Size 50 %): even taps for the left voice, odd for the right.
constexpr int kErTaps = 16;
constexpr double kErMs[kErTaps] = {3.1, 5.3, 7.9, 11.2, 13.7, 17.3, 19.9, 23.4, 29.1, 31.7, 37.2, 41.9, 47.3, 53.6, 61.2, 67.9};

// The decorrelators' all-pass delays (samples at 48 kHz), mutually prime, 2 to 7 ms: one set per voice.
constexpr int kDecorDelays[2][4] = {{113, 173, 241, 331}, {127, 181, 257, 347}};

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

void Engine::Voice::reset ()
{
    for (auto& a : decor)
        a.reset ();
    shift.reset ();
    erDamp.reset ();
    for (auto& b : bank)
        b.reset ();
    drift = driftT = 0.0;
    driftCount = 0;
}

Engine::Engine ()
{
    gCur.fill (1.0f);
    gYieldCur.fill (1.0f);
    voice[0].seed = 0x1234567u;
    voice[1].seed = 0x89abcdeu;
    tail.setMidSide (true); // saturating mid and side apart keeps the width when pushed
}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    line.prepare ((int)(0.2 * sr) + 8);
    for (int v = 0; v < 2; ++v)
    {
        for (int i = 0; i < 4; ++i)
            voice[(size_t)v].decor[(size_t)i].prepare ((int)std::lround (kDecorDelays[v][i] * sr / 48000.0), 0.5f);
        voice[(size_t)v].shift.prepare (sr);
    }
    reverb.prepare (sr);
    revHpC = BiquadCoeffs::highPass (250.0, M_SQRT1_2, sr);
    for (auto* r : {&ringM, &ringS, &ringGm, &ringGs, &ringMono})
        r->assign (kFft, 0.0f);
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
    xHz = airDb = beyondAmt = srcHpHz = srcLpHz = -1.0;
    reset ();
}

void Engine::reset ()
{
    srcHp.reset ();
    srcLp.reset ();
    revHp.reset ();
    revLp.reset ();
    line.reset ();
    for (auto& v : voice)
        v.reset ();
    reverb.reset ();
    airZ.reset ();
    beyondZ.reset ();
    for (auto* v : {&sideHp, &midLp, &midHp})
        for (auto& b : *v)
            b.reset ();
    for (auto* r : {&ringM, &ringS, &ringGm, &ringGs, &ringMono})
        std::fill (r->begin (), r->end (), 0.0f);
    ringPos = hopCount = 0;
    for (auto* e : {&eM, &eS, &eGm, &eGs, &eMono, &pubSide})
        e->fill (0.0f);
    sLR = sLL = sRR = 0.0;
    corr = 1.0f;
    envFast = envSlow = 0.0;
    duck = duckT = 1.0f;
    duckCount = 0;
    tail.reset ();
    // settle the smoothed controls on the current settings
    blockSetup ();
    gen = genT;
    for (auto& v : voice)
        v.delay = v.delayT;
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
    const float norm = std::sqrt (c.haas * c.haas + c.decor * c.decor + c.pitch * c.pitch + c.er * c.er);
    const float gain = kVoiceScale * c.level / norm;
    genT = {c.haas * gain, c.decor * gain, c.pitch * gain, c.er * gain};
    const double left = std::clamp (c.haasMs * (0.5 + size), 0.5, 30.0) * 0.001 * sr;
    voice[0].delayT = left;
    voice[1].delayT = left * kRightDelay;
    driftDepth = c.driftMs * (0.5 + size) * 0.001 * sr;
    // ms -> samples: the pattern at 0.4x to 2x by Size, times the Character's spacing (at most 150 ms)
    erScaleT = std::min ((0.4 + 1.6 * size) * c.erSpacing, 150.0 / 68.0) * 0.001 * sr;
    level = c.level;
    contrast = (float)(std::clamp (p[kContrast], 0.0, 1.0) * c.contrast);
    erTaps = c.taps;
    // the reflections fade with time; each voice's are normalised to a power of 1
    std::array<float, 2> power {};
    for (int t = 0; t < kErTaps; ++t)
    {
        erGain[(size_t)t] = t / 2 < erTaps ? (float)std::exp (-kErMs[t] / 35.0) : 0.0f;
        power[(size_t)(t & 1)] += erGain[(size_t)t] * erGain[(size_t)t];
    }
    for (int t = 0; t < kErTaps; ++t)
        erGain[(size_t)t] /= std::sqrt (std::max (1e-9f, power[(size_t)(t & 1)]));
    reverbSend = c.reverb;
    reverb.set (p[kDecay], p[kPreDelay], p[kDamping]);
    erDampA = OnePole::coeff (p[kDamping], sr);
    // the voices' source: the mid above Mono Below, below a tone that follows Damping
    const double hpHz = std::max (60.0, p[kMonoBelow]), lpHz = std::min (0.45 * sr, p[kDamping] * 1.6);
    if (std::fabs (hpHz - srcHpHz) > 1e-3 || std::fabs (lpHz - srcLpHz) > 1e-3)
    {
        srcHpHz = hpHz;
        srcLpHz = lpHz;
        srcHpC = BiquadCoeffs::highPass (hpHz, M_SQRT1_2, sr);
        srcLpC = BiquadCoeffs::lowPass (lpHz, M_SQRT1_2, sr);
        revLpC = BiquadCoeffs::lowPass (std::min (5000.0, p[kDamping]), M_SQRT1_2, sr);
    }
    // micro pitch: a few cents down on the left and up on the right, each drifting on its own LFO
    voice[0].shift.setCents (-c.cents * (1.0 + 0.3 * std::sin (2.0 * M_PI * 0.11 * lfoPhase)));
    voice[1].shift.setCents (c.cents * (1.0 + 0.3 * std::sin (2.0 * M_PI * 0.17 * lfoPhase + 1.0)));
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
    bands (ringGm, eGm);
    bands (ringGs, eGs);
    bands (ringMono, eMono);

    // Mono Guard, per band: what the voices add to the mono fold stays under monoGain (1.2 dB at
    // 100 %), and the side stays under ratio * mid (a floor under the correlation, c0)
    const double guard = std::clamp (p[kGuard], 0.0, 1.0);
    const double monoGain = guard > 0.0 ? (std::pow (10.0, 0.12) - 1.0) / guard : 0.0;
    const double c0 = -1.0 + 1.2 * guard;
    const double ratio = (1.0 - c0) / std::max (1e-6, 1.0 + c0);
    const float ag = (float)(1.0 - std::exp (-(double)kHop / (0.2 * sr))); // 200 ms: nothing pumps
    // spectral contrast: the voices give way where the mid stands out from its neighbouring bands
    // and fill where it is thin (-9 .. +6 dB at full contrast)
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
        double gGuard = 2.0;
        if (guard > 0.0)
        {
            if (eGm[i] > 1e-12f)
            {
                // the mono fold with the voices at gain g: eM + g^2 eGm + 2 g x, where x (the part of
                // the voices in phase with the mid) comes from the measured sum; keep it under
                // (1 + monoGain) eM
                const double x = 0.5 * ((double)eMono[i] - eM[i] - eGm[i]);
                const double g = (-x + std::sqrt (std::max (0.0, x * x + (double)eGm[i] * monoGain * eM[i]))) / eGm[i];
                gGuard = std::min (gGuard, std::max (0.0, g));
            }
            if (eGs[i] > 1e-12f)
                gGuard = std::min (gGuard, std::sqrt (std::clamp ((ratio * eM[i] - eS[i]) / eGs[i], 0.0, 4.0)));
        }
        const float target = std::min ((float)gGuard, gContrast[i]) * std::clamp (mix.yield[i], 0.0f, 1.0f);
        gCur[i] += (target - gCur[i]) * ag;
        gYieldCur[i] += (mix.yield[i] - gYieldCur[i]) * ag;
        pubSide[i] = eGs[i] * gCur[i] * gCur[i];
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
        for (auto& v : voice)
            for (auto& b : v.bank)
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
    const double driftA = 1.0 - std::exp (-1.0 / (0.2 * sr));
    const int driftEvery = std::max (1, (int)(0.1 * sr));

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
    auto capture = [&] (float m, float s, float gm, float gs) {
        const size_t k = (size_t)ringPos;
        ringM[k] = m;
        ringS[k] = s;
        ringGm[k] = gm;
        ringGs[k] = gs;
        ringMono[k] = m + gm;
        ringPos = (ringPos + 1) & (kFft - 1);
        if (++hopCount == kHop)
            analyse ();
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
            capture (0.5f * (l + r), 0.5f * (l - r), 0.0f, 0.0f);
            finish (i, l, r);
        }
    }
    else
    {
        if (bypassed)
        {
            // coming back from the bypass: the voices start from silence
            bypassed = false;
            line.reset ();
            for (auto& v : voice)
                v.reset ();
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
            erScale += (erScaleT - erScale) * slow;
            mirror += (mix.mirror - mirror) * slow;

            const float l = inL[i], r = inR[i];
            const float m = 0.5f * (l + r), s = 0.5f * (l - r);

            // the voices' source: the band-limited mid
            const float src = (float)srcLp.tick (srcLpC, srcHp.tick (srcHpC, m));
            line.push (src);

            // the reverb: fed band-limited, a left and a right output
            float revL, revR;
            reverb.tick ((float)revLp.tick (revLpC, revHp.tick (revHpC, 0.5f * m + s)), revL, revR);

            // temporal contrast: a transient in the mid (its short-term level above its long-term one)
            // ducks the voices, which bloom back in the gaps
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

            // the two voices
            std::array<float, 2> out {};
            for (int v = 0; v < 2; ++v)
            {
                Voice& vc = voice[(size_t)v];
                vc.delay += (vc.delayT - vc.delay) * slow;
                // the take wanders: a new random target every 100 ms, reached smoothly
                if (++vc.driftCount >= driftEvery)
                {
                    vc.driftCount = 0;
                    vc.seed = vc.seed * 1664525u + 1013904223u;
                    vc.driftT = (double)((vc.seed >> 8) & 0xFFFF) / 32768.0 - 1.0;
                }
                vc.drift += (vc.driftT - vc.drift) * driftA;
                const float take = line.at (std::max (1.0, vc.delay + vc.drift * driftDepth));
                float d = src;
                for (auto& ap : vc.decor)
                    d = ap.tick (d);
                const float pitched = vc.shift.tick (src);
                float er = 0.0f;
                for (int t = v; t < kErTaps; t += 2)
                    if (erGain[(size_t)t] > 0.0f)
                        er += erGain[(size_t)t] * line.at (kErMs[t] * erScale);
                er = vc.erDamp.lp (erDampA, er);
                const float rev = v == 0 ? revL : revR;
                out[(size_t)v] = (width * (gen[0] * take + gen[1] * d + gen[2] * pitched + gen[3] * er) +
                                  space * reverbSend * level * rev) *
                                 duck;
            }
            // a twin of the same role gets the voices the other way round
            const float a = 0.5f * (1.0f + mirror);
            float vl = a * out[0] + (1.0f - a) * out[1], vr = a * out[1] + (1.0f - a) * out[0];
            capture (m, s, 0.5f * (vl + vr), 0.5f * (vl - vr));

            // per-band gains (contrast, Mono Guard, the group)
            if (!bankFlat)
                for (int k = 0; k < kBands; ++k)
                {
                    vl = (float)voice[0].bank[(size_t)k].tick (bankC[(size_t)k], vl);
                    vr = (float)voice[1].bank[(size_t)k].tick (bankC[(size_t)k], vr);
                }
            double mid = m + 0.5 * ((double)vl + vr), side = s + 0.5 * ((double)vl - vr);
            side = airZ.tick (airC, side);
            side = beyondZ.tick (beyondC, side);

            // Mono Below: LR8 high-pass on the side, the matching all-pass on the mid
            double lo = mid, hi = mid;
            for (int j = 0; j < 4; ++j)
            {
                side = sideHp[(size_t)j].tick (xHp[(size_t)(j & 1)], side);
                lo = midLp[(size_t)j].tick (xLp[(size_t)(j & 1)], lo);
                hi = midHp[(size_t)j].tick (xHp[(size_t)(j & 1)], hi);
            }
            mid = lo + hi;
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

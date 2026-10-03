#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace multidyn {

namespace {
constexpr float kMaxBoostDb = 36.0f, kMaxCutDb = -80.0f;
inline bool on (double v) { return v >= 0.5; }
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1e-6f)); }

double kneeGain (double amount, double slope, bool softKnee, double kneeDb)
{
    // amount: dB beyond the threshold (positive = beyond), slope: dB of gain per dB beyond
    if (!softKnee)
        return amount > 0.0 ? amount * slope : 0.0;
    const double h = kneeDb * 0.5;
    if (amount <= -h)
        return 0.0;
    if (amount >= h)
        return amount * slope;
    return slope * (amount + h) * (amount + h) / (2.0 * kneeDb);
}
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

double aboveGainDb (double x, double thresh, double ratio, bool softKnee, double kneeDb)
{
    return kneeGain (x - thresh, 1.0 / std::max (0.01, ratio) - 1.0, softKnee, kneeDb);
}

double belowGainDb (double x, double thresh, double ratio, bool softKnee, double kneeDb)
{
    // Below the threshold the output slope is 1/ratio: ratio > 1 lifts quiet material (upward
    // compression), ratio < 1 pushes it down (downward expansion).
    return kneeGain (thresh - x, 1.0 - 1.0 / std::max (0.01, ratio), softKnee, kneeDb);
}

Engine::Engine (bool withTail) : hasTail (withTail)
{
    // Soften's Color: Smacheratr with only its high colour, around the Analog curve at Drive 0 dB
    using namespace smacheratr;
    color.setParam (kColorOn, 1.0);
    color.setParam (kColorLo, 0.0);
    color.setParam (kColorFreq, kColorPeakHz);
    color.setParam (kColorWidth, kColorPeakWidth);
    color.setParam (smacheratr::kDrive, 0.0);
    color.setParam (smacheratr::kPreLimit, 0.0);
    color.setParam (kPostClip, kPostOff);
    color.setParam (smacheratr::kOutput, 0.0);
    color.setParam (kHiQuality, 1.0);
    color.setParam (kDcFilter, 0.0);
    color.setParam (kMidSide, 0.0);
    color.setParam (kClarity, 0.0);
    color.setParam (kClarityAdvanced, 0.0);
    color.setParam (kClarityDrive, 0.0);
    color.setParam (kDryWet, 1.0);
    syncColor ();
}

void Engine::syncColor () { color.setParam (smacheratr::kColorHi, softenColorAmount (p[kSoften])); }

void Engine::processColor (float* L, float* R, int n)
{
    const bool want = on (p[kSoftenColor]);
    const int len = (int)colorDelay[0].size ();
    for (int pos = 0; pos < n; pos += kColorChunk)
    {
        const int m = std::min (kColorChunk, n - pos);
        float* ch[2] = {L + pos, R + pos};
        if (colorState == ColorState::Off && want)
        {
            // starting: from silence, beside the dry path, not heard until it has settled
            color.reset ();
            colorState = ColorState::Warming;
            colorPos = 0;
            colorMix = 0.0f;
        }
        const bool running = colorState != ColorState::Off;
        if (running)
        {
            for (int c = 0; c < 2; ++c)
                std::copy (ch[c], ch[c] + m, colorWet[c]);
            color.process (colorWet[0], colorWet[1], colorWet[0], colorWet[1], m);
        }
        for (int i = 0; i < m; ++i)
        {
            // the dry path: the same latency as the colour's
            float dry[2];
            for (int c = 0; c < 2; ++c)
            {
                dry[c] = colorDelay[c][(size_t)colorDelayPos];
                colorDelay[c][(size_t)colorDelayPos] = ch[c][i];
            }
            if (++colorDelayPos >= len)
                colorDelayPos = 0;
            if (colorState == ColorState::Warming)
            {
                if (!want)
                    colorState = ColorState::Off;
                else if (++colorPos >= colorWarm)
                    colorState = ColorState::In;
            }
            else if (colorState == ColorState::In)
            {
                colorMix = std::clamp (colorMix + (want ? colorStep : -colorStep), 0.0f, 1.0f);
                if (colorMix <= 0.0f)
                    colorState = ColorState::Off; // faded out: it stops
            }
            if (!running || colorMix <= 0.0f)
            {
                ch[0][i] = dry[0];
                ch[1][i] = dry[1];
            }
            else if (colorMix >= 1.0f)
            {
                ch[0][i] = colorWet[0][i];
                ch[1][i] = colorWet[1][i];
            }
            else
                for (int c = 0; c < 2; ++c)
                    ch[c][i] = dry[c] + (colorWet[c][i] - dry[c]) * colorMix;
        }
    }
}

void Engine::Bank::tune (int j, float g)
{
    split[j].setup (g, slope);
    scSplit[j].setup (g, slope);
    for (int b = 0; b < kMaxBands - 1; ++b)
    {
        ap[b][j].setup (g, slope);
        scAp[b][j].setup (g, slope);
    }
    subAp[j].setup (g, slope);
    scSubAp[j].setup (g, slope);
}

void Engine::Bank::tuneSub (float g)
{
    sub.setup (g, slope);
    scSub.setup (g, slope);
}

void Engine::Bank::resetSub ()
{
    sub.reset ();
    scSub.reset ();
    for (int j = 0; j < kMaxBands - 1; ++j)
    {
        subAp[j].reset ();
        scSubAp[j].reset ();
    }
}

void Engine::Bank::reset ()
{
    for (int j = 0; j < kMaxBands - 1; ++j)
    {
        split[j].reset ();
        scSplit[j].reset ();
        for (int b = 0; b < kMaxBands - 1; ++b)
        {
            ap[b][j].reset ();
            scAp[b][j].reset ();
        }
    }
    resetSub ();
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    // Level detection. Peak: instant attack, 60 ms release (holds the peak between cycles, so
    // the gain doesn't collapse at zero crossings). RMS: the RMS Window (see process).
    peakCoefC = (float)std::exp (-1.0 / (0.060 * sr));
    meterFall = (float)std::exp (-1.0 / (0.300 * sr));    // meter decay
    smooth = (float)(1.0 - std::exp (-1.0 / (0.010 * sr))); // parameter smoothing
    // pre-limiter: the gain reaches most of its reduction over the look-ahead, releases in 50 ms
    look = std::clamp ((int)std::lround (0.001 * sr), 1, kMaxLookahead);
    limAtk = (float)std::exp (-4.0 / look);
    limPeakDecay = (float)std::exp (-1.0 / (2.0 * look)); // the input peak is held about twice the look-ahead
    guardC = (float)std::exp (-1.0 / (0.003 * sr));
    limRel = (float)std::exp (-1.0 / (0.050 * sr));
    sat.prepare (sr, 512);
    color.prepare (sr, kColorChunk);
    colorWarm = color.latency () + (int)std::lround (0.010 * sr);
    colorStep = (float)(1.0 / (0.020 * sr));
    for (auto& d : colorDelay)
        d.assign ((size_t)std::max (1, color.latency ()), 0.0f);
    fadeWarm = (int)std::lround (0.030 * sr); // a new slope's filters settle, then the bands crossfade
    fadeLen = std::max (1, (int)std::lround (0.020 * sr));
    subWarm = (int)std::lround (0.100 * sr); // the Sub band settles, then fades in
    subStep = (float)(1.0 / (0.030 * sr));
    for (auto& d : bypassDelay)
        d.assign ((size_t)std::max (1, latency ()), 0.0f);
    bypassPos = 0;
    reset ();
}

void Engine::reset ()
{
    fadePos = -1;
    banks[cur].slope = std::clamp ((int)std::lround (p[kXoverSlope]), 0, kNumXoverSlopes - 1);
    for (auto& bank : banks)
        bank.reset ();
    for (int b = 0; b < kNumBands; ++b)
    {
        bands[b] = BandState {};
        bands[b].inGain = dbToGain (p[bandParam (b, kBandInput)] + kBakedInputDb);
        const bool ott = std::lround (p[kStyle]) == kStyleOtt; // (OTT style: its own makeup, no baked gains)
        bands[b].outGain = dbToGain (p[bandParam (b, kBandOutput)] + (ott ? 0.0 : kBakedOutputDb[b]));
        meters[b] = BandMeter {};
    }
    bands[kSubBand] = BandState {};
    bands[kSubBand].outGain = dbToGain (p[kSubOutput]);
    meters[kSubBand] = BandMeter {};
    subState = on (p[kSubOn]) ? SubState::In : SubState::Off;
    subMix = subState == SubState::In ? 1.0f : 0.0f;
    outGain = dbToGain (p[kOutput] + kBakedMasterDb);
    scGain = dbToGain (p[kScGain]);
    syncSaturator ();
    sat.reset ();
    syncColor ();
    color.reset ();
    for (auto& d : colorDelay)
        std::fill (d.begin (), d.end (), 0.0f);
    colorDelayPos = 0;
    colorState = on (p[kSoftenColor]) ? ColorState::In : ColorState::Off;
    colorMix = colorState == ColorState::In ? 1.0f : 0.0f;
    updateFilters (true);
}

void Engine::syncSaturator ()
{
    for (uint32_t f = 0; f < pk::kTailFields; ++f)
        sat.setParam (f, p[kSatOn + f]);
    for (uint32_t f = 0; f < pk::kTailExtFields; ++f)
        sat.setParam (pk::kTailFields + f, p[kSatExtBase + f]);
    for (uint32_t f = 0; f < pk::kTailExt2Fields; ++f)
        sat.setParam (pk::kTailFields + pk::kTailExtFields + f, p[kSatExt2Base + f]);
}

int Engine::bandCount () const { return std::clamp ((int)std::lround (p[kBands]) + 1, 1, kMaxBands); }

void Engine::updateFilters (bool force)
{
    const int n = bandCount ();
    const float fsr = (float)sr;
    const float top = 0.45f * fsr;
    float prev = 0.0f;
    for (int j = 0; j < kMaxBands - 1; ++j)
    {
        float t = (float)p[kXover1 + j];
        if (j < n - 1 && j > 0)
            t = std::max (t, prev * 1.2f); // keep every band open and in order
        t = std::min (t, top / std::pow (1.2f, (float)(kMaxBands - 2 - j)));
        prev = t;
        // glide crossover changes in the log domain to avoid zipper noise
        const float next =
            force || xf[j] <= 0.0f ? t : std::exp (std::log (xf[j]) + 0.35f * (std::log (t) - std::log (xf[j])));
        if (force || std::fabs (next - xf[j]) > 1e-3f * xf[j])
        {
            xf[j] = next;
            xg[j] = xoverG (next, fsr);
            banks[cur].tune (j, xg[j]);
            if (fadePos >= 0)
                banks[cur ^ 1].tune (j, xg[j]);
        }
    }
    // the Sub band's corner, gliding the same way
    const float st = (float)std::clamp (p[kSubFreq], 20.0, 100.0);
    const float nextSub = force || subF <= 0.0f ? st : std::exp (std::log (subF) + 0.35f * (std::log (st) - std::log (subF)));
    if (force || std::fabs (nextSub - subF) > 1e-3f * subF)
    {
        subF = nextSub;
        subG = xoverG (nextSub, fsr);
        banks[cur].tuneSub (subG);
        if (fadePos >= 0)
            banks[cur ^ 1].tuneSub (subG);
    }
}

void Engine::startSub ()
{
    // from silence, beside the bands, not heard until it has settled
    for (auto& bank : banks)
        bank.resetSub ();
    bands[kSubBand] = BandState {};
    bands[kSubBand].outGain = dbToGain (p[kSubOutput]);
    subState = SubState::Warming;
    subPos = 0;
    subMix = 0.0f;
}

void Engine::splitWithSub (float x, int c, int n, bool sc, Bank& bk, float mix, float* out)
{
    float lo, hi;
    (sc ? bk.scSub : bk.sub).tick (x, c, lo, hi);
    // the bands' input: the input as it is, going over to the part above the corner
    const float rest = mix >= 1.0f ? hi : x + (hi - x) * mix;
    if (sc)
        splitBands (rest, c, n, bk.scSplit, bk.scAp, out);
    else
        splitBands (rest, c, n, bk.split, bk.ap, out);
    XoverAllpass* aps = sc ? bk.scSubAp : bk.subAp;
    for (int j = 0; j < n - 1; ++j)
        lo = aps[j].tick (lo, c);
    out[kSubBand] = lo;
}

void Engine::splitBands (float x, int c, int n, XoverSplit* sp, XoverAllpass (*aps)[kMaxBands - 1], float* out)
{
    float rest = x;
    for (int j = 0; j < n - 1; ++j)
    {
        float lo, hi;
        sp[j].tick (rest, c, lo, hi);
        out[j] = lo;
        rest = hi;
    }
    out[n - 1] = rest;
    for (int b = 0; b + 2 < n; ++b)
        for (int j = b + 1; j < n - 1; ++j)
            out[b] = aps[b][j].tick (out[b], c);
}

void Engine::process (const float* inL, const float* inR, const float* scL, const float* scR, float* outL,
                      float* outR, int n)
{
    if (bypass)
    {
        const int len = (int)bypassDelay[0].size ();
        for (int i = 0; i < n; ++i)
        {
            const float l = bypassDelay[0][(size_t)bypassPos], r = bypassDelay[1][(size_t)bypassPos];
            bypassDelay[0][(size_t)bypassPos] = inL[i];
            bypassDelay[1][(size_t)bypassPos] = inR[i];
            if (++bypassPos >= len)
                bypassPos = 0;
            outL[i] = l;
            outR[i] = r;
        }
        for (auto& m : meters)
            m = BandMeter {};
        return;
    }
    updateFilters (false);
    const int nBands = bandCount ();
    const bool softKnee = on (p[kSoftKnee]);
    const double kneeDb = kCharacterKneeDb;
    const bool rms = std::lround (p[kDetector]) == kRms;
    const double rmsWindowMs = std::clamp (p[kRmsWindow], 1.0, 1000.0);
    const float rmsC = (float)std::exp (-1.0 / (rmsWindowMs * 0.001 * sr)), peakC = peakCoefC;
    const bool preLimit = on (p[kPreLimit]);
    const double ceilingOffset = p[kPreLimitCeiling]; // dB above each band's Above threshold
    const double amount = std::clamp (p[kAmount], 0.0, 1.0);
    const double timeScale = std::max (0.01, p[kTime]);
    const bool scActive = on (p[kScOn]) && scL != nullptr;
    const float scMix = scActive ? (float)std::clamp (p[kScMix], 0.0, 1.0) : 0.0f;
    const bool listen = on (p[kScListen]) && scActive;
    const float outTarget = dbToGain (p[kOutput] + kBakedMasterDb), scTarget = dbToGain (p[kScGain]);
    const int slopeT = std::clamp ((int)std::lround (p[kXoverSlope]), 0, kNumXoverSlopes - 1);
    const bool wantSub = on (p[kSubOn]);
    const bool ottStyle = std::lround (p[kStyle]) == kStyleOtt;
    syncSaturator ();
    syncColor ();

    // the bands, then the Sub band (kSubBand: its parameters in a band's terms, Above only)
    constexpr int kAll = kNumBands + 1;
    bool used[kAll], active[kAll], audible[kAll];
    bool anySolo = false;
    for (int b = 0; b < kNumBands; ++b)
    {
        used[b] = bandUsed (b);
        active[b] = on (p[bandParam (b, kBandActive)]);
        anySolo |= used[b] && on (p[bandParam (b, kBandSolo)]);
    }
    active[kSubBand] = true;
    audible[kSubBand] = !anySolo;
    float inTarget[kAll], outTargetB[kAll], atk[kAll], rel[kAll], relSlow[kAll], ceiling[kAll];
    double ta[kAll], ra[kAll], tb[kAll], rb[kAll];
    for (int b = 0; b < kAll; ++b)
    {
        if (b == kSubBand)
        {
            inTarget[b] = 1.0f;
            outTargetB[b] = dbToGain (p[kSubOutput]);
            const double attackMs = std::max (0.01, p[kSubAttack] * timeScale / 3.0);
            const double releaseMs = std::max (0.1, p[kSubRelease] * timeScale / 3.0);
            atk[b] = (float)std::exp (-1.0 / (attackMs * 0.001 * sr));
            rel[b] = (float)std::exp (-1.0 / (releaseMs * 0.001 * sr));
            relSlow[b] = (float)std::exp (-1.0 / (3.0 * releaseMs * 0.001 * sr));
            ta[b] = p[kSubThresh];
            ceiling[b] = dbToGain (ta[b] + ceilingOffset);
            ra[b] = p[kSubRatio];
            tb[b] = -120.0; // no Below: ratio 1:1
            rb[b] = 1.0;
            continue;
        }
        audible[b] = used[b] && (!anySolo || on (p[bandParam (b, kBandSolo)]));
        inTarget[b] = dbToGain (p[bandParam (b, kBandInput)] + kBakedInputDb);
        outTargetB[b] = dbToGain (p[bandParam (b, kBandOutput)] + kBakedOutputDb[b]);
        // Attack/Release are the time to (almost) complete the change - about 95 % - as in
        // "time to reach maximum compression", so the envelope time constant is a third of it.
        const double attackMs = std::max (0.01, p[bandParam (b, kAttack)] * timeScale / 3.0);
        const double releaseMs = std::max (0.1, p[bandParam (b, kRelease)] * timeScale / 3.0);
        atk[b] = (float)std::exp (-1.0 / (attackMs * 0.001 * sr));
        rel[b] = (float)std::exp (-1.0 / (releaseMs * 0.001 * sr));
        relSlow[b] = (float)std::exp (-1.0 / (3.0 * releaseMs * 0.001 * sr)); // deep gain changes
        ta[b] = p[bandParam (b, kAboveThresh)];
        ceiling[b] = dbToGain (ta[b] + ceilingOffset);
        ra[b] = p[bandParam (b, kAboveRatio)];
        tb[b] = p[bandParam (b, kBelowThresh)];
        rb[b] = p[bandParam (b, kBelowRatio)];
    }
    // OTT style (Ott.h): each band's kind, envelope coefficients and how far its controls moved it
    int ottKind[kAll] {};
    double ottAtk[kAll] {}, ottRel[kAll] {}, ottUp[kAll] {}, ottDown[kAll] {}, ottUpShift[kAll] {}, ottDownShift[kAll] {};
    if (ottStyle)
    {
        const auto& t = paramTable ();
        auto def = [&t] (int b, int f) { return t.info (bandParam (b, f)).def; };
        auto strength = [] (double r, double r0) { return (1.0 - 1.0 / std::max (1e-3, r)) / (1.0 - 1.0 / r0); };
        for (int b = 0; b < kNumBands; ++b)
        {
            const int k = ott::bandKind (b, nBands);
            ottKind[b] = k;
            const double atkSec = ott::kAttack[k] * p[bandParam (b, kAttack)] / def (b, kAttack);
            const double relSec = ott::releaseSec (k, 100.0 * p[kTime]) * p[bandParam (b, kRelease)] / def (b, kRelease);
            ottAtk[b] = 1.0 - std::exp (-1.0 / (std::max (1e-5, atkSec) * sr));
            ottRel[b] = 1.0 - std::exp (-1.0 / (std::max (1e-4, relSec) * sr));
            ottUp[b] = strength (rb[b], def (b, kBelowRatio));
            ottDown[b] = strength (ra[b], def (b, kAboveRatio));
            ottUpShift[b] = tb[b] - def (b, kBelowThresh);
            ottDownShift[b] = ta[b] - def (b, kAboveThresh);
            // OTT's makeup replaces the preset gains baked in for Character
            outTargetB[b] = dbToGain (p[bandParam (b, kBandOutput)]);
        }
    }
    // skip the gain computers when nothing would happen (ratio 1:1 everywhere or amount 0)
    bool neutral[kAll];
    for (int b = 0; b < kAll; ++b)
        neutral[b] = amount <= 0.0 || (std::fabs (ra[b] - 1.0) < 1e-6 && std::fabs (rb[b] - 1.0) < 1e-6);
    // Soften, on the top band: how far it is engaged (the thresholds' closeness times the knob)
    const int top = nBands - 1;
    const double soften =
        std::clamp (p[kSoften], 0.0, 1.0) * std::clamp ((18.0 - (ta[top] - tb[top])) / 12.0, 0.0, 1.0);
    const float gainSmooth = soften > 1e-3 ? (float)(1.0 - std::exp (-1.0 / (soften * 0.010 * sr))) : 1.0f;
    const double topKneeDb = (softKnee ? kneeDb : 0.0) + (softKnee ? 12.0 : 18.0) * soften;
    const float liftBlend = (float)soften, liftKeep = (float)(1.0 - 0.5 * soften);
    const float liftA = (float)(1.0 - std::exp (-2.0 * M_PI * std::min (8000.0 * std::pow (3500.0 / 8000.0, soften), 0.4 * sr) / sr));

    for (int i = 0; i < n; ++i)
    {
        outGain += (outTarget - outGain) * smooth;
        scGain += (scTarget - scGain) * smooth;
        // a new Slope: the other bank starts from silence with it, settles, and the bands crossfade to it
        if (fadePos < 0 && slopeT != banks[cur].slope)
        {
            Bank& fresh = banks[cur ^ 1];
            fresh.slope = slopeT;
            for (int j = 0; j < kMaxBands - 1; ++j)
                fresh.tune (j, xg[j]);
            fresh.tuneSub (subG);
            fresh.reset ();
            fadePos = 0;
        }
        const bool fading = fadePos >= 0;
        const float w = fading ? std::clamp ((float)(fadePos - fadeWarm) / (float)fadeLen, 0.0f, 1.0f) : 0.0f;
        Bank& bk = banks[cur];
        Bank& nb = banks[cur ^ 1];
        // the Sub band: starting, settling, fading in or out (off, nothing of it runs)
        if (subState == SubState::Off && wantSub)
            startSub ();
        if (subState == SubState::Warming)
        {
            if (!wantSub)
                subState = SubState::Off;
            else if (++subPos >= subWarm)
                subState = SubState::In;
        }
        else if (subState == SubState::In)
        {
            subMix = std::clamp (subMix + (wantSub ? subStep : -subStep), 0.0f, 1.0f);
            if (subMix <= 0.0f)
                subState = SubState::Off; // faded out: it stops
        }
        const bool subRun = subState != SubState::Off;
        used[kSubBand] = subRun;
        float xs[2] = {inL[i], inR[i]};
        float band[kAll][2] {};
        float scBand[kAll][2] {};
        const int nOut = subRun ? kAll : nBands;
        for (int c = 0; c < 2; ++c)
        {
            float tmp[kAll] {}, tmp2[kAll] {};
            if (subRun)
                splitWithSub (xs[c], c, nBands, false, bk, subMix, tmp);
            else
                splitBands (xs[c], c, nBands, bk.split, bk.ap, tmp);
            if (fading)
            {
                if (subRun)
                    splitWithSub (xs[c], c, nBands, false, nb, subMix, tmp2);
                else
                    splitBands (xs[c], c, nBands, nb.split, nb.ap, tmp2);
                for (int bb = 0; bb < nOut; ++bb)
                    tmp[bb] += (tmp2[bb] - tmp[bb]) * w;
            }
            for (int bb = 0; bb < nOut; ++bb)
                band[bb][c] = tmp[bb];
            if (scActive)
            {
                const float sx = (c == 0 ? scL[i] : scR[i]) * scGain;
                if (subRun)
                    splitWithSub (sx, c, nBands, true, bk, subMix, tmp);
                else
                    splitBands (sx, c, nBands, bk.scSplit, bk.scAp, tmp);
                if (fading)
                {
                    if (subRun)
                        splitWithSub (sx, c, nBands, true, nb, subMix, tmp2);
                    else
                        splitBands (sx, c, nBands, nb.scSplit, nb.scAp, tmp2);
                    for (int bb = 0; bb < nOut; ++bb)
                        tmp[bb] += (tmp2[bb] - tmp[bb]) * w;
                }
                for (int bb = 0; bb < nOut; ++bb)
                    scBand[bb][c] = tmp[bb];
            }
        }
        if (fading && ++fadePos > fadeWarm + fadeLen)
        {
            cur ^= 1; // the new bank is all there is now
            fadePos = -1;
        }

        float sumL = 0.0f, sumR = 0.0f;
        for (int b = 0; b < kAll; ++b)
        {
            if (!used[b])
                continue;
            BandState& st = bands[b];
            // the look-ahead delay (every band, always: the latency never changes)
            const float dl = st.delay[0][st.delayPos], dr = st.delay[1][st.delayPos];
            st.delay[0][st.delayPos] = band[b][0];
            st.delay[1][st.delayPos] = band[b][1];
            if (++st.delayPos >= look)
                st.delayPos = 0;
            float yl, yr;
            if (!active[b])
            {
                // deactivated: no dynamics and no band gains
                yl = dl;
                yr = dr;
                st.limGain = 1.0f;
                st.meterIn = std::max (std::max (std::fabs (yl), std::fabs (yr)), st.meterIn * meterFall);
                st.meterOut = st.meterIn;
            }
            else
            {
                st.inGain += (inTarget[b] - st.inGain) * smooth;
                st.outGain += (outTargetB[b] - st.outGain) * smooth;
                float xl = dl * st.inGain, xr = dr * st.inGain;
                if (preLimit)
                {
                    // the gain starts moving `look` samples before the peak reaches the output
                    const float ahead = std::max (std::fabs (band[b][0]), std::fabs (band[b][1])) * st.inGain;
                    st.limPeak = std::max (ahead, st.limPeak * limPeakDecay);
                    const float need = st.limPeak > ceiling[b] ? ceiling[b] / st.limPeak : 1.0f;
                    st.limGain = need < st.limGain ? need + limAtk * (st.limGain - need) : 1.0f + limRel * (st.limGain - 1.0f);
                    xl *= st.limGain;
                    xr *= st.limGain;
                }
                else
                    st.limGain = 1.0f;
                float g = 1.0f;
                float wl = xl, wr = xr; // what the gain applies to (Soften reshapes it)
                if (!neutral[b] && ottStyle && b != kSubBand)
                {
                    // OTT style: the band's stereo mean square, enveloped, into OTT's gain law (Ott.h)
                    const float dlv = xl * (1.0f - scMix) + scBand[b][0] * scMix;
                    const float drv = xr * (1.0f - scMix) + scBand[b][1] * scMix;
                    const double x2 = 0.5 * ((double)dlv * dlv + (double)drv * drv);
                    st.ottEnv += (x2 > st.ottEnv ? ottAtk[b] : ottRel[b]) * (x2 - st.ottEnv);
                    if (st.ottEnv < 1e-30)
                        st.ottEnv = 0.0;
                    const double e = 10.0 * std::log10 (std::max (st.ottEnv, 1e-20));
                    const int k = ottKind[b];
                    const double gDb = ott::gainDb (k, e, amount, ottUp[b], ottDown[b], ottUpShift[b], ottDownShift[b]);
                    // the meters: the dynamic part (what is over OTT's makeup)
                    const double makeup = ott::makeupShape (k, amount) * ott::kMakeup[k];
                    st.belowDb = (float)std::max (0.0, gDb - makeup);
                    st.aboveDb = (float)std::min (0.0, gDb - makeup);
                    g = dbToGain ((float)gDb);
                    st.liftLp[0][0] = st.liftLp[0][1] = st.liftLp[1][0] = st.liftLp[1][1] = 0.0f;
                }
                else if (!neutral[b])
                {
                    // detector: the band itself, the side-chain band, or a blend of both
                    const float dlv = xl * (1.0f - scMix) + scBand[b][0] * scMix;
                    const float drv = xr * (1.0f - scMix) + scBand[b][1] * scMix;
                    float level;
                    if (rms)
                    {
                        st.rms = rmsC * st.rms + (1.0f - rmsC) * 0.5f * (dlv * dlv + drv * drv);
                        level = std::sqrt (st.rms);
                    }
                    else
                    {
                        const float inst = std::max (std::fabs (dlv), std::fabs (drv));
                        st.peak = std::max (inst, st.peak * peakC);
                        level = st.peak;
                    }
                    const float lev = std::max (-120.0f, gainToDb (level));
                    // the release slows down the deeper the current gain change
                    const float deep = std::min (1.0f, std::fabs (st.aboveDb + st.belowDb) / 12.0f);
                    const float relA = rel[b] + (relSlow[b] - rel[b]) * deep, relB = relA;
                    // Level-domain envelopes feed the static curves (so the audible timing
                    // depends on how far the level is past a threshold, as in the original):
                    // Above reacts with Attack to rising levels, Below with Attack to falling ones.
                    st.envAbove = lev + (lev > st.envAbove ? atk[b] : relA) * (st.envAbove - lev);
                    st.envBelow = lev + (lev < st.envBelow ? atk[b] : relB) * (st.envBelow - lev);
                    // a second stage rounds the onset of the gain change
                    const float eA1 = st.envAbove, eB1 = st.envBelow;
                    st.envAbove2 = eA1 + (eA1 > st.envAbove2 ? atk[b] : relA) * (st.envAbove2 - eA1);
                    st.envBelow2 = eB1 + (eB1 < st.envBelow2 ? atk[b] : relB) * (st.envBelow2 - eB1);
                    const float eA = st.envAbove2, eB = st.envBelow2;
                    const bool softened = b == top && soften > 1e-3;
                    const bool knee = softened ? topKneeDb > 0.01 : softKnee;
                    const double kDb = softened ? topKneeDb : kneeDb;
                    float aDb = (float)(aboveGainDb (eA, ta[b], ra[b], knee, kDb) * amount);
                    float bDb = (float)(belowGainDb (eB, tb[b], rb[b], knee, kDb) * amount);
                    // Upward compression never lifts the signal past the Below threshold, even while
                    // its envelope is still releasing (e.g. a loud hit right after silence), and the
                    // transient guard sees the hit coming (see Engine.h)
                    const float gl = band[b][0] * st.inGain * (1.0f - scMix) + scBand[b][0] * scMix;
                    const float gr = band[b][1] * st.inGain * (1.0f - scMix) + scBand[b][1] * scMix;
                    float fast;
                    if (rms)
                    {
                        st.guardMs = guardC * st.guardMs + (1.0f - guardC) * 0.5f * (gl * gl + gr * gr);
                        fast = std::sqrt (st.guardMs);
                    }
                    else
                    {
                        st.guardPeak = std::max (std::max (std::fabs (gl), std::fabs (gr)), st.guardPeak * peakC);
                        fast = st.guardPeak;
                    }
                    const float guardRoom = std::max (0.0f, (float)tb[b] - std::max (-120.0f, gainToDb (fast)));
                    if (st.guardDb > 500.0f)
                        st.guardDb = guardRoom;
                    else if (guardRoom < st.guardDb) // down within the look-ahead, back up at the Release
                        st.guardDb = guardRoom + limAtk * (st.guardDb - guardRoom);
                    else
                        st.guardDb = guardRoom + rel[b] * (st.guardDb - guardRoom);
                    const float room = std::min (std::max (0.0f, (float)tb[b] - lev), st.guardDb);
                    if (bDb > 0.0f)
                        bDb = std::min (bDb, room);
                    if (softened)
                    {
                        // Soften rounds the gain changes off (and the cap above still holds)
                        aDb = st.aboveDb + (aDb - st.aboveDb) * gainSmooth;
                        bDb = st.belowDb + (bDb - st.belowDb) * gainSmooth;
                        if (bDb > 0.0f)
                            bDb = std::min (bDb, room);
                    }
                    st.aboveDb = aDb;
                    st.belowDb = bDb;
                    const float totalDb = std::clamp (aDb + bDb, kMaxCutDb, kMaxBoostDb);
                    g = dbToGain (totalDb);
                    if (softened)
                    {
                        // the lifted part (what upward compression adds on top of the rest) is
                        // low-passed, in proportion to Soften
                        const float gBase = dbToGain (totalDb - std::max (0.0f, bDb));
                        const float gLift = g - gBase;
                        float* ch[2] = {&wl, &wr};
                        for (int c = 0; c < 2; ++c)
                        {
                            const float lift = *ch[c] * gLift;
                            float* lp = st.liftLp[c];
                            lp[0] += (lift - lp[0]) * liftA;
                            lp[1] += (lp[0] - lp[1]) * liftA;
                            *ch[c] = *ch[c] * gBase + lift + (lp[1] * liftKeep - lift) * liftBlend;
                        }
                        g = 1.0f; // applied above
                    }
                    else
                        st.liftLp[0][0] = st.liftLp[0][1] = st.liftLp[1][0] = st.liftLp[1][1] = 0.0f;
                }
                else
                {
                    st.aboveDb *= rel[b];
                    st.belowDb *= rel[b];
                    g = dbToGain (st.aboveDb + st.belowDb);
                }
                yl = wl * g * st.outGain;
                yr = wr * g * st.outGain;
                st.meterIn = std::max (std::max (std::fabs (xl), std::fabs (xr)), st.meterIn * meterFall);
                st.meterOut = std::max (std::max (std::fabs (yl), std::fabs (yr)), st.meterOut * meterFall);
            }
            if (b == kSubBand)
            {
                // heard as far as it has faded in
                yl *= subMix;
                yr *= subMix;
            }
            if (audible[b])
            {
                sumL += yl;
                sumR += yr;
            }
        }
        if (listen)
        {
            outL[i] = scL[i] * scGain;
            outR[i] = scR[i] * scGain;
        }
        else
        {
            outL[i] = sumL * outGain;
            outR[i] = sumR * outGain;
        }
    }

    // Soften's Color after the Output gain, then the built-in saturator (neither on the side-chain
    // listen signal)
    if (!listen)
        processColor (outL, outR, n);
    if (!listen && hasTail)
        sat.process (outL, outR, n);

    used[kSubBand] = subState != SubState::Off;
    for (int b = 0; b < kAll; ++b)
    {
        meters[b].inputDb = gainToDb (bands[b].meterIn);
        meters[b].outputDb = gainToDb (bands[b].meterOut);
        meters[b].gainDb = active[b] ? bands[b].aboveDb + bands[b].belowDb + gainToDb (bands[b].limGain) : 0.0f;
        if (!used[b])
            meters[b] = BandMeter {};
    }
}

} // namespace multidyn

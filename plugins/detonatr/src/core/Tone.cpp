#include "Tone.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define DETONATR_TONE_SSE 1
#endif

namespace detonatr {

namespace {

// Flushes denormals to zero while alive: the resonators, the band filters and the all-passes all
// decay towards them after a hit, and they are slow on x86.
class NoDenormals
{
public:
    NoDenormals ()
    {
#if defined(DETONATR_TONE_SSE)
        old = _mm_getcsr ();
        _mm_setcsr (old | 0x8040); // FTZ | DAZ
#endif
    }
    ~NoDenormals ()
    {
#if defined(DETONATR_TONE_SSE)
        _mm_setcsr (old);
#endif
    }
    NoDenormals (const NoDenormals&) = delete;
    NoDenormals& operator= (const NoDenormals&) = delete;

private:
    unsigned int old = 0;
};

constexpr double kLn1000 = 6.907755278982137; // a T60 is a fall of 1000 times in amplitude

// A material: its modes' frequency ratios to Root, their relative gains and relative decays (times
// Decay). The lowest mode rings longest, so Decay is the fundamental's T60.
struct MaterialDef
{
    int count;
    double ratio[12], gain[12], decay[12];
};

const MaterialDef kMaterials[kNumMaterials] = {
    // glass: a wine glass's bending modes, n (n^2 - 1) / sqrt (n^2 + 1) for n = 2 .. 7; the lower ones
    // are close pairs (the glass is never perfectly round), which beat slowly
    {9,
     {1.0, 1.0035, 2.828, 2.841, 5.424, 5.45, 8.771, 12.87, 17.71},
     {1.0, 0.75, 0.5, 0.35, 0.3, 0.2, 0.22, 0.16, 0.12},
     {1.0, 0.97, 0.8, 0.78, 0.62, 0.6, 0.47, 0.36, 0.28}},
    // metal pot: shell modes in close pairs that clang, a medium ring
    {12,
     {1.0, 1.52, 2.03, 2.11, 3.26, 3.41, 5.17, 5.34, 8.33, 11.9, 12.4, 17.1},
     {1.0, 0.5, 0.45, 0.4, 0.35, 0.32, 0.26, 0.24, 0.18, 0.15, 0.14, 0.1},
     {1.0, 0.72, 0.76, 0.7, 0.56, 0.52, 0.46, 0.43, 0.36, 0.3, 0.28, 0.22}},
    // pipe: an air column, near-harmonic and slightly stretched
    {10,
     {1.0, 2.004, 3.012, 4.025, 5.04, 6.06, 7.09, 8.12, 9.16, 10.2},
     {1.0, 0.62, 0.46, 0.38, 0.32, 0.27, 0.24, 0.21, 0.19, 0.17},
     {1.0, 0.9, 0.8, 0.72, 0.64, 0.57, 0.5, 0.45, 0.4, 0.36}},
    // wood: a block; few modes, the upper ones weak and all of them short
    {6,
     {1.0, 1.58, 2.76, 3.9, 5.4, 7.1},
     {1.0, 0.45, 0.3, 0.15, 0.08, 0.05},
     {0.35, 0.25, 0.17, 0.12, 0.09, 0.07}},
    // bell: hum, prime, tierce (the minor third that makes a bell), quint, nominal and the upper
    // partials of an old, not quite tuned bell
    {10,
     {1.0, 1.94, 2.37, 3.03, 4.0, 5.24, 5.41, 6.72, 8.15, 10.9},
     {1.0, 0.8, 0.6, 0.35, 0.55, 0.3, 0.28, 0.22, 0.15, 0.1},
     {1.0, 0.9, 0.7, 0.6, 0.5, 0.36, 0.34, 0.28, 0.22, 0.17}},
    // bottle: a pure Helmholtz note and the walls' glassy clink far above it
    {7,
     {1.0, 6.8, 7.02, 11.3, 16.2, 16.5, 21.5},
     {1.0, 0.22, 0.2, 0.15, 0.12, 0.1, 0.08},
     {1.0, 0.35, 0.33, 0.25, 0.2, 0.19, 0.15}},
};

// The resonators ring, after a hit, at kResGain * (the hit's spectrum at the mode, in amplitude
// seconds) times the mode's normalised gain: in continuous-time terms, so it does not change with the
// sample rate. It is set so a 20 ms burst of white noise comes out at about the input's peak level.
constexpr double kResGain = 3000.0;
// A hit whose energy sits right on a mode (a kick on the root) would ring far louder than that, so
// the resonators' peak is held to this many times the input's (both followers fall at Decay's rate,
// so a held ring keeps its shape). +3 dB: most real impacts (dull, low-heavy) end up here.
constexpr float kCeiling = 1.41f;

constexpr double kBandLo = 40.0, kBandHi = 12000.0, kBandQ = 3.0;
// the vocoded output's level: a noise carrier comes out a few dB under the input, a tonal one (whose
// neighbouring bands add up in phase) a few dB over it
constexpr float kVocGain = 2.0f;
// a carrier band more than 30 dB below its loudest is not raised to full (it would only be noise)
constexpr float kFloorRel = 0.0316f;

inline void glideLog (double& now, double target, double c)
{
    now *= std::pow (target / now, c);
    if (std::abs (now / target - 1.0) < 1e-6)
        now = target;
}

inline void glide (float& now, float target, float c)
{
    now += (target - now) * c;
    if (std::abs (target - now) < 1e-4f)
        now = target;
}

// One sample through a bank of band-passes (two cascaded RBJ band-passes per band, b1 = 0,
// b2 = -b0), all bands at once so the loop vectorises.
inline void bankStep (float* s1, float* s2, float* t1, float* t2, const float* b0, const float* a1,
                      const float* a2, int nb, float x, float* y)
{
    for (int b = 0; b < nb; ++b)
    {
        const float u = b0[b] * x + s1[b];
        s1[b] = s2[b] - a1[b] * u;
        s2[b] = -b0[b] * x - a2[b] * u;
        const float v = b0[b] * u + t1[b];
        t1[b] = t2[b] - a1[b] * v;
        t2[b] = -b0[b] * u - a2[b] * v;
        y[b] = v;
    }
}

// 4-point Hermite read of a looping recording at a fractional position in [0, frames)
inline float hermite (const float* d, int frames, double pos)
{
    const int i = (int)pos;
    const float t = (float)(pos - i);
    const int i0 = i == 0 ? frames - 1 : i - 1;
    int i2 = i + 1;
    if (i2 >= frames)
        i2 -= frames;
    int i3 = i2 + 1;
    if (i3 >= frames)
        i3 -= frames;
    const float y0 = d[i0], y1 = d[i], y2 = d[i2], y3 = d[i3];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

// the frames a carrier can really play (0: treat it as empty)
inline int playableFrames (const Carrier* c)
{
    if (c == nullptr || !(c->sampleRate > 0.0))
        return 0;
    return std::max (0, std::min (c->frames, (int)c->ch[0].size ()));
}

} // namespace

void Tone::Bank::clear ()
{
    for (int b = 0; b < kBands; ++b)
        s1[b] = s2[b] = t1[b] = t2[b] = env[b] = 0.0f;
}

void Tone::prepare (double sampleRate, int mb)
{
    sr = std::clamp (sampleRate, 8000.0, 768000.0);
    maxBlock = std::max (1, mb);
    slowCoef = 1.0 - std::exp (-kSub / (0.03 * sr));
    levelCoef = (float)(1.0 - std::exp (-1.0 / (0.01 * sr)));
    fadeStep = (float)(1.0 / (0.01 * sr));
    ceilRise = (float)(1.0 - std::exp (-1.0 / (0.03 * sr)));
    apFadeLen = std::max (1, (int)std::lround (0.015 * sr));

    // The vocoder's bands. The envelopes follow a band within about half a cycle (but no faster than
    // 0.5 ms, so an impact's front comes through) and fall within four cycles (15 .. 80 ms): slow
    // enough not to ripple at the band's own frequency, fast enough to keep the gaps between hits.
    bandCount = 0;
    for (int b = 0; b < kBands; ++b)
    {
        const double f = kBandLo * std::pow (kBandHi / kBandLo, b / (double)(kBands - 1));
        if (f > 0.45 * sr)
            break;
        const double w = 2.0 * M_PI * f / sr, alpha = std::sin (w) / (2.0 * kBandQ), a0 = 1.0 + alpha;
        bandB0[b] = (float)(alpha / a0);
        bandA1[b] = (float)(-2.0 * std::cos (w) / a0);
        bandA2[b] = (float)((1.0 - alpha) / a0);
        const double atk = std::clamp (0.5 / f, 0.0005, 0.008), rel = std::clamp (4.0 / f, 0.015, 0.08);
        envAtk[b] = (float)(1.0 - std::exp (-1.0 / (atk * sr)));
        envRel[b] = (float)(1.0 - std::exp (-1.0 / (rel * sr)));
        carRel[b] = (float)std::exp (-1.0 / (rel * sr));
        ++bandCount;
    }
    reset ();
}

void Tone::reset ()
{
    for (int c = 0; c < 2; ++c)
    {
        for (int k = 0; k < kMaxModes; ++k)
            zRe[c][k] = zIm[c][k] = 0.0;
        prevX1[c] = prevX2[c] = 0.0;
        inBank[c].clear ();
        for (int k = 0; k < kMaxStages; ++k)
            apS1[c][k] = apS2[c][k] = 0.0;
    }
    peakIn = peakRes = 0.0f;
    ceilGain = 1.0f;
    resRunning = inRunning = false;
    for (auto& s : slots)
    {
        s.pos = 0.0;
        s.running = false;
        s.fade = 0.0f;
        s.floorLevel[0] = s.floorLevel[1] = 0.0f;
        s.bank[0].clear ();
        s.bank[1].clear ();
    }
    apFrom = apTo = 0;
    apFadePos = apFadeLen;
    modesMaterial = -1;
    snap = true;
}

void Tone::setRoot (double hz) { rootHz = std::clamp (hz, 30.0, 400.0); }
void Tone::setMaterial (int m) { material = std::clamp (m, 0, kNumMaterials - 1); }
void Tone::setDecay (double s) { decaySec = std::clamp (s, 0.05, 4.0); }
void Tone::setResonators (double v) { resLevel = (float)std::clamp (v, 0.0, 1.0); }
void Tone::setCarriers (double v) { carLevel = (float)std::clamp (v, 0.0, 1.0); }
void Tone::setDry (double v) { dryLevel = (float)std::clamp (v, 0.0, 1.0); }
void Tone::setDisperse (double v) { dispAmount = std::clamp (v, 0.0, 1.0); }
void Tone::setDisperseFreq (double hz) { dispHz = std::clamp (hz, 50.0, 5000.0); }

void Tone::setCarrierLevel (int slot, double v)
{
    if (slot >= 0 && slot < kCarrierSlots)
        slots[slot].level = (float)std::clamp (v, 0.0, 1.0);
}

void Tone::setCarrier (int slot, const Carrier* c)
{
    if (slot < 0 || slot >= kCarrierSlots || slots[slot].carrier == c)
        return;
    slots[slot].carrier = c;
    slots[slot].pos = 0.0; // a new recording starts from its beginning
}

void Tone::process (float* L, float* R, int n)
{
    if (n <= 0)
        return;
    NoDenormals noDenormals;
    if (snap)
    {
        updateSlow (true);
        resNow = resLevel;
        carNow = carLevel;
        dryNow = dryLevel;
        for (auto& s : slots)
            s.levelNow = s.level;
        snap = false;
    }
    for (int off = 0; off < n; off += kSub)
        processSub (L + off, R + off, std::min (kSub, n - off));
}

void Tone::updateSlow (bool jump)
{
    const double c = jump ? 1.0 : slowCoef;
    glideLog (rootNow, rootHz, c);
    glideLog (decayNow, decaySec, c);
    glideLog (dispHzNow, std::min (dispHz, 0.45 * sr), c);
    // more dispersion is more stages, each a little sharper (Q 0.6 .. 2): a longer, more pitched chirp
    const double qTarget = 0.6 + 1.4 * dispAmount;
    dispQNow += (qTarget - dispQNow) * c;
    if (std::abs (qTarget - dispQNow) < 1e-6)
        dispQNow = qTarget;

    if (material != modesMaterial || rootNow != modesRoot || decayNow != modesDecay)
        updateModes ();
    peakFall = (float)std::exp (-kLn1000 / (decayNow * sr));

    const double w = 2.0 * M_PI * dispHzNow / sr, alpha = std::sin (w) / (2.0 * dispQNow);
    apA1 = -2.0 * std::cos (w) / (1.0 + alpha);
    apA2 = (1.0 - alpha) / (1.0 + alpha);

    // The stage count moves in steps, so a change crossfades (15 ms) from the old count's output to
    // the new one's; the stages it newly takes in start from silence.
    const int stages = dispAmount > 0.0 ? std::max (1, (int)std::lround (dispAmount * kMaxStages)) : 0;
    if (jump)
    {
        apFrom = apTo = stages;
        apFadePos = apFadeLen;
    }
    else if (apFadePos >= apFadeLen && stages != apTo)
    {
        apFrom = apTo;
        apTo = stages;
        apFadePos = 0;
        for (int c2 = 0; c2 < 2; ++c2)
            for (int k = apFrom; k < apTo; ++k)
                apS1[c2][k] = apS2[c2][k] = 0.0;
    }
}

void Tone::updateModes ()
{
    const MaterialDef& m = kMaterials[material];
    if (material != modesMaterial)
    {
        // modes the last material did not use start from silence
        for (int c = 0; c < 2; ++c)
            for (int k = modesMaterial < 0 ? 0 : modeCount; k < kMaxModes; ++k)
                zRe[c][k] = zIm[c][k] = 0.0;
    }
    double sumSq = 0.0;
    for (int k = 0; k < m.count; ++k)
        sumSq += m.gain[k] * m.gain[k];
    const double norm = 1.0 / std::sqrt (sumSq);
    const double top = 0.45 * sr;
    for (int k = 0; k < m.count; ++k)
    {
        // A mode is a complex one-pole (a decaying phasor) fed by x[n] - x[n-2], which has no DC and
        // no Nyquist; its ring after an impulse is modeGain * 2 sin w, which the gain undoes. A mode
        // above the top (only at high roots and low rates) keeps ringing but takes no new input.
        const double f = rootNow * m.ratio[k];
        const double w = 2.0 * M_PI * std::min (f, top) / sr;
        const double r = std::exp (-kLn1000 / (decayNow * m.decay[k] * sr));
        poleRe[k] = r * std::cos (w);
        poleIm[k] = r * std::sin (w);
        modeGain[k] = f > top ? 0.0 : kResGain * m.gain[k] * norm / sr / (2.0 * std::sin (w));
    }
    modeCount = m.count;
    modesMaterial = material;
    modesRoot = rootNow;
    modesDecay = decayNow;
}

void Tone::processSub (float* L, float* R, int n)
{
    updateSlow (false);

    float res[2][kSub], voc[2][kSub];
    const bool resOn = resNow > 0.0f || resLevel > 0.0f;
    if (resOn && !resRunning)
    {
        // it was skipped while silent: start from rest
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < kMaxModes; ++k)
                zRe[c][k] = zIm[c][k] = 0.0;
            prevX1[c] = prevX2[c] = 0.0;
        }
        peakIn = peakRes = 0.0f;
        ceilGain = 1.0f;
    }
    resRunning = resOn;
    if (resOn)
        runResonators (L, R, res[0], res[1], n);
    const bool vocOn = runVocoder (L, R, voc[0], voc[1], n);

    for (int i = 0; i < n; ++i)
    {
        glide (dryNow, dryLevel, levelCoef);
        glide (resNow, resLevel, levelCoef);
        glide (carNow, carLevel, levelCoef);
        float l = dryNow * L[i], r = dryNow * R[i];
        if (resOn)
        {
            l += resNow * res[0][i];
            r += resNow * res[1][i];
        }
        if (vocOn)
        {
            l += carNow * voc[0][i];
            r += carNow * voc[1][i];
        }
        L[i] = l;
        R[i] = r;
    }
    runDisperser (L, R, n);
}

void Tone::runResonators (const float* L, const float* R, float* outL, float* outR, int n)
{
    const float* in[2] = {L, R};
    float* out[2] = {outL, outR};
    const int nm = modeCount;
    for (int c = 0; c < 2; ++c)
    {
        double* zr = zRe[c];
        double* zi = zIm[c];
        double x1 = prevX1[c], x2 = prevX2[c];
        for (int i = 0; i < n; ++i)
        {
            const double x = in[c][i], d = x - x2;
            x2 = x1;
            x1 = x;
            double sum = 0.0;
            for (int k = 0; k < nm; ++k)
            {
                const double re = poleRe[k] * zr[k] - poleIm[k] * zi[k] + modeGain[k] * d;
                const double im = poleRe[k] * zi[k] + poleIm[k] * zr[k];
                zr[k] = re;
                zi[k] = im;
                sum += im;
            }
            out[c][i] = (float)sum;
        }
        prevX1[c] = x1;
        prevX2[c] = x2;
        // below any audible level: let it go to zero (the denormal guard is x86 only)
        for (int k = 0; k < nm; ++k)
            if (std::abs (zr[k]) + std::abs (zi[k]) < 1e-15)
                zr[k] = zi[k] = 0.0;
    }

    // The ceiling: the resonators' peak (linked across channels) stays within kCeiling of the input's.
    // Both peaks fall at the fundamental's rate, so once a ring is held its gain stays put.
    for (int i = 0; i < n; ++i)
    {
        const float a = std::max (std::abs (L[i]), std::abs (R[i]));
        const float o = std::max (std::abs (outL[i]), std::abs (outR[i]));
        peakIn = std::max (a, peakIn * peakFall);
        peakRes = std::max (o, peakRes * peakFall);
        const float lim = kCeiling * peakIn;
        const float target = peakRes > lim ? lim / peakRes : 1.0f;
        if (target < ceilGain)
            ceilGain = target;
        else
            ceilGain += (target - ceilGain) * ceilRise;
        outL[i] *= ceilGain;
        outR[i] *= ceilGain;
    }
}

bool Tone::runVocoder (const float* L, const float* R, float* outL, float* outR, int n)
{
    const bool heardAtAll = carNow > 0.0f || carLevel > 0.0f;
    int frames[kCarrierSlots];
    bool any = false;
    for (int s = 0; s < kCarrierSlots; ++s)
    {
        Slot& sl = slots[s];
        frames[s] = playableFrames (sl.carrier);
        const bool heard = heardAtAll && (sl.levelNow > 0.0f || sl.level > 0.0f);
        if (!heard)
        {
            sl.running = false;
            sl.fade = 0.0f;
        }
        else if (!sl.running && frames[s] > 0)
        {
            // starts from rest and fades in (a band-pass that has just started would be normalised
            // up to full at once)
            sl.bank[0].clear ();
            sl.bank[1].clear ();
            sl.floorLevel[0] = sl.floorLevel[1] = 0.0f;
            sl.fade = 0.0f;
            sl.running = true;
        }
        any = any || sl.running;
    }

    // the recordings play on (loop) whether they are heard or not
    float carBuf[kCarrierSlots][2][kSub];
    bool stereo[kCarrierSlots] = {};
    for (int s = 0; s < kCarrierSlots; ++s)
    {
        Slot& sl = slots[s];
        const int nf = frames[s];
        if (nf <= 0)
        {
            sl.pos = 0.0;
            if (sl.running)
                for (int i = 0; i < n; ++i)
                    carBuf[s][0][i] = carBuf[s][1][i] = 0.0f;
            continue;
        }
        const Carrier& c = *sl.carrier;
        const double inc = c.sampleRate / sr;
        if (sl.pos >= nf || sl.pos < 0.0)
            sl.pos = std::fmod (std::max (0.0, sl.pos), (double)nf);
        if (!sl.running)
        {
            sl.pos = std::fmod (sl.pos + inc * n, (double)nf);
            continue;
        }
        stereo[s] = (int)c.ch[1].size () >= nf;
        const float* d0 = c.ch[0].data ();
        const float* d1 = stereo[s] ? c.ch[1].data () : nullptr;
        double pos = sl.pos;
        for (int i = 0; i < n; ++i)
        {
            carBuf[s][0][i] = hermite (d0, nf, pos);
            if (d1 != nullptr)
                carBuf[s][1][i] = hermite (d1, nf, pos);
            pos += inc;
            if (pos >= nf)
                pos = std::fmod (pos, (double)nf);
        }
        sl.pos = pos;
    }

    if (!any)
    {
        inRunning = false;
        return false;
    }
    if (!inRunning)
    {
        inBank[0].clear ();
        inBank[1].clear ();
        inRunning = true;
    }

    const int nb = bandCount;
    const float* in[2] = {L, R};
    float y[kBands], norm[kBands];
    for (int i = 0; i < n; ++i)
    {
        // the input's band envelopes
        for (int c = 0; c < 2; ++c)
        {
            Bank& k = inBank[c];
            bankStep (k.s1, k.s2, k.t1, k.t2, bandB0, bandA1, bandA2, nb, in[c][i], y);
            for (int b = 0; b < nb; ++b)
            {
                const float a = std::abs (y[b]);
                k.env[b] += (a > k.env[b] ? envAtk[b] : envRel[b]) * (a - k.env[b]);
            }
        }
        float accL = 0.0f, accR = 0.0f;
        for (int s = 0; s < kCarrierSlots; ++s)
        {
            Slot& sl = slots[s];
            if (!sl.running)
                continue;
            glide (sl.levelNow, sl.level, levelCoef);
            sl.fade = frames[s] > 0 ? std::min (1.0f, sl.fade + fadeStep) : std::max (0.0f, sl.fade - fadeStep);
            const float g = kVocGain * sl.levelNow * sl.fade;
            // Each carrier band, divided by its own envelope (which never falls below the band, so
            // the ratio stays within 1), carries the input's envelope for that band.
            for (int c = 0; c < (stereo[s] ? 2 : 1); ++c)
            {
                Bank& k = sl.bank[c];
                bankStep (k.s1, k.s2, k.t1, k.t2, bandB0, bandA1, bandA2, nb, carBuf[s][c][i], y);
                const float fl = std::max (1e-7f, sl.floorLevel[c]);
                for (int b = 0; b < nb; ++b)
                {
                    k.env[b] = std::max (std::abs (y[b]), k.env[b] * carRel[b]);
                    norm[b] = y[b] / std::max (k.env[b], fl);
                }
                if (stereo[s])
                {
                    float sum = 0.0f;
                    for (int b = 0; b < nb; ++b)
                        sum += norm[b] * inBank[c].env[b];
                    (c == 0 ? accL : accR) += g * sum;
                }
                else
                {
                    float sumL = 0.0f, sumR = 0.0f;
                    for (int b = 0; b < nb; ++b)
                    {
                        sumL += norm[b] * inBank[0].env[b];
                        sumR += norm[b] * inBank[1].env[b];
                    }
                    accL += g * sumL;
                    accR += g * sumR;
                }
            }
        }
        outL[i] = accL;
        outR[i] = accR;
    }

    for (auto& sl : slots)
    {
        if (!sl.running)
            continue;
        if (playableFrames (sl.carrier) <= 0 && sl.fade <= 0.0f)
        {
            sl.running = false; // faded out after its recording was removed
            continue;
        }
        for (int c = 0; c < 2; ++c)
        {
            float top = 0.0f;
            for (int b = 0; b < nb; ++b)
                top = std::max (top, sl.bank[c].env[b]);
            sl.floorLevel[c] = kFloorRel * top;
        }
    }
    return true;
}

void Tone::runDisperser (float* L, float* R, int n)
{
    if (apFrom == 0 && apTo == 0)
        return; // Disperse 0: untouched
    const int stages = std::max (apFrom, apTo);
    float* io[2] = {L, R};
    const double a1 = apA1, a2 = apA2;
    for (int i = 0; i < n; ++i)
    {
        const bool fading = apFadePos < apFadeLen;
        const double t = fading ? (apFadePos + 1.0) / apFadeLen : 1.0;
        for (int c = 0; c < 2; ++c)
        {
            double* s1 = apS1[c];
            double* s2 = apS2[c];
            const double x = io[c][i];
            double y = x, yFrom = x, yTo = x;
            for (int k = 0; k < stages; ++k)
            {
                // an all-pass biquad in transposed direct form II: b0 = a2, b1 = a1, b2 = 1
                const double o = a2 * y + s1[k];
                s1[k] = a1 * (y - o) + s2[k];
                s2[k] = y - a2 * o;
                y = o;
                if (k + 1 == apFrom)
                    yFrom = y;
                if (k + 1 == apTo)
                    yTo = y;
            }
            io[c][i] = (float)(yFrom + (yTo - yFrom) * t);
        }
        if (fading && ++apFadePos >= apFadeLen)
            apFrom = apTo;
    }
    // below any audible level: let the stages go to zero
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < stages; ++k)
            if (std::abs (apS1[c][k]) + std::abs (apS2[c][k]) < 1e-20)
                apS1[c][k] = apS2[c][k] = 0.0;
}

} // namespace detonatr

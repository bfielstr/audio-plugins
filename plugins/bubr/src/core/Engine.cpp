#include "Engine.h"

#include "BandShape.h"
#include "WetShape.h"

#include "pluginkit/NoDenormals.h"

#include <algorithm>
#include <cmath>

namespace bubr {

namespace dsp = sweepr::dsp;

namespace {
inline double coefFor (double sec, double sr) { return 1.0 - std::exp (-1.0 / (std::max (1e-6, sec) * sr)); }
// glides x towards t by c, landing on it once close (so a still setting is exactly its value)
inline void glide (double& x, double t, double c, double eps = 1e-9)
{
    x += (t - x) * c;
    if (std::fabs (t - x) < eps)
        x = t;
}
inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
// an 11-point table over 0 .. 1 (steps of 0.1), linear between its points
inline double table11 (const double* t, double x)
{
    const double pos = std::clamp (x, 0.0, 1.0) * 10.0;
    const int i = std::min (9, (int)pos);
    return t[i] + (t[i + 1] - t[i]) * (pos - i);
}
// a band-pass section (the SVF's, normalized: 0 dB at its centre) at hz, as the filter at rate sr has it
inline std::complex<double> bandPass (double fc, double q, double hz, double sr)
{
    const double w = 2.0 * dsp::kPi * fc / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    const std::complex<double> z1 = std::polar (1.0, -2.0 * dsp::kPi * hz / sr);
    return (a - a * z1 * z1) / ((1.0 + a) - 2.0 * c * z1 + (1.0 - a) * z1 * z1);
}
inline std::complex<double> highPass (double fc, double q, double hz, double sr)
{
    const double w = 2.0 * dsp::kPi * fc / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
    const std::complex<double> z1 = std::polar (1.0, -2.0 * dsp::kPi * hz / sr);
    return 0.5 * (1.0 + c) * (1.0 - 2.0 * z1 + z1 * z1) / ((1.0 + a) - 2.0 * c * z1 + (1.0 - a) * z1 * z1);
}
// a first-order low shelf (gain g below fc, unity above), bilinear: y = b0 x + b1 x1 - a1 y1
struct Shelf
{
    double b0 = 1.0, b1 = 0.0, a1 = 0.0;
};
inline Shelf shelfAt (double fc, double g, double sr)
{
    const double wc = std::tan (dsp::kPi * std::min (fc, 0.45 * sr) / sr);
    return {(1.0 + g * wc) / (1.0 + wc), (g * wc - 1.0) / (1.0 + wc), (wc - 1.0) / (1.0 + wc)};
}
// the clipper's curve: a knee of order kKnee, its bias taken out at rest
inline double clipCurve (double v)
{
    const double b = v + shape::kBias;
    return b / std::pow (1.0 + std::pow (std::fabs (b), shape::kKnee), 1.0 / shape::kKnee) - shape::kBiasOut;
}
inline float softClip (float x)
{
    const float a = std::fabs (x);
    if (a <= 0.5f)
        return x;
    const float s = x < 0.0f ? -1.0f : 1.0f;
    if (a >= 1.5f)
        return s;
    const float u = a - 0.5f;
    return s * (0.5f + u - 0.5f * u * u);
}
inline float hardClip (float x) { return std::clamp (x, -1.0f, 1.0f); }
// Dry/Wet (-1 .. 1) as the reference's 0 .. 100 and the mix's parts: the effect's gain, the dry's and how much of
// the sum goes through Low Cut (and its Q)
struct MixLaw
{
    double wet, dry, k, q;
};
inline MixLaw mixLaw (double dryWet)
{
    const double m = std::clamp (0.5 * (dryWet + 1.0), 0.0, 1.0) * 100.0;
    MixLaw l;
    l.wet = m < 50.0 ? table11 (shape::kWetMix, m / 50.0) : 1.0;
    l.dry = m > 50.0 ? table11 (shape::kDryMix, (m - 50.0) / 50.0) : 1.0;
    l.k = std::min (1.0, m / 50.0);
    l.q = shape::kLowCutQ0 + (shape::kLowCutQ - shape::kLowCutQ0) * l.k;
    return l;
}
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

Band bandAt (double freq, double qFactor)
{
    using namespace shape;
    const double lf = std::log (std::clamp (freq, kGridFreq[0], kGridFreq[kFreqs - 1]));
    int i0 = 0;
    while (i0 < kFreqs - 2 && lf > std::log (kGridFreq[i0 + 1]))
        ++i0;
    const double fx = std::clamp ((lf - std::log (kGridFreq[i0])) / (std::log (kGridFreq[i0 + 1]) - std::log (kGridFreq[i0])), 0.0, 1.0);
    const double qp = std::clamp (qFactor, 0.0, 1.0) * (kQs - 1);
    const int j0 = std::min (kQs - 2, (int)qp);
    const double fy = qp - j0;
    // log (f / grid Freq), log q and log |gain|, bilinearly between the four neighbours
    double acc[9] = {};
    for (int di = 0; di < 2; ++di)
        for (int dj = 0; dj < 2; ++dj)
        {
            const double w = (di ? fx : 1.0 - fx) * (dj ? fy : 1.0 - fy);
            const int a = i0 + di, b = j0 + dj;
            for (int k = 0; k < 4; ++k)
            {
                acc[2 * k] += w * std::log ((double)kSecRatio[a][b][k]);
                acc[2 * k + 1] += w * std::log ((double)kSecQ[a][b][k]);
            }
            acc[8] += w * (double)kGainLn[a][b];
        }
    Band out;
    for (int k = 0; k < 4; ++k)
    {
        out.f[k] = freq * std::exp (acc[2 * k]);
        out.q[k] = std::exp (acc[2 * k + 1]);
    }
    out.gain = -std::exp (acc[8]); // (the band inverts)
    return out;
}

namespace {
// the post-clipper gain for a band: its last two sections' level at Freq set to the ceiling's
double postNorm (const Band& b, double freq, double sr)
{
    return shape::kCeilNorm / std::abs (bandPass (b.f[0], b.q[0], freq, sr) * bandPass (b.f[3], b.q[3], freq, sr));
}
// Intensity and Boost's part into the clipper, Boost's part after it (dB), Drive's gain (dB)
void gainsDb (const ParamArray& p, double intensity, double boost, double drive, bool x2, double& pre, double& post, double& drv)
{
    const double bd = 100.0 * (boost - 0.55); // (percent from the default)
    pre = table11 (shape::kIntensityDb, intensity) + shape::kBoostPre * bd;
    post = (0.3 - shape::kBoostPre) * bd;
    drv = table11 (x2 ? shape::kDriveX2Db : shape::kDriveDb, drive);
    (void)p;
}
} // namespace

std::complex<double> wetResponse (const ParamArray& p, double hz, double sampleRate)
{
    const Band b = bandAt (p[kFreq], p[kQFactor]);
    double pre, post, drv;
    gainsDb (p, p[kIntensity], p[kBoost], p[kDrive], p[kDriveX2] >= 0.5, pre, post, drv);
    std::complex<double> h = b.gain * dbToGain (pre + post + drv);
    for (int k = 0; k < 4; ++k)
        h *= bandPass (b.f[k], b.q[k], hz, sampleRate);
    const double lc = std::clamp (p[kLowCut], 20.0, 125.0);
    const std::complex<double> hp = highPass (lc, shape::kLowCutQ, hz, sampleRate);
    return h * hp * hp;
}

Engine::Engine () {}

int Engine::latency () const
{
    const int os = osFactor == 4 ? (int)os4[0].latency () : (osFactor == 2 ? os2[0].latency () : 0);
    return os + look;
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    (void)maxBlock;
    const double pass = std::min (20000.0, 0.46 * sr);
    for (int c = 0; c < 2; ++c)
    {
        os2[c].design (pass / sr, 80.0, 257);
        os4[c].prepare (sr, kChunk);
        upBuf[c].assign ((size_t)(kChunk * kMaxOs), 0.0f);
    }
    cSmooth = coefFor (0.02, sr);
    compAtk = coefFor (shape::kCompAttackMs * 1e-3, sr);
    compRel = coefFor (shape::kCompReleaseMs * 1e-3, sr);
    limAf = std::exp (-1.0 / (shape::kLimFastMs * 1e-3 * sr));
    limAa = coefFor (shape::kLimSlowAttackMs * 1e-3, sr);
    limAr = std::exp (-1.0 / (shape::kLimSlowReleaseMs * 1e-3 * sr));
    limHoldN = (int)std::lround (shape::kLimHoldMs * 1e-3 * sr);
    const int lookMax = (int)std::lround (shape::kLookaheadMs * 1e-3 * sr);
    const int dryMax = std::max (os4[0].latency (), os2[0].latency ()) + 1;
    for (int c = 0; c < 2; ++c)
    {
        dryDelay[c].assign ((size_t)dryMax, 0.0f);
        lookBuf[c].assign ((size_t)lookMax + 1, 0.0f);
        grBuf[c].assign ((size_t)lookMax + 1, 0.0);
    }
    for (uint32_t id = 0; id < kNumParams; ++id)
        setParam (id, p[id]);
    reset ();
}

void Engine::reset ()
{
    for (int c = 0; c < 2; ++c)
    {
        os2[c].reset ();
        os4[c].reset ();
        for (auto& s : band)
            s[c].reset ();
        for (auto& s : lc[c])
            s.reset ();
        tZ[c] = tY[c] = 0.0;
        fbL[c] = fbH[c] = 0.0;
        gateG[c] = 1.0;
        limFast[c] = limSlow[c] = 0.0;
        limHold[c] = 0;
        std::fill (dryDelay[c].begin (), dryDelay[c].end (), 0.0f);
        std::fill (lookBuf[c].begin (), lookBuf[c].end (), 0.0f);
        std::fill (grBuf[c].begin (), grBuf[c].end (), 0.0);
        grSum[c] = 0.0;
    }
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < 2; ++k)
            eX[c][k] = eY[c][k] = iX[c][k] = iY[c][k] = 0.0;
    dryPos = lookPos = 0;
    compEnv = 0.0;
    compGrNow = 1.0;
    limGrNow = 0.0;
    freq = p[kFreq];
    qf = p[kQFactor];
    intensity = p[kIntensity];
    boost = p[kBoost];
    drive = p[kDrive];
    x2 = p[kDriveX2] >= 0.5 ? 1.0 : 0.0;
    lowCut = p[kLowCut];
    dryWet = p[kDryWet];
    outGain = p[kOutput] <= -59.99 ? 0.0 : dbToGain (p[kOutput]);
    shownFreq = shownQ = shownLowCut = -1.0;
    shownMix = -2.0;
    osNow = -1;
    controlChunk ();
}

void Engine::setParam (uint32_t id, double plain)
{
    if (id >= kNumParams)
        return;
    p[id] = plain;
    // (read where they are used)
}

// The band's sections for Freq and Q-Factor now (only when they moved).
void Engine::bandCoefs ()
{
    if (std::fabs (freq - shownFreq) < 1e-6 && std::fabs (qf - shownQ) < 1e-7)
        return;
    shownFreq = freq;
    shownQ = qf;
    const Band b = bandAt (freq, qf);
    for (int k = 0; k < 4; ++k)
    {
        bandC[k].k = 1.0 / std::max (0.05, b.q[k]);
        bandC[k].set (std::tan (dsp::kPi * std::min (b.f[k], 0.45 * sr) / sr));
    }
    bandGain = b.gain;
    bandPostNorm = postNorm (b, freq, sr);
    // the tilt's polynomials: bilinear, zeros and poles at their ratios of Freq (each normalized)
    auto poly = [this] (double f, double q, double* out) {
        const double w = 2.0 * dsp::kPi * std::min (f, 0.45 * sr) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
        out[0] = 1.0;
        out[1] = -2.0 * c / (1.0 + a);
        out[2] = (1.0 - a) / (1.0 + a);
    };
    poly (shape::kTiltZeroRatio * freq, shape::kTiltZeroQ, en);
    poly (shape::kTiltPoleRatio * freq, shape::kTiltPoleQ, ed);
}

// Once a chunk: the smoothed controls and what follows from them.
void Engine::controlChunk ()
{
    const double c = std::min (1.0, cSmooth * kChunk);
    glide (freq, std::clamp (p[kFreq], 20.0, 125.0), c, 1e-5);
    glide (qf, std::clamp (p[kQFactor], 0.0, 1.0), c, 1e-6);
    glide (intensity, std::clamp (p[kIntensity], 0.0, 1.0), c, 1e-6);
    glide (boost, std::clamp (p[kBoost], 0.0, 1.1), c, 1e-6);
    glide (drive, std::clamp (p[kDrive], 0.0, 1.0), c, 1e-6);
    glide (x2, p[kDriveX2] >= 0.5 ? 1.0 : 0.0, c, 1e-6);
    glide (lowCut, std::clamp (p[kLowCut], 20.0, 125.0), c, 1e-5);
    glide (dryWet, std::clamp (p[kDryWet], -1.0, 1.0), c, 1e-6);
    bandCoefs ();

    // the oversampling: a new factor starts its filters again
    const int osSel = std::clamp ((int)std::lround (p[kOversample]), 0, 2);
    if (osSel != osNow)
    {
        osNow = osSel;
        osFactor = osSel == kOs4x ? 4 : (osSel == kOs2x ? 2 : 1);
        for (int ch = 0; ch < 2; ++ch)
        {
            os2[ch].reset ();
            os4[ch].reset ();
            std::fill (dryDelay[ch].begin (), dryDelay[ch].end (), 0.0f);
        }
        dryLen = osFactor == 4 ? os4[0].latency () : (osFactor == 2 ? os2[0].latency () : 0);
        dryPos = 0;
        fbA = std::exp (-2.0 * dsp::kPi * shape::kFeedbackHz / (sr * osFactor));
    }
    // the rectified band's shelf follows Freq
    {
        const Shelf t = shelfAt (shape::kRectRatio * freq, shape::kRectShelf, sr * osFactor);
        tB0 = t.b0, tB1 = t.b1, tA1 = t.a1;
    }
    double pre, post, drv;
    gainsDb (p, intensity, boost, drive, x2 >= 0.5, pre, post, drv);
    // (the band's inversion after the clipper, as fitted: the clipper's bias and the rectified band see the
    // focus sections' polarity)
    gPre = std::fabs (bandGain) / bandPostNorm * dbToGain (pre);
    gPost = (bandGain < 0.0 ? -1.0 : 1.0) * bandPostNorm * dbToGain (post);
    gDrive = dbToGain (drv);
    // the compressor: its range Boost's part ahead of the clipper, its detector the band with all of Boost
    compRange = shape::kCompRange * boost * 100.0;
    compDet = shape::kCompDetScale * dbToGain ((shape::kCompDetBoost - shape::kBoostPre) * (boost * 100.0 - 55.0));

    // the gate
    gateThr = dbToGain (std::clamp (p[kGate], -60.0, 0.0));
    gateExp = std::max (0.0, std::clamp (p[kRatio], 1.0, 100.0) - 1.0);
    const double atk = std::clamp (p[kAttack], 0.0, 300.0), rel = std::clamp (p[kRelease], 20.0, 500.0);
    gAtk = atk <= 0.0 ? 1.0 : coefFor (atk * 1e-3, sr);
    gRel = coefFor (rel * 1e-3, sr);

    // Dry/Wet and Low Cut
    if (std::fabs (dryWet - shownMix) > 1e-7 || std::fabs (lowCut - shownLowCut) > 1e-6)
    {
        shownMix = dryWet;
        shownLowCut = lowCut;
        const MixLaw l = mixLaw (dryWet);
        wetG = l.wet;
        dryG = l.dry;
        mixK = l.k;
        lcC.k = 1.0 / l.q;
        lcC.set (std::tan (dsp::kPi * lowCut / sr));
    }

    // the limiter's lookahead (a change clears it)
    const int lk = p[kLookahead] >= 0.5 ? (int)lookBuf[0].size () - 1 : 0;
    if (lk != lookNow)
    {
        lookNow = look = lk;
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (lookBuf[ch].begin (), lookBuf[ch].end (), 0.0f);
            std::fill (grBuf[ch].begin (), grBuf[ch].end (), 0.0);
            grSum[ch] = 0.0;
        }
        lookPos = 0;
    }
}

// The effect over a chunk: in -> out (before Dry/Wet), gate: the gate's gain per sample.
void Engine::processWet (const double (*in)[kChunk], double (*out)[kChunk], int m, const double* gate)
{
    // the base rate: the first two sections, the tilt, the gains into the clipper and the gate
    float u[2][kChunk];
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < m; ++i)
        {
            double s = in[c][i], lp;
            for (int k = 1; k <= 2; ++k)
                s = bandC[k].k * band[k][c].tick (s, bandC[k], lp);
            const double e = s + en[1] * eX[c][0] + en[2] * eX[c][1] - ed[1] * eY[c][0] - ed[2] * eY[c][1];
            eX[c][1] = eX[c][0];
            eX[c][0] = s;
            eY[c][1] = eY[c][0];
            eY[c][0] = e;
            u[c][i] = (float)(e * gPre * gate[i]);
        }
    // the compressor (linked): Boost's part ahead of the clipper taken back as the band gets loud
    for (int i = 0; i < m; ++i)
    {
        const double d = std::max (std::fabs ((double)u[0][i]), std::fabs ((double)u[1][i])) * compDet;
        compEnv += (d - compEnv) * (d > compEnv ? compAtk : compRel);
        double gc = 1.0;
        if (compRange > 0.0)
        {
            const double z = (20.0 * std::log10 (compEnv + 1e-12) - shape::kCompThrDb) / shape::kCompWidthDb;
            const double sp = z > -30.0 ? std::log1p (std::exp (z)) : 0.0;
            const double gr = compRange * (1.0 - std::exp (-shape::kCompShape * sp * shape::kCompWidthDb / compRange));
            gc = std::pow (10.0, -gr / 20.0);
        }
        compGrNow = gc;
        u[0][i] = (float)(u[0][i] * gc);
        u[1][i] = (float)(u[1][i] * gc);
    }
    for (int c = 0; c < 2; ++c)
        clipAndPost (u[c], out[c], c, m);
}

// One channel's clipper (oversampled when asked) and the band's last two sections.
void Engine::clipAndPost (float* u, double* out, int c, int m)
{
    // the clipper, oversampled when asked
    float* x = upBuf[c].data ();
    const int mo = m * osFactor;
    if (osFactor == 4)
        os4[c].up (u, x, m);
    else if (osFactor == 2)
        os2[c].up (u, x, m);
    else
        std::copy (u, u + m, x);
    for (int j = 0; j < mo; ++j)
    {
        const double v0 = x[j];
        const double r = std::fabs (v0);
        const double t = tB0 * r + tB1 * tZ[c] - tA1 * tY[c];
        tZ[c] = r;
        tY[c] = t;
        double v = v0 + shape::kRect * t - shape::kFeedback * fbH[c];
        v = v / (1.0 + std::fabs (v) * (1.0 / shape::kSoft));
        const double w = clipCurve (v);
        fbL[c] = w + fbA * (fbL[c] - w);
        fbH[c] = w - fbL[c];
        x[j] = (float)((w + shape::kFeedback * fbH[c]) * gDrive);
    }
    if (osFactor == 4)
        os4[c].down (x, u, m);
    else if (osFactor == 2)
        os2[c].down (x, u, m);
    else
        std::copy (x, x + m, u);
    // the base rate: the tilt undone, the last two sections, the gain after the clipper
    for (int i = 0; i < m; ++i)
    {
        const double s0 = u[i];
        const double e = s0 + ed[1] * iX[c][0] + ed[2] * iX[c][1] - en[1] * iY[c][0] - en[2] * iY[c][1];
        iX[c][1] = iX[c][0];
        iX[c][0] = s0;
        iY[c][1] = iY[c][0];
        iY[c][0] = e;
        double s = e, lp;
        s = bandC[0].k * band[0][c].tick (s, bandC[0], lp);
        s = bandC[3].k * band[3][c].tick (s, bandC[3], lp);
        out[i] = s * gPost;
    }
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    const pk::NoDenormals guard;
    const double outT = p[kOutput] <= -59.99 ? 0.0 : dbToGain (p[kOutput]);
    const bool soft = p[kSoftClip] >= 0.5, hard = p[kHardClip] >= 0.5;
    double inPeak = 0.0, wetPeak = 0.0, limMax = 0.0;

    for (int a = 0; a < n; a += kChunk)
    {
        const int m = std::min (kChunk, n - a);
        controlChunk ();

        // the gate (linked: the louder channel's peak)
        double gate[kChunk];
        for (int i = 0; i < m; ++i)
        {
            const double d = std::max (std::fabs ((double)xl[a + i]), std::fabs ((double)xr[a + i]));
            inPeak = std::max (inPeak, d);
            double t = 1.0;
            if (d < gateThr)
            {
                t = d > 0.0 ? std::pow (d / gateThr, gateExp) : 0.0;
                if (t < shape::kGateFloor)
                    t = 0.0;
            }
            double& g = gateG[0];
            g += (t - g) * (t > g ? gAtk : gRel);
            gate[i] = g;
        }
        gateG[1] = gateG[0];

        double in[2][kChunk], wet[2][kChunk];
        for (int i = 0; i < m; ++i)
        {
            in[0][i] = xl[a + i];
            in[1][i] = xr[a + i];
        }
        processWet (in, wet, m, gate);

        for (int i = 0; i < m; ++i)
        {
            outGain += (outT - outGain) * cSmooth;
            if (std::fabs (outT - outGain) < 1e-7)
                outGain = outT;
            double y[2];
            for (int c = 0; c < 2; ++c)
            {
                // the dry, delayed to meet the oversampled effect
                double dry = in[c][i];
                if (dryLen > 0)
                {
                    const size_t k = (size_t)((dryPos + (int)dryDelay[c].size () - dryLen) % (int)dryDelay[c].size ());
                    dryDelay[c][(size_t)dryPos] = (float)dry;
                    dry = dryDelay[c][k];
                }
                wetPeak = std::max (wetPeak, std::fabs (wet[c][i]));
                // Dry/Wet, then Low Cut on the sum (blended in below 0 %)
                const double s = dryG * dry + wetG * wet[c][i];
                double h = s, lp;
                for (int k = 0; k < 2; ++k)
                {
                    const double bp = lc[c][k].tick (h, lcC, lp);
                    h = h - lcC.k * bp - lp;
                }
                y[c] = (1.0 - mixK) * s + mixK * h;
            }
            if (dryLen > 0)
                dryPos = (dryPos + 1) % (int)dryDelay[0].size ();

            // the output limiter (linked), its gain reduction in dB
            const double pk = std::max (std::fabs (y[0]), std::fabs (y[1]));
            const double t = pk > 1.0 ? 20.0 * std::log10 (pk) : 0.0;
            double& fast = limFast[0];
            double& slow = limSlow[0];
            fast = std::max (t, fast * limAf);
            if (fast > slow)
            {
                slow += (fast - slow) * limAa;
                limHold[0] = limHoldN;
            }
            else if (limHold[0] > 0)
                --limHold[0];
            else
                slow *= limAr;
            double gr = std::max (fast, slow);
            if (look > 0)
            {
                // ramped in over the lookahead (a mean of the window's running maximum) on the delayed signal
                const int L = look;
                const int slot = lookPos;
                grBuf[0][(size_t)slot] = gr;
                double mx = 0.0;
                for (int k = 0; k <= L; ++k)
                    mx = std::max (mx, grBuf[0][(size_t)k]);
                grSum[0] += mx - grBuf[1][(size_t)slot];
                grBuf[1][(size_t)slot] = mx;
                const double ramp = std::max (0.0, grSum[0] / (L + 1));
                // the signal L samples back (the ring holds L + 1: the oldest is the next slot)
                const size_t oldest = (size_t)((slot + 1) % (L + 1));
                for (int c = 0; c < 2; ++c)
                {
                    lookBuf[c][(size_t)slot] = (float)y[c];
                    y[c] = lookBuf[c][oldest];
                }
                lookPos = (lookPos + 1) % (L + 1);
                gr = ramp;
            }
            limGrNow = gr;
            limMax = std::max (limMax, gr);
            const double g = gr > 0.0 ? std::pow (10.0, -gr / 20.0) : 1.0;
            for (int c = 0; c < 2; ++c)
            {
                float o = (float)(std::clamp (y[c] * g, -1.0, 1.0) * outGain);
                if (soft)
                    o = softClip (o);
                if (hard)
                    o = hardClip (o);
                (c == 0 ? yl : yr)[a + i] = o;
            }
        }
        // (never: a NaN or an overflow in the filters starts them again)
        bool ok = std::isfinite (yl[a + m - 1]) && std::isfinite (yr[a + m - 1]);
        for (int s = 0; ok && s < 4; ++s)
            ok = band[s][0].finite () && band[s][1].finite ();
        if (!ok)
        {
            reset ();
            for (int i = 0; i < m; ++i)
                yl[a + i] = yr[a + i] = 0.0f;
        }
    }

    if (meters)
    {
        constexpr auto rx = std::memory_order_relaxed;
        meters->sampleRate.store ((float)sr, rx);
        meters->inDb.store ((float)(20.0 * std::log10 (std::max (inPeak, 1e-6))), rx);
        meters->gateDb.store ((float)(20.0 * std::log10 (std::max (gateG[0], 1e-6))), rx);
        meters->wetDb.store ((float)(20.0 * std::log10 (std::max (wetPeak, 1e-6))), rx);
        meters->limitDb.store ((float)-limMax, rx);
        meters->blocks.fetch_add (1, std::memory_order_release);
    }
}

} // namespace bubr

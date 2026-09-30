#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace locus {

namespace {
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
constexpr int kFreqRadius = 3; // bins either side for the neighbourhood level
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    tail.prepare (sr, maxBlock);
    for (uint32_t f = 0; f < pk::kTailFields; ++f)
        tail.setParam (f, p[kTailBase + f]);
    for (uint32_t f = 0; f < pk::kTailExtFields; ++f)
        tail.setParam (pk::kTailFields + f, p[kTailExtBase + f]);
    for (uint32_t f = 0; f < pk::kTailExt2Fields; ++f)
        tail.setParam (pk::kTailFields + pk::kTailExtFields + f, p[kTailExt2Base + f]);
    // ~85 ms analysis window at any rate, 8x overlap for good time resolution
    fftSize = 1;
    while (fftSize < sr * 0.085)
        fftSize <<= 1;
    fftSize = std::clamp (fftSize, 1024, 16384);
    hop = fftSize / 8;
    fft.init (fftSize);
    const int bins = fftSize / 2 + 1;
    window.resize ((size_t)fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t)i] = 0.5f - 0.5f * std::cos (2.0f * (float)M_PI * i / fftSize);
    for (auto* v : {&inL, &inR, &accL, &accR, &frame})
        v->assign ((size_t)fftSize, 0.0f);
    for (auto* v : {&lvl, &ref, &weight, &gainDb, &mag})
        v->assign ((size_t)bins, 0.0f);
    specL.assign ((size_t)bins, {});
    specR.assign ((size_t)bins, {});
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    reset ();
}

void Engine::reset ()
{
    for (auto* v : {&inL, &inR, &accL, &accR})
        std::fill (v->begin (), v->end (), 0.0f);
    for (auto* v : {&lvl, &ref, &gainDb})
        std::fill (v->begin (), v->end (), 0.0f);
    inPos = outPos = hopCount = 0;
    normDbSmoothed = 0.0f;
    outGain = dbToGain ((float)p[kOutput]);
    tail.reset ();
}

float Engine::rangeWeight (int bin) const
{
    const double f = bin * sr / fftSize;
    if (f <= 0.0)
        return 0.0f;
    const double lo = std::clamp (p[kLowFreq], 10.0, sr * 0.2);
    const double hi = std::max (p[kHighFreq], lo * 1.05);
    constexpr double taper = 1.0 / 3.0; // octaves
    double t = 0.0;
    if (f < lo)
        t = std::log2 (lo / f) / taper;
    else if (f > hi)
        t = std::log2 (f / hi) / taper;
    if (t >= 1.0)
        return 0.0f;
    return (float)(0.5 + 0.5 * std::cos (M_PI * t));
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    const float target = dbToGain ((float)p[kOutput]);
    const int N = fftSize;
    for (int i = 0; i < n; ++i)
    {
        inL[(size_t)inPos] = xl[i];
        inR[(size_t)inPos] = xr[i];
        outGain += (target - outGain) * smooth;
        yl[i] = accL[(size_t)outPos] * outGain;
        yr[i] = accR[(size_t)outPos] * outGain;
        accL[(size_t)outPos] = 0.0f;
        accR[(size_t)outPos] = 0.0f;
        inPos = (inPos + 1) % N;
        outPos = (outPos + 1) % N;
        if (++hopCount >= hop)
        {
            hopCount = 0;
            processFrame ();
        }
    }
    tail.process (yl, yr, n);
}

void Engine::processFrame ()
{
    const int N = fftSize, bins = N / 2 + 1;
    const float binHz = (float)(sr / N);
    const bool smoothMode = std::lround (p[kMode]) == kSmooth;
    const double hopSec = hop / sr;
    // time constants (seconds): how fast the contrast measurement follows the spectrum, and the
    // attack / release of the gains it produces
    const double tauRef = smoothMode ? 0.45 : 0.07;
    const double tauAtk = smoothMode ? 0.04 : 0.005;
    const double tauRel = smoothMode ? 0.25 : 0.05;
    const float aRef = (float)std::exp (-hopSec / tauRef);
    const float aAtk = (float)std::exp (-hopSec / tauAtk);
    const float aRel = (float)std::exp (-hopSec / tauRel);
    const float aNorm = (float)std::exp (-hopSec / 0.03);
    const double contrast = std::clamp (p[kContrast], -1.0, 1.0);
    const float alpha = (float)(contrast >= 0.0 ? contrast * 1.0 : contrast * 0.6);
    const float userGainDb = (float)p[kGain];
    const bool solo = p[kSolo] >= 0.5;

    // analysis
    for (int i = 0; i < N; ++i)
        frame[(size_t)i] = inL[(size_t)((inPos + i) % N)] * window[(size_t)i];
    fft.forward (frame.data (), specL.data ());
    for (int i = 0; i < N; ++i)
        frame[(size_t)i] = inR[(size_t)((inPos + i) % N)] * window[(size_t)i];
    fft.forward (frame.data (), specR.data ());

    // the processed region: up to the top of the range taper
    const double hi = std::max (p[kHighFreq], p[kLowFreq] * 1.05);
    const int kMax = std::min (bins - 1, (int)std::ceil (hi * std::pow (2.0, 1.0 / 3.0) / binHz) + kFreqRadius + 1);
    const int snapBins = std::min (Spectrum::kMaxBins, (int)(2000.0 / binHz) + 1);
    const int measured = std::max (kMax + kFreqRadius, spectrum ? snapBins : 0);
    for (int k = 0; k <= measured && k < bins; ++k)
    {
        const float pl = std::norm (specL[(size_t)k]), pr = std::norm (specR[(size_t)k]);
        mag[(size_t)k] = std::sqrt (0.5f * (pl + pr)); // stereo-linked level
    }

    // relative level -> contrast gain
    float targetDb[Spectrum::kMaxBins * 8];
    const int kLimit = std::min (kMax, (int)(sizeof (targetDb) / sizeof (float)) - 1);
    const float eps = 1e-7f * N;
    for (int k = 1; k <= kLimit; ++k)
    {
        const float w = rangeWeight (k);
        weight[(size_t)k] = w;
        float num = 0.0f, den = 0.0f;
        for (int j = -kFreqRadius; j <= kFreqRadius; ++j)
        {
            const int kk = k + j;
            if (kk < 1 || kk >= bins)
                continue;
            const float wj = (float)(kFreqRadius + 1 - std::abs (j));
            num += wj * mag[(size_t)kk] * mag[(size_t)kk];
            den += wj;
        }
        const float neighbourhood = std::sqrt (num / std::max (den, 1e-9f));
        // The bin level and its neighbourhood are smoothed with the same time constant, so a
        // level change of the whole range scales both alike and their ratio does not move.
        lvl[(size_t)k] = aRef * lvl[(size_t)k] + (1.0f - aRef) * mag[(size_t)k];
        ref[(size_t)k] = aRef * ref[(size_t)k] + (1.0f - aRef) * neighbourhood;
        float t = 0.0f;
        if (w > 0.0f && alpha != 0.0f)
        {
            const float rho = (lvl[(size_t)k] + eps) / (ref[(size_t)k] + eps);
            t = std::clamp (20.0f * alpha * std::log10 (rho), -30.0f, 12.0f) * w;
        }
        targetDb[k] = t;
    }

    // smooth the gains over time, then find the gain that keeps the loudness of the range
    // steady (contrast shapes, Gain sets the level); it follows the gains actually applied
    double eIn = 0.0, eOut = 0.0;
    for (int k = 1; k <= kLimit; ++k)
    {
        float& cur = gainDb[(size_t)k];
        const float tgt = targetDb[k];
        const float a = std::fabs (tgt) > std::fabs (cur) ? aAtk : aRel;
        cur = tgt + a * (cur - tgt);
        const float w = weight[(size_t)k];
        if (w > 0.0f)
        {
            const double m2 = (double)mag[(size_t)k] * mag[(size_t)k] * w;
            eIn += m2;
            eOut += m2 * std::pow (10.0, cur / 10.0);
        }
    }
    float normDb = 0.0f;
    if (eIn > 1e-12 && eOut > 1e-12)
        normDb = std::clamp ((float)(10.0 * std::log10 (eIn / eOut)), -12.0f, 12.0f);
    normDbSmoothed = aNorm * normDbSmoothed + (1.0f - aNorm) * normDb;

    // apply
    const float magScale = 2.0f / (0.5f * N); // Hann coherent gain
    for (int k = 0; k < bins; ++k)
    {
        float g = 1.0f;
        if (k >= 1 && k <= kLimit)
        {
            const float w = weight[(size_t)k];
            g = dbToGain (gainDb[(size_t)k] + w * (normDbSmoothed + userGainDb));
            if (solo)
                g *= w;
        }
        else if (solo)
            g = 0.0f;
        if (g != 1.0f)
        {
            specL[(size_t)k] *= g;
            specR[(size_t)k] *= g;
        }
        if (spectrum && k < snapBins)
        {
            const float inDb = 20.0f * std::log10 (std::max (1e-9f, mag[(size_t)k] * magScale));
            const float gdb = 20.0f * std::log10 (std::max (1e-9f, g));
            spectrum->inputDb[(size_t)k].store (inDb, std::memory_order_relaxed);
            spectrum->outputDb[(size_t)k].store (inDb + gdb, std::memory_order_relaxed);
            spectrum->gainDb[(size_t)k].store (gdb, std::memory_order_relaxed);
        }
    }
    if (spectrum)
    {
        spectrum->binHz.store (binHz, std::memory_order_relaxed);
        spectrum->bins.store (snapBins, std::memory_order_release);
    }

    // synthesis: windowed overlap-add (Hann^2 at 8x overlap sums to 3)
    const float scale = 1.0f / 3.0f;
    fft.inverse (specL.data (), frame.data ());
    for (int i = 0; i < N; ++i)
        accL[(size_t)((outPos + i) % N)] += frame[(size_t)i] * window[(size_t)i] * scale;
    fft.inverse (specR.data (), frame.data ());
    for (int i = 0; i < N; ++i)
        accR[(size_t)((outPos + i) % N)] += frame[(size_t)i] * window[(size_t)i] * scale;
}

} // namespace locus

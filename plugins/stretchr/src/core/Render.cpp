#include "Render.h"

#include "smempler/src/core/Fft.h"
#include "smempler/src/core/Interp.h"
#include "smempler/src/core/Warp.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace stretchr {

using smempler::Fft;

RenderSettings settingsFromParams (const double* p, double hostBpm)
{
    RenderSettings s;
    s.algorithm = std::clamp ((int)std::lround (p[kAlgorithm]), 0, (int)kNumAlgorithms - 1);
    s.semis = std::round (p[kPitch]) + std::round (p[kFine]) / 100.0;
    s.formantSemis = p[kFormant];
    s.preserveFormants = p[kPreserveFormants] >= 0.5;
    s.speed = p[kSpeed];
    if (p[kFollowTempo] >= 0.5 && hostBpm > 1.0 && p[kSourceBpm] > 1.0)
        s.speed = std::clamp (hostBpm / p[kSourceBpm], 0.05, 4.0);
    s.windowMs = p[kWindow];
    s.transients = std::clamp ((int)std::lround (p[kTransients]), 0, 2);
    s.smearMs = p[kSmear];
    s.gainDb = p[kGain];
    return s;
}

namespace {

// Throttled progress/cancel callback.
struct Ticker
{
    const Progress& fn;
    int counter = 0;
    bool cancelled = false;
    bool operator() (double fraction)
    {
        if (cancelled)
            return false;
        if (!fn || (++counter & 31) != 0)
            return true;
        cancelled = !fn ((float)std::clamp (fraction, 0.0, 1.0));
        return !cancelled;
    }
};

inline double semisAt (const Clip& c, const RenderSettings& s, double srcSeconds)
{
    return s.semis + pitchAt (c.pitch, srcSeconds);
}

inline float readLinear (const float* d, int len, double pos)
{
    const long long i = (long long)std::floor (pos);
    if (i < 0 || i + 1 >= len)
        return (i >= 0 && i < len) ? d[i] : 0.0f;
    const float f = (float)(pos - (double)i);
    return d[i] + f * (d[i + 1] - d[i]);
}

//------------------------------------------------------------------------------
// The streaming engines shared with Smempler, driven block by block along the time map.
template <typename Engine>
bool runEngine (Engine& eng, const Clip& c, const RenderSettings& s, const TimeMap& map, float* L, float* R,
                long long total, Ticker& tick)
{
    const SampleData& a = *c.audio;
    const double sr = a.sampleRate;
    smempler::WarpRates w;
    w.srcRate = 1.0;
    w.formantShift = std::pow (2.0, s.formantSemis / 12.0);
    constexpr int kBlock = 64;
    for (long long i = 0; i < total; i += kBlock)
    {
        const int n = (int)std::min<long long> (kBlock, total - i);
        const double target = map.srcAt ((double)(i + n) / sr) * sr;
        const double v = eng.virtualPos ();
        w.srcPerOut = std::clamp ((target - v) / n, 1e-4, 64.0);
        w.pitchRatio = std::pow (2.0, semisAt (c, s, v / sr) / 12.0);
        eng.render (a, L + i, R + i, n, w);
        if (!tick ((double)i / (double)total))
            return false;
    }
    return true;
}

bool renderTape (const Clip& c, const TimeMap& map, float* L, float* R, long long total, Ticker& tick)
{
    const SampleData& a = *c.audio;
    const double sr = a.sampleRate;
    const float* d0 = a.data (0);
    const float* d1 = a.numChannels > 1 ? a.data (1) : nullptr;
    constexpr int kBlock = 64;
    for (long long i = 0; i < total; i += kBlock)
    {
        const int n = (int)std::min<long long> (kBlock, total - i);
        const double v0 = map.srcAt ((double)i / sr) * sr;
        const double v1 = map.srcAt ((double)(i + n) / sr) * sr;
        const double rate = (v1 - v0) / n;
        const float cutoff = (float)std::min (1.0, 1.0 / std::max (rate, 1e-6));
        for (int j = 0; j < n; ++j)
            smempler::readSinc (d0, d1, a.length, v0 + rate * j, cutoff, L[i + j], R[i + j]);
        if (!tick ((double)i / (double)total))
            return false;
    }
    return true;
}

//------------------------------------------------------------------------------
// Soloist: pitch-synchronous overlap-add. Voiced grains are two periods long and centred on
// pitch marks; they are re-spaced for the new pitch (and repeated or skipped for the new
// timing), which keeps the formants where they were. Unvoiced parts are plain overlap-add.
bool renderPsola (const Clip& c, const RenderSettings& s, const TimeMap& map, AnalysisCache& cache, float* L,
                  float* R, long long total, Ticker& tick)
{
    const SampleData& a = *c.audio;
    if (cache.audio != &a)
        analysePitchMarks (a, cache);
    const auto& marks = cache.marks;
    if (marks.empty ())
        return true;
    const double sr = a.sampleRate;
    const float* d0 = a.data (0);
    const float* d1 = a.numChannels > 1 ? a.data (1) : nullptr;
    const double f = std::pow (2.0, s.formantSemis / 12.0);
    double ts = 0.0;
    while (ts < (double)total)
    {
        const double vs = map.srcAt (ts / sr) * sr;
        if (vs >= a.length)
            break;
        auto it = std::lower_bound (marks.begin (), marks.end (), vs,
                                    [] (const AnalysisCache::Mark& m, double v) { return (double)m.pos < v; });
        size_t mi = it == marks.end () ? marks.size () - 1 : (size_t)(it - marks.begin ());
        if (mi > 0 && std::fabs ((double)marks[mi - 1].pos - vs) < std::fabs ((double)marks[mi].pos - vs))
            --mi;
        const auto& mk = marks[mi];
        const double P = std::max (8.0, (double)mk.period);
        const double ratio = mk.voiced ? std::pow (2.0, semisAt (c, s, vs / sr) / 12.0) : 1.0;
        const double Ps = std::max (4.0, P / ratio);
        // Grains stay two source periods long (shorter or longer with the formant shift). Longer
        // grains would resolve the source harmonics and keep them from moving when pitching down.
        const double Ho = std::max (P / f, 4.0);
        const double g = std::min (1.0, Ps / Ho); // overlapping grains add up coherently
        const double centre = mk.voiced ? (double)mk.pos : vs;
        const long long oc = (long long)std::llround (ts);
        const int J = (int)std::floor (Ho);
        for (int j = -J; j <= J; ++j)
        {
            const long long o = oc + j;
            if (o < 0 || o >= total)
                continue;
            const float wgt = (float)(g * (0.5 + 0.5 * std::cos (M_PI * j / Ho)));
            const double sp = centre + j * f;
            const float l = readLinear (d0, a.length, sp);
            L[o] += wgt * l;
            R[o] += wgt * (d1 ? readLinear (d1, a.length, sp) : l);
        }
        ts += Ps;
        if (!tick (ts / (double)total))
            return false;
    }
    return true;
}

//------------------------------------------------------------------------------
// Extreme: long analysis frames resynthesised with random phases (spectral smearing), for
// stretching far beyond what the other algorithms can do without artefacts.
bool renderExtreme (const Clip& c, const RenderSettings& s, const TimeMap& map, float* L, float* R, long long total,
                    Ticker& tick)
{
    const SampleData& a = *c.audio;
    const double sr = a.sampleRate;
    const bool stereo = a.numChannels > 1;
    int N = 1024;
    while (N < 65536 && N < s.smearMs * 0.001 * sr)
        N <<= 1;
    const int H = N / 4, bins = N / 2 + 1;
    Fft fft (N);
    std::vector<float> win ((size_t)N), buf ((size_t)N), y ((size_t)N);
    double meanW2 = 0.0;
    for (int i = 0; i < N; ++i)
    {
        win[(size_t)i] = 0.5f - 0.5f * std::cos (2.0f * (float)M_PI * i / N);
        meanW2 += (double)win[(size_t)i] * win[(size_t)i];
    }
    meanW2 /= N;
    // Random phases make overlapping frames add up in power: scale back to the input level.
    const float g = (float)(1.0 / (meanW2 * std::sqrt ((double)N / H)));
    std::vector<Fft::cf> spec ((size_t)bins), shifted ((size_t)bins);
    std::vector<float> mag ((size_t)bins), phase ((size_t)bins);
    uint32_t seed = 0x9e3779b9u;
    auto rnd = [&seed] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return (float)(seed >> 8) * (1.0f / 16777216.0f);
    };
    for (long long cpos = 0; cpos - N / 2 < total; cpos += H)
    {
        const double vs = map.srcAt ((double)std::min (cpos, total) / sr) * sr;
        const double ratio = std::pow (2.0, semisAt (c, s, vs / sr) / 12.0);
        for (int k = 0; k < bins; ++k)
            phase[(size_t)k] = 2.0f * (float)M_PI * rnd ();
        for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
        {
            const float* d = a.data (ch);
            const long long start = (long long)std::llround (vs) - N / 2;
            for (int i = 0; i < N; ++i)
            {
                const long long idx = start + i;
                buf[(size_t)i] = (idx >= 0 && idx < a.length) ? d[idx] * win[(size_t)i] : 0.0f;
            }
            fft.forward (buf.data (), spec.data ());
            for (int k = 0; k < bins; ++k)
                mag[(size_t)k] = std::abs (spec[(size_t)k]);
            for (int k = 0; k < bins; ++k)
            {
                const double src = k / ratio;
                float m = 0.0f;
                if (src < bins - 1)
                {
                    const int i0 = (int)src;
                    const float fr = (float)(src - i0);
                    m = mag[(size_t)i0] + fr * (mag[(size_t)i0 + 1] - mag[(size_t)i0]);
                }
                shifted[(size_t)k] = (k == 0 || k == bins - 1) ? Fft::cf (m, 0.0f) : std::polar (m, phase[(size_t)k]);
            }
            fft.inverse (shifted.data (), y.data ());
            float* out = ch == 0 ? L : R;
            for (int i = 0; i < N; ++i)
            {
                const long long o = cpos - N / 2 + i;
                if (o >= 0 && o < total)
                    out[o] += y[(size_t)i] * win[(size_t)i] * g;
            }
        }
        if (!tick ((double)cpos / (double)total))
            return false;
    }
    if (!stereo)
        std::memcpy (R, L, sizeof (float) * (size_t)total);
    return true;
}

} // namespace

//==============================================================================
// Pitch marks

void analysePitchMarks (const SampleData& a, AnalysisCache& cache)
{
    cache.audio = &a;
    cache.marks.clear ();
    const int len = a.length;
    if (len <= 0)
        return;
    const double sr = a.sampleRate;
    const float* d0 = a.data (0);
    const float* d1 = a.numChannels > 1 ? a.data (1) : nullptr;
    std::vector<float> mono ((size_t)len);
    for (int i = 0; i < len; ++i)
        mono[(size_t)i] = d1 ? 0.5f * (d0[i] + d1[i]) : d0[i];

    // Pitch track on a 2x decimated signal (YIN difference function computed with FFTs).
    const int D = sr > 32000.0 ? 2 : 1;
    const double sd = sr / D;
    const int nd = len / D;
    std::vector<float> x ((size_t)std::max (nd, 1));
    for (int i = 0; i < nd; ++i)
    {
        float acc = 0.0f;
        for (int j = 0; j < D; ++j)
            acc += mono[(size_t)(i * D + j)];
        x[(size_t)i] = acc / D;
    }
    const int tmax = (int)std::ceil (sd / 55.0), tmin = std::max (2, (int)std::floor (sd / 1000.0));
    const int W = tmax, hop = std::max (1, (int)std::lround (0.01 * sd));
    int N = 1;
    while (N < 2 * (W + tmax))
        N <<= 1;
    Fft fft (N);
    std::vector<double> sq ((size_t)nd + 1, 0.0);
    float peak = 1e-9f;
    for (int i = 0; i < nd; ++i)
    {
        sq[(size_t)i + 1] = sq[(size_t)i] + (double)x[(size_t)i] * x[(size_t)i];
        peak = std::max (peak, std::fabs (x[(size_t)i]));
    }
    const int frames = nd > W + tmax ? (nd - W - tmax) / hop + 1 : 0;
    std::vector<float> period ((size_t)std::max (frames, 1), 0.0f);
    std::vector<uint8_t> voiced ((size_t)std::max (frames, 1), 0);
    std::vector<float> fa ((size_t)N), fb ((size_t)N), r ((size_t)N);
    std::vector<Fft::cf> A ((size_t)N / 2 + 1), B ((size_t)N / 2 + 1);
    std::vector<double> dd ((size_t)tmax + 1);
    const double silence = std::max (1e-8, (double)peak * peak * 1e-4); // -40 dB below the peak (power)
    for (int fi = 0; fi < frames; ++fi)
    {
        const int t = fi * hop;
        const double e0 = sq[(size_t)(t + W)] - sq[(size_t)t];
        if (e0 / W < silence)
            continue;
        std::fill (fa.begin (), fa.end (), 0.0f);
        std::fill (fb.begin (), fb.end (), 0.0f);
        std::copy (x.begin () + t, x.begin () + t + W, fa.begin ());
        std::copy (x.begin () + t, x.begin () + t + W + tmax, fb.begin ());
        fft.forward (fa.data (), A.data ());
        fft.forward (fb.data (), B.data ());
        for (size_t k = 0; k < A.size (); ++k)
            A[k] = std::conj (A[k]) * B[k];
        fft.inverse (A.data (), r.data ());
        double running = 0.0;
        dd[0] = 1.0;
        for (int tau = 1; tau <= tmax; ++tau)
        {
            const double et = sq[(size_t)(t + tau + W)] - sq[(size_t)(t + tau)];
            const double d = std::max (0.0, e0 + et - 2.0 * r[(size_t)tau]);
            running += d;
            dd[(size_t)tau] = running > 0.0 ? d * tau / running : 1.0;
        }
        int best = -1;
        for (int tau = tmin; tau < tmax; ++tau)
            if (dd[(size_t)tau] < 0.15)
            {
                while (tau + 1 < tmax && dd[(size_t)tau + 1] < dd[(size_t)tau])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            best = tmin;
            for (int tau = tmin; tau < tmax; ++tau)
                if (dd[(size_t)tau] < dd[(size_t)best])
                    best = tau;
            if (dd[(size_t)best] > 0.3)
                continue;
        }
        double p = best;
        if (best > tmin && best < tmax - 1)
        {
            const double y0 = dd[(size_t)best - 1], y1 = dd[(size_t)best], y2 = dd[(size_t)best + 1];
            const double den = y0 - 2.0 * y1 + y2;
            if (std::fabs (den) > 1e-12)
                p += std::clamp (0.5 * (y0 - y2) / den, -0.5, 0.5);
        }
        period[(size_t)fi] = (float)(p * D);
        voiced[(size_t)fi] = 1;
    }
    // A voiced frame between two unvoiced ones is usually a detection glitch.
    for (int fi = 1; fi + 1 < frames; ++fi)
        if (voiced[(size_t)fi] && !voiced[(size_t)fi - 1] && !voiced[(size_t)fi + 1])
            voiced[(size_t)fi] = 0;

    const double frameCentre = W * 0.5 * D, frameHop = (double)hop * D;
    auto frameAt = [&] (double pos) {
        if (frames <= 0)
            return -1;
        return std::clamp ((int)std::lround ((pos - frameCentre) / frameHop), 0, frames - 1);
    };
    const int unvoicedStep = std::max (16, (int)std::lround (0.01 * sr));
    int last = -1;
    bool prevVoiced = false;
    while (true)
    {
        const int probe = last < 0 ? 0 : last;
        if (probe >= len - 1)
            break;
        const int fi = frameAt (probe);
        if (fi >= 0 && voiced[(size_t)fi])
        {
            const double P = period[(size_t)fi];
            int lo, hi;
            if (prevVoiced)
            {
                lo = (int)std::floor (last + 0.75 * P);
                hi = (int)std::ceil (last + 1.25 * P);
            }
            else
            {
                lo = last < 0 ? 0 : last + unvoicedStep / 2;
                hi = (int)std::ceil (lo + P);
            }
            lo = std::max (lo, last + 1);
            hi = std::min (hi, len - 1);
            if (lo > hi)
                break;
            int m = lo;
            for (int i = lo; i <= hi; ++i)
                if (mono[(size_t)i] > mono[(size_t)m])
                    m = i;
            cache.marks.push_back ({m, (float)P, true});
            last = m;
            prevVoiced = true;
        }
        else
        {
            const int m = last < 0 ? 0 : last + unvoicedStep;
            if (m >= len)
                break;
            cache.marks.push_back ({m, (float)unvoicedStep, false});
            last = m;
            prevVoiced = false;
        }
    }
}

//==============================================================================

bool renderClip (const Clip& clip, const RenderSettings& s, double outRate, Rendered& out, AnalysisCache& cache,
                 const Progress& progress)
{
    if (clip.empty () || outRate <= 0.0)
        return false;
    const SampleData& a = *clip.audio;
    const double sr = a.sampleRate;
    std::vector<StretchMarker> markers = clip.markers;
    sanitizeMarkers (markers, clip.srcLength ());
    const TimeMap map (markers, 1.0 / std::clamp (s.speed, 0.01, 100.0));
    const long long total = (long long)std::ceil (map.outLength () * sr);
    if (total <= 0 || total > 0x7ffffff0LL)
        return false;
    std::vector<float> L ((size_t)total, 0.0f), R ((size_t)total, 0.0f);
    Ticker tick {progress};
    smempler::PlayRegion region;
    region.start = 0.0;
    region.end = a.length;
    bool ok = true;
    switch (s.algorithm)
    {
        case kWindowed:
        case kBalanced:
        {
            auto eng = std::make_unique<smempler::GrainWarp> ();
            eng->prepare (sr);
            eng->start (region, s.algorithm == kBalanced, (float)s.windowMs, 0.0f, 1u);
            ok = runEngine (*eng, clip, s, map, L.data (), R.data (), total, tick);
            break;
        }
        case kBeats:
        {
            std::vector<int> bounds;
            for (const auto& o : a.onsets)
                if (o.strength >= 0.1f)
                    bounds.push_back (o.pos);
            auto eng = std::make_unique<smempler::BeatsWarp> ();
            eng->prepare (sr);
            eng->start (a, region, bounds, 1, 1.0f);
            ok = runEngine (*eng, clip, s, map, L.data (), R.data (), total, tick);
            break;
        }
        case kSoloist: ok = renderPsola (clip, s, map, cache, L.data (), R.data (), total, tick); break;
        case kExtreme: ok = renderExtreme (clip, s, map, L.data (), R.data (), total, tick); break;
        case kTape: ok = renderTape (clip, map, L.data (), R.data (), total, tick); break;
        case kPolyphonic:
        default:
        {
            const int base = sr > 50000.0 ? 2048 : 1024;
            const int frame = std::min (4096, base << std::clamp (s.transients, 0, 2));
            auto eng = std::make_unique<smempler::PvWarp> ();
            eng->prepare (sr);
            eng->start (a, region, s.preserveFormants, 1.0f, 128, frame);
            ok = runEngine (*eng, clip, s, map, L.data (), R.data (), total, tick);
            break;
        }
    }
    if (!ok)
        return false;

    // Resample to the host rate.
    if (std::fabs (outRate - sr) > 1e-6)
    {
        const long long total2 = (long long)std::ceil ((double)total * outRate / sr);
        std::vector<float> L2 ((size_t)total2), R2 ((size_t)total2);
        const double step = sr / outRate;
        const float cutoff = (float)std::min (1.0, outRate / sr);
        for (long long j = 0; j < total2; ++j)
        {
            smempler::readSinc (L.data (), R.data (), (int)total, (double)j * step, cutoff, L2[(size_t)j], R2[(size_t)j]);
            if ((j & 8191) == 0 && !tick (1.0))
                return false;
        }
        L.swap (L2);
        R.swap (R2);
    }

    // Gain and short edge fades so the clip never clicks in or out.
    const float gain = (float)std::pow (10.0, s.gainDb / 20.0);
    const long long n = (long long)L.size ();
    const long long fadeIn = std::min<long long> (n / 2, (long long)(0.002 * outRate));
    const long long fadeOut = std::min<long long> (n / 2, (long long)(0.005 * outRate));
    for (long long i = 0; i < n; ++i)
    {
        float g = gain;
        if (i < fadeIn)
            g *= (float)i / (float)fadeIn;
        if (i >= n - fadeOut)
            g *= (float)(n - 1 - i) / (float)fadeOut;
        float l = L[(size_t)i] * g, r = R[(size_t)i] * g;
        L[(size_t)i] = std::isfinite (l) ? l : 0.0f;
        R[(size_t)i] = std::isfinite (r) ? r : 0.0f;
    }
    out.l = std::move (L);
    out.r = std::move (R);
    out.sampleRate = outRate;
    out.srcSeconds = clip.srcLength ();
    if (progress)
        progress (1.0f);
    return true;
}

} // namespace stretchr

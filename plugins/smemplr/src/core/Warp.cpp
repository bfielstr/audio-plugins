#include "Warp.h"

#include "Interp.h"
#include "Lfo.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace smemplr {

namespace {
inline float wrapPi (float x)
{
    x = std::fmod (x + (float)M_PI, 2.0f * (float)M_PI);
    if (x < 0.0f)
        x += 2.0f * (float)M_PI;
    return x - (float)M_PI;
}
} // namespace

//==============================================================================
// Beats

void BeatsWarp::prepare (double sr)
{
    hostSr = sr;
    seg.reserve (4096);
    fadeLen = std::max (16, (int)(0.003 * sr));
}

void BeatsWarp::start (const SampleData& s, const PlayRegion& r, const std::vector<int>& boundaries, int mode,
                       float env01)
{
    region = r;
    loopMode = mode;
    envelope = std::clamp (env01, 0.0f, 1.0f);
    seg.clear ();
    seg.push_back (r.start);
    const double limit = r.loop ? r.loopEnd : r.end;
    const double minGap = 0.01 * s.sampleRate;
    for (int b : boundaries)
        if (b > r.start + minGap && b < limit - minGap && seg.size () + 2 < seg.capacity ())
            seg.push_back (b);
    if (r.loop && r.loopStart > r.start + minGap)
        seg.push_back (r.loopStart);
    std::sort (seg.begin (), seg.end ());
    seg.erase (std::unique (seg.begin (), seg.end (), [minGap] (double a, double b) { return b - a < minGap; }),
               seg.end ());
    v = r.start;
    lastP = r.start;
    curIdx = -1;
    cur = {};
    old = {};
    oldFade = 0;
    finished = false;
}

void BeatsWarp::beginSegment (int idx, double p, const WarpRates& w)
{
    if (cur.active)
    {
        old = cur;
        oldFade = fadeLen;
    }
    curIdx = idx;
    double s0 = seg[(size_t)idx];
    if (region.loop && p >= region.loopStart && s0 < region.loopStart)
        s0 = region.loopStart;
    double e0 = (size_t)idx + 1 < seg.size () ? seg[(size_t)idx + 1] : (region.loop ? region.loopEnd : region.end);
    if (region.loop)
        e0 = std::min (e0, region.loopEnd);
    e0 = std::max (e0, s0 + 2.0);
    cur.segStart = s0;
    cur.segEnd = e0;
    segOutLen = std::max (1.0, (e0 - s0) / w.srcPerOut);
    segOutElapsed = std::max (0.0, (p - s0) / w.srcPerOut);
    cur.pos = s0 + segOutElapsed * w.pitchRatio * w.srcRate;
    const double segLen = e0 - s0;
    const double maxTail = 0.12 * hostSr * w.srcRate;
    const double tail = std::clamp (segLen * 0.5, 2.0, std::max (2.0, maxTail));
    cur.tailStart = e0 - tail;
    cur.dir = 1;
    cur.active = true;
}

void BeatsWarp::readerTick (const SampleData& s, const SampleReader& in, Reader& rd, double rate, float& l, float& r) const
{
    switch (loopMode)
    {
        case 0: // Off: play the segment once, then silence
        {
            if (rd.pos >= rd.segEnd)
            {
                l = r = 0.0f;
                break;
            }
            in.read (rd.pos, l, r);
            const double xf = fadeLen * rate;
            const double rem = rd.segEnd - rd.pos;
            if (rem < xf)
            {
                const float g = (float)(rem / xf);
                l *= g;
                r *= g;
            }
            rd.pos += rate;
            break;
        }
        case 1: // Forward loop of the segment tail
        {
            const double tail = rd.segEnd - rd.tailStart;
            in.read (rd.pos, l, r);
            const double xf = std::min (tail * 0.5, 0.004 * s.sampleRate);
            if (xf > 2.0 && rd.pos > rd.segEnd - xf)
            {
                const float x = (float)((rd.pos - (rd.segEnd - xf)) / xf);
                float l2, r2;
                in.read (rd.pos - tail, l2, r2);
                l = l * (1.0f - x) + l2 * x;
                r = r * (1.0f - x) + r2 * x;
            }
            rd.pos += rate;
            int guard = 0;
            while (rd.pos >= rd.segEnd && tail > 0.0 && ++guard < 64)
                rd.pos -= tail;
            break;
        }
        default: // Back and forth over the tail
        {
            in.read (rd.pos, l, r);
            rd.pos += rate * rd.dir;
            int guard = 0;
            while (++guard < 64)
            {
                if (rd.dir > 0 && rd.pos >= rd.segEnd)
                {
                    rd.pos = 2.0 * rd.segEnd - rd.pos;
                    rd.dir = -1;
                }
                else if (rd.dir < 0 && rd.pos <= rd.tailStart)
                {
                    rd.pos = 2.0 * rd.tailStart - rd.pos;
                    rd.dir = 1;
                }
                else
                    break;
            }
            break;
        }
    }
}

void BeatsWarp::render (const SampleData& s, float* L, float* R, int n, const WarpRates& w)
{
    const double rate = w.pitchRatio * w.srcRate;
    const SampleReader in (s, rate); // band-limited at any speed
    const float envPow = (1.0f - envelope) * 3.0f;
    for (int i = 0; i < n; ++i)
    {
        if (finished || seg.empty ())
        {
            L[i] = R[i] = 0.0f;
            continue;
        }
        const double p = region.map (v);
        int idx = (int)(std::upper_bound (seg.begin (), seg.end (), p) - seg.begin ()) - 1;
        idx = std::max (0, idx);
        if (idx != curIdx || p < lastP - 0.5)
            beginSegment (idx, p, w);
        lastP = p;

        float l, r;
        readerTick (s, in, cur, rate, l, r);
        if (envPow > 0.0f)
        {
            const float t = (float)std::min (1.0, segOutElapsed / segOutLen);
            const float g = std::pow (1.0f - t, envPow);
            l *= g;
            r *= g;
        }
        if (oldFade > 0)
        {
            float ol, orr;
            readerTick (s, in, old, rate, ol, orr);
            const float x = (float)oldFade / (float)fadeLen;
            l = l * (1.0f - x) + ol * x;
            r = r * (1.0f - x) + orr * x;
            --oldFade;
        }
        L[i] = l;
        R[i] = r;
        segOutElapsed += 1.0;
        v += w.srcPerOut;
        if (region.pastEnd (v))
            finished = true;
    }
}

//==============================================================================
// Tones / Texture

void GrainWarp::prepare (double sr) { hostSr = sr; }

void GrainWarp::start (const PlayRegion& r, bool tonesMode, float grainMs, float flux01, uint32_t seedIn)
{
    region = r;
    tones = tonesMode;
    flux = std::clamp (flux01, 0.0f, 1.0f);
    seed = seedIn | 1u;
    grainLen = std::max (64, (int)(grainMs * 0.001 * hostSr)) & ~1;
    for (auto& g : grains)
        g = {};
    v = r.start;
    hopCounter = 0;
    first = true;
    finished = false;
}

double GrainWarp::align (const SampleData& s, double natural, double target, double rate) const
{
    const int len = s.length;
    const float* a = s.data (0);
    const float* b = s.numChannels > 1 ? s.data (1) : nullptr;
    const int win = std::clamp ((int)(lastGrainHop * rate), 64, 1024);
    const int range = (int)std::min (0.5 * grainLen * rate, 0.015 * s.sampleRate);
    const int nat = (int)natural;
    if (nat < 0 || nat + win >= len || range < 4)
        return target;
    auto mono = [&] (int i) { return b ? a[i] + b[i] : a[i]; };
    auto score = [&] (int c, int stride) {
        double ab = 0.0, bb = 1e-9;
        for (int j = 0; j < win; j += stride)
        {
            const double x = mono (nat + j), y = mono (c + j);
            ab += x * y;
            bb += y * y;
        }
        return ab / std::sqrt (bb);
    };
    const int t = (int)target;
    const int lo = std::max ((int)region.start, t - range);
    const int hi = std::min (len - win - 1, t + range);
    if (hi <= lo)
        return target;
    int best = std::clamp (t, lo, hi);
    double bestScore = -1e30;
    for (int c = lo; c <= hi; c += 4)
    {
        const double sc = score (c, 4);
        if (sc > bestScore)
        {
            bestScore = sc;
            best = c;
        }
    }
    const int c0 = best;
    for (int c = std::max (lo, c0 - 3); c <= std::min (hi, c0 + 3); ++c)
    {
        const double sc = score (c, 2);
        if (sc > bestScore || c == c0)
        {
            if (c != c0 && sc <= bestScore)
                continue;
            bestScore = sc;
            best = c;
        }
    }
    return best + (target - (double)t);
}

void GrainWarp::render (const SampleData& s, float* L, float* R, int n, const WarpRates& w)
{
    const double rate = w.pitchRatio * w.srcRate;
    const SampleReader in (s, rate); // band-limited at any speed
    const int hop = grainLen / 2;
    const double fadeSrc = 64.0 * rate;
    for (int i = 0; i < n; ++i)
    {
        if (finished)
        {
            L[i] = R[i] = 0.0f;
            continue;
        }
        if (hopCounter <= 0 && !region.pastEnd (v))
        {
            const double target = region.map (v);
            double src = target;
            if (!first)
            {
                if (tones)
                    src = align (s, lastGrainSrc + lastGrainHop * lastGrainRate, target, rate);
                else if (flux > 0.0f)
                    src = std::max (region.start, target + flux * 0.5 * grainLen * rate * randomBipolar (seed));
            }
            for (auto& g : grains)
                if (!g.active)
                {
                    g.src = src;
                    g.age = 0;
                    g.len = grainLen;
                    g.flatStart = first;
                    g.active = true;
                    break;
                }
            lastGrainSrc = src;
            lastGrainRate = rate;
            lastGrainHop = hop;
            first = false;
            hopCounter = hop;
        }
        float sl = 0.0f, sr = 0.0f;
        bool any = false;
        for (auto& g : grains)
        {
            if (!g.active)
                continue;
            any = true;
            float wgt = 0.5f - 0.5f * std::cos (2.0f * (float)M_PI * (float)g.age / (float)g.len);
            if (g.flatStart && g.age < g.len / 2)
                wgt = 1.0f;
            if (!region.loop && g.src >= region.end)
                wgt *= (float)std::max (0.0, 1.0 - (g.src - region.end) / fadeSrc);
            if (wgt > 0.0f)
            {
                float l, r;
                in.read (g.src, l, r);
                sl += l * wgt;
                sr += r * wgt;
            }
            g.src += rate;
            if (++g.age >= g.len)
                g.active = false;
        }
        L[i] = sl;
        R[i] = sr;
        --hopCounter;
        v += w.srcPerOut;
        if (!any && region.pastEnd (v))
            finished = true;
    }
}

//==============================================================================
// Complex / Complex Pro

void PvWarp::prepare (double sampleRate)
{
    hostSr = sampleRate;
    const size_t n = kMaxN, b = kMaxN / 2 + 1;
    for (auto* vec : {&window, &winBase, &fa, &fb, &fl, &fr, &olaL, &olaR, &olaW, &cep})
        vec->assign (n, 0.0f);
    for (auto* vec : {&synthPhase, &corr, &logEnv, &envBase})
        vec->assign (b, 0.0f);
    for (auto* vec : {&sa, &sb, &sl, &sr, &stmp})
        vec->assign (b, Fft::cf ());
    peakOf.assign (b, 0);
    fifoL.assign (kFifo, 0.0f);
    fifoR.assign (kFifo, 0.0f);
}

void PvWarp::start (const SampleData& s, const PlayRegion& r, bool fm, float f01, int order, int frameSize)
{
    region = r;
    formantMode = fm;
    formants = std::clamp (f01, 0.0f, 1.0f);
    envOrder = order;
    baseN = frameSize == 1024 || frameSize == 2048 || frameSize == 4096 ? frameSize : (s.sampleRate > 50000.0 ? 4096 : 2048);
    level = -1; // chosen by the first render, from the speed the note starts at
    levelScale = 1.0;
    setFrame (baseN);
    std::copy (window.begin (), window.begin () + baseN, winBase.begin ());
    stereo = s.numChannels > 1;
    std::fill (olaL.begin (), olaL.end (), 0.0f);
    std::fill (olaR.begin (), olaR.end (), 0.0f);
    std::fill (olaW.begin (), olaW.end (), 0.0f);
    std::fill (synthPhase.begin (), synthPhase.end (), 0.0f);
    apos = r.start;
    envValid = false;
    v = r.start;
    written = 0;
    firstFrame = true;
    finished = false;
}

Fft* PvWarp::fftOf (int size)
{
    return size == 4096 ? &fft4k : (size == 2048 ? &fft2k : (size == 1024 ? &fft1k : (size == 512 ? &fft512 : &fft256)));
}

void PvWarp::setFrame (int size)
{
    N = size;
    fft = fftOf (N);
    hs = N / 4;
    for (int i = 0; i < N; ++i)
        window[(size_t)i] = 0.5f - 0.5f * std::cos (2.0f * (float)M_PI * i / N);
    readPos = N / 2;
}

void PvWarp::chooseLevel (const SampleData& s, double consume)
{
    // the level whose rate the note starts at between 2^0.5 and 2^1.5 times real time: room to bend
    // about an octave up (readSincRing is clean up to 4x) and some down before the top gets dull
    level = consume >= std::exp2 (1.5) ? std::min ((int)std::floor (std::log2 (consume) - 0.5), s.levels () - 1) : 0;
    levelScale = std::ldexp (1.0, level);
    // the same analysis as at the sample's rate: frames as long in the sample's time, so the same
    // frequency resolution (and the same transients), at the level's rate
    if (level > 0)
        setFrame (std::max (256, baseN >> level));
}

void PvWarp::readFrame (const SampleData& s, double centre, float* mid, float* l, float* r) const
{
    // centre and the region are in the sample's frames; the frame's samples are the level's
    const int lv = std::max (level, 0);
    const double scale = levelScale;
    const long long c = (long long)std::llround (centre / scale);
    const float* d0 = s.levelData (lv, 0);
    const float* d1 = stereo ? s.levelData (lv, 1) : nullptr;
    const long long len = s.levelLength (lv);
    for (int i = 0; i < N; ++i)
    {
        const double vp = (double)(c - N / 2 + i) * scale;
        float a = 0.0f, b = 0.0f;
        if (vp >= region.start && !region.pastEnd (vp))
        {
            const long long idx = lv == 0 ? (long long)region.map (vp) : (long long)std::floor (region.map (vp) / scale + 0.5);
            if (idx >= 0 && idx < len)
            {
                a = d0[idx];
                b = d1 ? d1[idx] : a;
            }
        }
        const float wv = window[(size_t)i];
        if (mid)
            mid[i] = 0.5f * (a + b) * wv;
        if (l)
            l[i] = a * wv;
        if (r)
            r[i] = b * wv;
    }
}

void PvWarp::sampleEnvelope (const SampleData& s)
{
    const int eN = baseN;
    const int eBins = eN / 2 + 1;
    Fft* efft = fftOf (eN);
    const long long c = (long long)std::llround (apos);
    const float* d0 = s.data (0);
    const float* d1 = stereo ? s.data (1) : nullptr;
    float* x = fa.data (); // free again: its spectrum is in sa, which is free too
    for (int i = 0; i < eN; ++i)
    {
        const double vp = (double)(c - eN / 2 + i);
        float a = 0.0f, b = 0.0f;
        if (vp >= region.start && !region.pastEnd (vp))
        {
            const long long idx = (long long)region.map (vp);
            if (idx >= 0 && idx < s.length)
            {
                a = d0[idx];
                b = d1 ? d1[idx] : a;
            }
        }
        x[i] = 0.5f * (a + b) * winBase[(size_t)i];
    }
    std::vector<Fft::cf>& tmp = sa;
    efft->forward (x, tmp.data ());
    for (int k = 0; k < eBins; ++k)
        tmp[(size_t)k] = Fft::cf (std::log (std::abs (tmp[(size_t)k]) + 1e-7f), 0.0f);
    efft->inverse (tmp.data (), cep.data ());
    const int q = std::clamp (envOrder * eN / 8192, 4, eN / 2 - 1);
    for (int i = q; i <= eN - q; ++i)
        cep[(size_t)i] = 0.0f;
    efft->forward (cep.data (), tmp.data ());
    for (int k = 0; k < eBins; ++k)
        envBase[(size_t)k] = tmp[(size_t)k].real ();
}

void PvWarp::levelFormants (const SampleData& s, const WarpRates& w, bool shiftFormants)
{
    // On a level the frame holds only the level's band (a 16th of the spectrum at level 4, say), but
    // the formants of what is heard up to the output's Nyquist lie far above it: the envelope is taken
    // from the sample itself, a frame of baseN (as long as the level's frame in the sample's time, so
    // level bin k is the envelope's bin k * baseN / (N * 2^level)), the rest as at level 0. Taken
    // again once the analysis has moved on by a quarter of that frame (as often as at the sample's own
    // rate: read fast, the level's frames come several times as often).
    const int eN = baseN;
    const int eBins = eN / 2 + 1;
    if (!envValid || std::fabs (apos - envAt) >= 0.25 * eN)
    {
        envAt = apos;
        envValid = true;
        sampleEnvelope (s);
    }
    auto env = [&] (float kk) {
        if (kk >= eBins - 1)
            return envBase[(size_t)eBins - 1];
        const int i0 = (int)kk;
        const float fr0 = kk - i0;
        return envBase[(size_t)i0] + fr0 * (envBase[(size_t)i0 + 1] - envBase[(size_t)i0]);
    };
    const float toBase = (float)(eN / (N * levelScale));
    const float ratio = (float)w.pitchRatio;
    const float amount = shiftFormants ? 1.0f : formants;
    const float target = shiftFormants ? (float)(std::pow (w.pitchRatio, formantMode ? 1.0 - formants : 1.0) * w.formantShift)
                                       : 1.0f;
    const int bins = N / 2 + 1;
    for (int k = 0; k < bins; ++k)
    {
        const float kb = k * toBase;
        corr[(size_t)k] = std::clamp (std::exp (amount * (env (kb * ratio / target) - env (kb))), 0.06f, 16.0f);
    }
}

void PvWarp::synthesiseFrame (const SampleData& s, const WarpRates& w)
{
    const int bins = N / 2 + 1;
    const float twoPi = 2.0f * (float)M_PI;

    readFrame (s, apos - hs * levelScale, fa.data (), nullptr, nullptr);
    readFrame (s, apos, fb.data (), fl.data (), stereo ? fr.data () : nullptr);
    fft->forward (fa.data (), sa.data ());
    fft->forward (fb.data (), sb.data ());
    fft->forward (fl.data (), sl.data ());
    if (stereo)
        fft->forward (fr.data (), sr.data ());

    // magnitudes of the mid frame (stored in corr temporarily)
    float eA = 0.0f, eB = 0.0f;
    for (int k = 0; k < bins; ++k)
    {
        const float ma = std::abs (sa[(size_t)k]), mb = std::abs (sb[(size_t)k]);
        eA += ma * ma;
        eB += mb * mb;
        logEnv[(size_t)k] = mb;
    }
    const bool reset = firstFrame || eB > 4.0f * eA + 1e-12f;

    // peak picking and regions of influence
    int lastPeak = -1, prevPeak = 0;
    for (int k = 0; k < bins; ++k)
    {
        const float m = logEnv[(size_t)k];
        bool isPeak = m > 1e-9f;
        for (int j = std::max (0, k - 2); j <= std::min (bins - 1, k + 2) && isPeak; ++j)
            if (j != k && logEnv[(size_t)j] > m)
                isPeak = false;
        if (!isPeak)
            continue;
        if (lastPeak < 0)
            for (int j = 0; j < k; ++j)
                peakOf[(size_t)j] = k;
        else
        {
            const int mid = (lastPeak + k) / 2;
            for (int j = prevPeak; j <= k; ++j)
                peakOf[(size_t)j] = j <= mid ? lastPeak : k;
        }
        lastPeak = k;
        prevPeak = k;
    }
    if (lastPeak < 0)
        for (int j = 0; j < bins; ++j)
            peakOf[(size_t)j] = j;
    else
        for (int j = prevPeak; j < bins; ++j)
            peakOf[(size_t)j] = lastPeak;

    // phase propagation at peaks
    for (int k = 0; k < bins; ++k)
    {
        if (peakOf[(size_t)k] != k)
            continue;
        const float phiB = std::arg (sb[(size_t)k]);
        if (reset)
            synthPhase[(size_t)k] = phiB;
        else
        {
            const float phiA = std::arg (sa[(size_t)k]);
            const float omega = twoPi * k / N;
            const float dphi = wrapPi (phiB - phiA - omega * hs);
            const float inst = omega + dphi / hs;
            synthPhase[(size_t)k] = wrapPi (synthPhase[(size_t)k] + inst * hs);
        }
    }
    // identity phase locking for the other bins; store rotations in stmp (as unit vectors)
    for (int k = 0; k < bins; ++k)
    {
        const int p = peakOf[(size_t)k];
        const float phiK = std::arg (sb[(size_t)k]);
        if (p != k)
            synthPhase[(size_t)k] = synthPhase[(size_t)p] + phiK - std::arg (sb[(size_t)p]);
        const float d = synthPhase[(size_t)k] - phiK;
        stmp[(size_t)k] = Fft::cf (std::cos (d), std::sin (d));
    }

    // formant correction (Complex Pro): keep the spectral envelope where it was before resampling
    const bool shiftFormants = std::fabs (w.formantShift - 1.0) > 1e-4;
    const bool doFormants =
        (formantMode && formants > 0.0f && std::fabs (w.pitchRatio - 1.0) > 1e-4) || shiftFormants;
    if (doFormants && level > 0)
        levelFormants (s, w, shiftFormants);
    else if (doFormants)
    {
        std::vector<Fft::cf>& tmp = sa; // sa is no longer needed
        for (int k = 0; k < bins; ++k)
            tmp[(size_t)k] = Fft::cf (std::log (logEnv[(size_t)k] + 1e-7f), 0.0f);
        fft->inverse (tmp.data (), cep.data ());
        const int q = std::clamp (envOrder * N / 8192, 4, N / 2 - 1);
        for (int i = q; i <= N - q; ++i)
            cep[(size_t)i] = 0.0f;
        fft->forward (cep.data (), tmp.data ());
        for (int k = 0; k < bins; ++k)
            logEnv[(size_t)k] = tmp[(size_t)k].real ();
        // After resampling by the pitch ratio, bin k lands at k * ratio; the envelope that should end up
        // there is the one from (k * ratio) / target, target being how far the envelope itself moves.
        const float ratio = (float)w.pitchRatio;
        const float amount = shiftFormants ? 1.0f : formants;
        const float target = shiftFormants ? (float)(std::pow (w.pitchRatio, formantMode ? 1.0 - formants : 1.0) *
                                                     w.formantShift)
                                           : 1.0f;
        for (int k = 0; k < bins; ++k)
        {
            const float kk = k * ratio / target;
            float e2;
            if (kk >= bins - 1)
                e2 = logEnv[(size_t)bins - 1];
            else
            {
                const int i0 = (int)kk;
                const float fr0 = kk - i0;
                e2 = logEnv[(size_t)i0] + fr0 * (logEnv[(size_t)i0 + 1] - logEnv[(size_t)i0]);
            }
            corr[(size_t)k] = std::clamp (std::exp (amount * (e2 - logEnv[(size_t)k])), 0.06f, 16.0f);
        }
    }

    auto synth = [&] (std::vector<Fft::cf>& spec, std::vector<float>& ola) {
        for (int k = 0; k < bins; ++k)
        {
            Fft::cf y = spec[(size_t)k] * stmp[(size_t)k];
            if (doFormants)
                y *= corr[(size_t)k];
            spec[(size_t)k] = y;
        }
        fft->inverse (spec.data (), fb.data ());
        for (int i = 0; i < N; ++i)
            ola[(size_t)i] += fb[(size_t)i] * window[(size_t)i];
    };
    synth (sl, olaL);
    if (stereo)
        synth (sr, olaR);
    for (int i = 0; i < N; ++i)
        olaW[(size_t)i] += window[(size_t)i] * window[(size_t)i];

    const int mask = kFifo - 1;
    for (int i = 0; i < hs; ++i)
    {
        const float g = 1.0f / std::max (olaW[(size_t)i], 0.1f);
        const int idx = (int)((written + i) & mask);
        fifoL[(size_t)idx] = olaL[(size_t)i] * g;
        fifoR[(size_t)idx] = stereo ? olaR[(size_t)i] * g : fifoL[(size_t)idx];
    }
    std::memmove (olaL.data (), olaL.data () + hs, sizeof (float) * (size_t)(N - hs));
    std::memmove (olaR.data (), olaR.data () + hs, sizeof (float) * (size_t)(N - hs));
    std::memmove (olaW.data (), olaW.data () + hs, sizeof (float) * (size_t)(N - hs));
    std::fill (olaL.begin () + (N - hs), olaL.begin () + N, 0.0f);
    std::fill (olaR.begin () + (N - hs), olaR.begin () + N, 0.0f);
    std::fill (olaW.begin () + (N - hs), olaW.begin () + N, 0.0f);
    written += hs;

    const double consume = w.pitchRatio * w.srcRate / levelScale; // fifo frames read per output sample
    apos += hs * w.srcPerOut / consume * levelScale;
    firstFrame = false;
}

void PvWarp::render (const SampleData& s, float* L, float* R, int n, const WarpRates& w)
{
    if (level < 0)
        chooseLevel (s, w.pitchRatio * w.srcRate);
    const double consume = w.pitchRatio * w.srcRate / levelScale; // the fifo is at the level's rate
    const float cutoff = (float)std::min (1.0, 1.0 / consume);
    const int reach = sincReach (cutoff);
    const int mask = kFifo - 1;
    for (int i = 0; i < n; ++i)
    {
        if (finished)
        {
            L[i] = R[i] = 0.0f;
            continue;
        }
        int guard = 0;
        while ((double)written < readPos + reach + 1 && ++guard < 16)
            synthesiseFrame (s, w);
        float l, r;
        readSincRing (fifoL.data (), fifoR.data (), mask, readPos, cutoff, l, r);
        L[i] = l;
        R[i] = r;
        readPos += consume;
        v += w.srcPerOut;
        if (region.pastEnd (v - hs * w.srcPerOut / std::max (1e-6, consume)))
            finished = true;
    }
}

} // namespace smemplr

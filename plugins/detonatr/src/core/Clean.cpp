// Detonatr's Clean stage. A short-time Fourier transform (Hann analysis and synthesis windows, 75 %
// overlap, a frame of about 43 ms whatever the sample rate) with one real gain per frequency bin,
// common to left and right so the stereo image stays put.
//
// Both channels go through a single complex FFT (left as the real part, right as the imaginary
// part). Because the gain is real and the same for a bin and its mirror, the inverse FFT hands back
// the processed left and right as its real and imaginary parts again.
//
// Tonality. The good part of an impact is its partials, so each bin is asked how tonal it is: a
// partial (steady or dying away exponentially) turns its bins by the same angle and scales them by the
// same factor from frame to frame, while noise and a diffuse room tail do neither. Tonal bins are
// spared by both processes (unless they are only the far skirt of a much louder partial).
//
// Denoise. The noise floor of each bin is a slowly rising minimum of its (smoothed) power: it falls
// at once to anything quieter and otherwise creeps up by a few dB a second. That follows the decay of
// a hit down into the noise instead of chasing it, which a plain "minimum over the last second"
// tracker would do. Three things keep it honest:
//   - where a noise-like bin's power has been steady for a while the floor may jump up to the minimum
//     of that stretch, so it learns the noise quickly after digital silence;
//   - a tonal bin never learns its own floor (it is the partial's level, not the noise's); it takes
//     the lowest floor of the noise-like bins nearby instead;
//   - a bin's floor may not stand more than kCapDb above the lowest floor of its neighbours. Noise is
//     smooth across frequency and a partial is narrow, so a steady partial can not become "noise".
// Bins that do not rise clearly above the floor are turned down by up to 30 dB (Denoise sets how far
// and how high "clearly" is), along a smooth curve.
//
// Dereverb. The late reverb of a bin is estimated from the same bin a short time ago (histLen frames,
// about 50 ms) decayed by what a typical room (T60 of 1 s) would have taken away meanwhile (after
// Lebart et al.), less the noise floor (steady noise is not a tail: that is Denoise's job, and left
// here it would flicker). That part is subtracted from the power (a Wiener-style gain, smoothed where
// the bin sits near its expected tail). A new onset has nothing loud behind it, so it passes whole;
// the attack and the first ~50 ms of the body stay, and after that the partials ring on (they are
// tonal) while the noisy tail around them goes.
//
// The gains of both are multiplied, smoothed across frequency (1-2-1) and across time (they rise at
// once, so attacks are not dulled, and fall with a short release), which keeps the "musical noise"
// (isolated bins flickering on and off) down.
#include "Clean.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define DETONATR_CLEAN_SSE 1
#endif

namespace detonatr {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kTiny = 1e-30f;     // keeps powers off zero (and off denormals)
constexpr int kSub = 5;             // sub-windows of the stationarity check
constexpr double kSubSeconds = 0.15; // each (the check looks back kSub * this)
constexpr double kStatDb = 6.0;     // max/min of the slow power below this: "steady" (noise: under 5)
constexpr double kRiseDbPerSec = 3.0;
constexpr double kCapDb = 6.0;      // how far a floor may stand above its neighbours' lowest
constexpr int kCapBins = 6;         // the neighbourhood: this many bins each side (~140 Hz)
constexpr int kFillBins = 12;       // how far a partial looks for noise-like bins to take its floor from
constexpr float kSkirtDb = 40.0f;    // tonal bins this far under the loudest nearby are not spared
constexpr double kBiasDb = 3.0;     // the floor sits under the noise's mean power by about this
constexpr double kDenoiseDepthDb = 30.0;
constexpr double kReverbT60 = 1.0;  // the room the dereverb assumes, seconds
constexpr double kReverbDelay = 0.05; // late reverb starts this long after the sound
constexpr double kDereverbDepthDb = 24.0;
constexpr double kMaxDepthDb = 40.0;
// unpredictability: tonal below lo, noise above hi (noise sits around 0.5; with the smoothing, the
// quick view of it goes under 0.27 and the slow one under 0.36 in fewer than 1 in 10000 bins x frames)
constexpr float kTonalLo = 0.07f, kTonalHi = 0.18f;
constexpr float kSlowTonalLo = 0.24f, kSlowTonalHi = 0.33f;
const float kStatRatio = (float)std::pow (10.0, kStatDb / 10.0);
const float kCapMul = (float)std::pow (10.0, kCapDb / 10.0);
const float kBias = (float)std::pow (10.0, kBiasDb / 10.0);

// Flushes denormals to zero while alive: the estimators decay towards them in silence.
class NoDenormals
{
public:
    NoDenormals ()
    {
#if defined(DETONATR_CLEAN_SSE)
        old = _mm_getcsr ();
        _mm_setcsr (old | 0x8040); // FTZ | DAZ
#endif
    }
    ~NoDenormals ()
    {
#if defined(DETONATR_CLEAN_SSE)
        _mm_setcsr (old);
#endif
    }
    NoDenormals (const NoDenormals&) = delete;
    NoDenormals& operator= (const NoDenormals&) = delete;

private:
    unsigned int old = 0;
};

inline float smoothstep (float t)
{
    t = std::min (1.0f, std::max (0.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

// x if it is a number under 1e6 in size, else 0. Tested on the bits: fast-math compilers may assume
// NaN never happens and drop a floating-point check for it.
inline float sane (float x)
{
    uint32_t b;
    std::memcpy (&b, &x, sizeof b);
    return (b & 0x7fffffffu) < 0x49742400u ? x : 0.0f; // 0x49742400: 1e6f
}

// 10 log10 (x) for x > 0, to about 0.001 dB: a few thousand of these per frame are the bulk of the
// per-bin work, and the gain curves need nothing like full precision.
inline float fastDb (float x)
{
    x = std::max (x, 1e-37f);
    uint32_t b;
    std::memcpy (&b, &x, sizeof b);
    const float e = (float)((int)((b >> 23) & 255) - 127);
    b = (b & 0x7fffffu) | 0x3f800000u; // the mantissa, 1 .. 2
    float m;
    std::memcpy (&m, &b, sizeof m);
    const float t = m - 1.0f; // log2 (1 + t), least-squares fit on 0 .. 1
    const float l2 = e + t * (1.43854793f + t * (-0.678089456f + t * (0.323646308f - 0.0842946558f * t)));
    return 3.0102999f * l2; // 10 log10 (2) * log2 (x)
}

// 10^(db / 20) for db from about -100 to +100, to about 0.001 dB
inline float fastGain (float db)
{
    const float y = std::min (100.0f, std::max (-100.0f, db)) * 0.16609640f; // log2 (10) / 20
    const float fl = std::floor (y), f = y - fl;
    const float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    const uint32_t e = (uint32_t)((int)fl + 127) << 23;
    float s;
    std::memcpy (&s, &e, sizeof s);
    return s * p;
}

} // namespace

void Clean::prepare (double sampleRate, int /*maxBlock*/)
{
    // any block size works (the frame is filled sample by sample), so maxBlock sizes nothing
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;

    // 2048 at 44.1 / 48 kHz, 4096 at 88.2 / 96 kHz, 8192 at 176.4 / 192 kHz: always ~43 ms, ~23 Hz bins
    const int octaves = std::min (3, std::max (-2, (int)std::lround (std::log2 (sr / 48000.0))));
    N = octaves >= 0 ? 2048 << octaves : 2048 >> -octaves;
    hop = N / 4;
    bins = N / 2 + 1;

    int bits = 0;
    while ((1 << bits) < N)
        ++bits;
    rev.assign ((size_t)N, 0);
    for (int i = 0; i < N; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        rev[(size_t)i] = r;
    }
    cosT.assign ((size_t)N / 2, 0.0f);
    sinT.assign ((size_t)N / 2, 0.0f);
    for (int k = 0; k < N / 2; ++k)
    {
        cosT[(size_t)k] = (float)std::cos (2.0 * kPi * k / N);
        sinT[(size_t)k] = (float)std::sin (2.0 * kPi * k / N);
    }
    // periodic Hann: at a hop of N/4 the squares add up to exactly 1.5
    win.assign ((size_t)N, 0.0f);
    for (int i = 0; i < N; ++i)
        win[(size_t)i] = (float)(0.5 - 0.5 * std::cos (2.0 * kPi * i / N));

    const double ht = hop / sr; // seconds per frame
    aGain = (float)std::exp (-ht / 0.015);
    aFloor = (float)std::exp (-ht / 0.06);
    aStat = (float)std::exp (-ht / 0.2);
    aTonalFall = (float)std::exp (-ht / 0.02);
    aTonalRise = (float)std::exp (-ht / 0.05);
    aTonalSlow = (float)std::exp (-ht / 0.15);
    aReverb = (float)std::exp (-ht / 0.04);
    release = (float)std::exp (-ht / 0.05);
    rise = (float)std::pow (10.0, kRiseDbPerSec * ht / 10.0);
    subLen = std::max (1, (int)std::lround (kSubSeconds / ht));
    // the reference frame must not overlap the current one (4 hops), or a hit would hide itself
    histLen = std::max (4, (int)std::lround (kReverbDelay / ht));
    tailDecay = (float)std::pow (10.0, -6.0 * histLen * ht / kReverbT60); // power lost over histLen frames

    for (Vec* v : {&re, &im, &inL, &inR, &accL, &accR, &z1r, &z1i, &z2r, &z2i})
        v->assign ((size_t)N, 0.0f);
    outL.assign ((size_t)hop, 0.0f);
    outR.assign ((size_t)hop, 0.0f);
    for (Vec* v : {&pw, &pg, &pf, &ps, &noiseFloor, &capped, &curMinPf, &curMinPs, &curMaxPs, &gRaw, &gTmp, &gain, &unpred, &unpredSlow, &tonal, &rvGain})
        v->assign ((size_t)bins, 0.0f);
    for (Vec* v : {&subMinPf, &subMinPs, &subMaxPs})
        v->assign ((size_t)(kSub * bins), 0.0f);
    hist.assign ((size_t)(histLen * bins), 0.0f);
    reset ();
}

void Clean::reset ()
{
    for (Vec* v : {&re, &im, &inL, &inR, &accL, &accR, &outL, &outR, &pw, &pg, &pf, &ps, &gRaw, &gTmp, &hist,
                   &z1r, &z1i, &z2r, &z2i, &tonal, &rvGain})
        std::fill (v->begin (), v->end (), 0.0f);
    std::fill (noiseFloor.begin (), noiseFloor.end (), kTiny);
    std::fill (capped.begin (), capped.end (), kTiny);
    std::fill (gain.begin (), gain.end (), 1.0f);
    std::fill (unpred.begin (), unpred.end (), 1.0f);
    std::fill (unpredSlow.begin (), unpredSlow.end (), 1.0f);
    std::fill (curMinPf.begin (), curMinPf.end (), 1e30f);
    std::fill (curMinPs.begin (), curMinPs.end (), 1e30f);
    std::fill (curMaxPs.begin (), curMaxPs.end (), 0.0f);
    // unfilled sub-windows read as "not steady" (a huge range), so nothing jumps before they are real
    std::fill (subMinPf.begin (), subMinPf.end (), kTiny);
    std::fill (subMinPs.begin (), subMinPs.end (), kTiny);
    std::fill (subMaxPs.begin (), subMaxPs.end (), 1e30f);
    pos = N - hop;
    rd = 1;
    subCount = subIdx = histPos = 0;
}

void Clean::setDenoise (double amount) { denoise = (float)std::min (1.0, std::max (0.0, amount)); }
void Clean::setDereverb (double amount) { dereverb = (float)std::min (1.0, std::max (0.0, amount)); }

// A full frame of input is out after N - 1 more samples: the sample that completes a frame leaves
// with the frame's first sample, which went in N - 1 samples before it.
int Clean::latency () const { return N - 1; }

void Clean::process (float* L, float* R, int n)
{
    if (inL.empty ())
        return; // not prepared
    NoDenormals noDenormals;
    int i = 0;
    while (i < n)
    {
        const int c = std::min (n - i, N - pos);
        // NaN, infinities and absurd levels come in as silence: one would poison every estimate
        for (int j = 0; j < c; ++j)
        {
            inL[(size_t)(pos + j)] = sane (L[i + j]);
            inR[(size_t)(pos + j)] = sane (R[i + j]);
        }
        pos += c;
        if (pos == N)
        {
            // the last sample of the chunk completes a frame and leaves with its first finished sample
            std::memcpy (L + i, outL.data () + rd, sizeof (float) * (size_t)(c - 1));
            std::memcpy (R + i, outR.data () + rd, sizeof (float) * (size_t)(c - 1));
            processFrame ();
            L[i + c - 1] = outL[0];
            R[i + c - 1] = outR[0];
            rd = 1;
            pos = N - hop;
        }
        else
        {
            std::memcpy (L + i, outL.data () + rd, sizeof (float) * (size_t)c);
            std::memcpy (R + i, outR.data () + rd, sizeof (float) * (size_t)c);
            rd += c;
        }
        i += c;
    }
}

void Clean::processFrame ()
{
    for (int j = 0; j < N; ++j)
    {
        re[(size_t)j] = inL[(size_t)j] * win[(size_t)j];
        im[(size_t)j] = inR[(size_t)j] * win[(size_t)j];
    }
    fft (re.data (), im.data ());

    // Z = XL + i XR, so |XL|^2 + |XR|^2 = (|Z[k]|^2 + |Z[N-k]|^2) / 2; pw is half that (the mean of the two)
    for (int k = 0; k < bins; ++k)
    {
        const int m = (N - k) & (N - 1);
        pw[(size_t)k] = 0.25f * (re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k]
                                 + re[(size_t)m] * re[(size_t)m] + im[(size_t)m] * im[(size_t)m]) + kTiny;
    }

    computeGains ();

    for (int k = 0; k < bins; ++k)
    {
        const float g = gain[(size_t)k];
        re[(size_t)k] *= g;
        im[(size_t)k] *= g;
        if (k > 0 && k < N / 2)
        {
            re[(size_t)(N - k)] *= g;
            im[(size_t)(N - k)] *= g;
        }
    }
    fft (im.data (), re.data ()); // swapped in and out: the inverse transform (times N)

    const float scale = 1.0f / ((float)N * 1.5f);
    for (int j = 0; j < N; ++j)
    {
        const float w = win[(size_t)j] * scale;
        accL[(size_t)j] += re[(size_t)j] * w;
        accR[(size_t)j] += im[(size_t)j] * w;
    }
    // the first hop is finished (no later frame reaches back that far); move everything along a hop
    std::memcpy (outL.data (), accL.data (), sizeof (float) * (size_t)hop);
    std::memcpy (outR.data (), accR.data (), sizeof (float) * (size_t)hop);
    const size_t rest = (size_t)(N - hop);
    std::memmove (accL.data (), accL.data () + hop, sizeof (float) * rest);
    std::memmove (accR.data (), accR.data () + hop, sizeof (float) * rest);
    std::fill (accL.begin () + (long)rest, accL.end (), 0.0f);
    std::fill (accR.begin () + (long)rest, accR.end (), 0.0f);
    std::memmove (inL.data (), inL.data () + hop, sizeof (float) * rest);
    std::memmove (inR.data (), inR.data () + hop, sizeof (float) * rest);
}

void Clean::computeGains ()
{
    const int last = bins - 1;

    // power smoothed across frequency (1-2-1), then across time at three speeds
    for (int k = 0; k < bins; ++k)
    {
        const float lo = pw[(size_t)(k > 0 ? k - 1 : 1)];
        const float hi = pw[(size_t)(k < last ? k + 1 : last - 1)];
        const float q = 0.25f * (lo + hi) + 0.5f * pw[(size_t)k];
        pg[(size_t)k] = aGain * pg[(size_t)k] + (1.0f - aGain) * q;
        pf[(size_t)k] = aFloor * pf[(size_t)k] + (1.0f - aFloor) * q;
        ps[(size_t)k] = aStat * ps[(size_t)k] + (1.0f - aStat) * q;
    }

    // Tonality: a partial (steady or dying away exponentially) turns each bin it lights by the same
    // angle and scales it by the same factor every frame; noise (and a diffuse room tail) does neither.
    // Predict this frame from the last two (the last turn and the last change of level, the latter
    // held to at most +3.5 dB) and see how far off that is: near 0 for a partial, around 0.5 for
    // noise. Z[k] and Z[N-k] both hold bin k (of left and right mixed), so both are asked.
    for (int k = 0; k < bins; ++k)
    {
        float err = 0.0f, mag = 0.0f;
        for (int side = 0; side < 2; ++side)
        {
            const size_t K = (size_t)(side == 0 ? k : (N - k) & (N - 1));
            if (side == 1 && (k == 0 || k == N / 2))
                break;
            const float ar = re[K], ai = im[K], br = z1r[K], bi = z1i[K], cr = z2r[K], ci = z2i[K];
            // the last turn, b * conj (c) as a unit vector, times the last change of level
            const float tr = br * cr + bi * ci, ti = bi * cr - br * ci;
            const float bm = std::sqrt (br * br + bi * bi), cm = std::sqrt (cr * cr + ci * ci);
            const float scale = std::min (1.5f, bm / (cm + kTiny)) / (std::sqrt (tr * tr + ti * ti) + kTiny);
            const float pr = (br * tr - bi * ti) * scale, pi = (br * ti + bi * tr) * scale;
            err += std::sqrt ((ar - pr) * (ar - pr) + (ai - pi) * (ai - pi));
            mag += std::sqrt (ar * ar + ai * ai) + std::sqrt (pr * pr + pi * pi);
            z2r[K] = br;
            z2i[K] = bi;
            z1r[K] = ar;
            z1i[K] = ai;
        }
        const float c = err / (mag + kTiny);
        // Two views, each smoothed so that noise hardly ever looks tonal: a quick one (quicker to see
        // a partial than to lose it) for strong partials and fresh hits, and a slow one that can pick
        // out a partial only ~10 dB over the noise, and keeps a fading partial tonal into the noise.
        float& u = unpred[(size_t)k];
        const float x = mag > 0.0f ? c : 1.0f, a = x < u ? aTonalFall : aTonalRise;
        u = a * u + (1.0f - a) * x;
        unpredSlow[(size_t)k] = aTonalSlow * unpredSlow[(size_t)k] + (1.0f - aTonalSlow) * x;
    }
    // a partial lights a few bins; they all count as tonal if one clearly is
    for (int k = 0; k < bins; ++k)
    {
        const int a = std::max (0, k - 1), b = std::min (last, k + 1);
        float c = 1.0f, cs = 1.0f;
        for (int j = a; j <= b; ++j)
        {
            c = std::min (c, unpred[(size_t)j]);
            cs = std::min (cs, unpredSlow[(size_t)j]);
        }
        tonal[(size_t)k] = std::max (smoothstep ((kTonalHi - c) / (kTonalHi - kTonalLo)),
                                     smoothstep ((kSlowTonalHi - cs) / (kSlowTonalHi - kSlowTonalLo)));
    }

    // the noise floor: a slowly rising minimum, which may jump up to the recent minimum where the
    // power has been steady
    const bool subDone = ++subCount >= subLen;
    for (int k = 0; k < bins; ++k)
    {
        const size_t K = (size_t)k;
        float f = std::min (noiseFloor[K] * rise, pf[K]);

        curMinPf[K] = std::min (curMinPf[K], pf[K]);
        curMinPs[K] = std::min (curMinPs[K], ps[K]);
        curMaxPs[K] = std::max (curMaxPs[K], ps[K]);
        float wMinPf = curMinPf[K], wMinPs = curMinPs[K], wMaxPs = curMaxPs[K];
        for (int u = 0; u < kSub; ++u)
        {
            const size_t U = (size_t)(u * bins) + K;
            wMinPf = std::min (wMinPf, subMinPf[U]);
            wMinPs = std::min (wMinPs, subMinPs[U]);
            wMaxPs = std::max (wMaxPs, subMaxPs[U]);
        }
        if (wMaxPs < kStatRatio * wMinPs && tonal[K] < 0.5f)
            f = std::max (f, wMinPf);
        noiseFloor[K] = std::max (f, kTiny);

        if (subDone)
        {
            const size_t U = (size_t)(subIdx * bins) + K;
            subMinPf[U] = curMinPf[K];
            subMinPs[U] = curMinPs[K];
            subMaxPs[U] = curMaxPs[K];
            curMinPf[K] = curMinPs[K] = 1e30f;
            curMaxPs[K] = 0.0f;
        }
    }
    if (subDone)
    {
        subCount = 0;
        subIdx = (subIdx + 1) % kSub;
    }

    // Under a partial the floor can not be learned (its bins never go quiet, and they are kept from
    // jumping), so it is taken to be at least the lowest floor of the noise-like bins nearby. Where
    // there are none (a dense harmonic sound), it stays as it was.
    for (int k = 0; k < bins; ++k)
    {
        float m = 1e30f;
        if (tonal[(size_t)k] >= 0.5f)
        {
            const int a = std::max (0, k - kFillBins), b = std::min (last, k + kFillBins);
            for (int j = a; j <= b; ++j)
                if (tonal[(size_t)j] < 0.5f)
                    m = std::min (m, noiseFloor[(size_t)j]);
        }
        capped[(size_t)k] = m; // scratch
    }
    for (int k = 0; k < bins; ++k)
        if (capped[(size_t)k] < 1e30f)
            noiseFloor[(size_t)k] = std::max (noiseFloor[(size_t)k], capped[(size_t)k]);

    // no floor may stand more than kCapDb above the lowest floor within kCapBins: a steady partial
    // lifts its own bins' floors, never its neighbourhood's
    for (int k = 0; k < bins; ++k)
    {
        float m = noiseFloor[(size_t)k];
        const int a = std::max (0, k - kCapBins), b = std::min (last, k + kCapBins);
        for (int j = a; j <= b; ++j)
            m = std::min (m, noiseFloor[(size_t)j]);
        capped[(size_t)k] = kBias * std::min (noiseFloor[(size_t)k], kCapMul * m);
    }

    const bool dnOn = denoise > 0.0f, rvOn = dereverb > 0.0f;
    // Denoise: full cut up to loDb above the noise, none from hiDb; more amount = deeper and higher
    const float depthDn = (float)kDenoiseDepthDb * denoise;
    const float loDb = 2.0f, hiDb = loDb + 6.0f + 6.0f * denoise;
    // Dereverb: subtract beta times the reverb estimate, down to gMin2 (a power gain)
    const float beta = 0.5f + 2.0f * dereverb;
    const float gMin2 = (float)std::pow (10.0, -kDereverbDepthDb * dereverb / 10.0);
    const size_t H = (size_t)(histPos * bins);

    for (int k = 0; k < bins; ++k)
    {
        const size_t K = (size_t)k;
        const float p = pg[K];
        float db = 0.0f;
        // Tonal bins are spared, unless they are only the far skirt of a much louder partial (whose
        // level they do not carry; the noise they hold would pass for nothing).
        float spare = 0.0f;
        if (tonal[K] > 0.0f && (dnOn || rvOn))
        {
            float m = p;
            const int a = std::max (0, k - kCapBins), b = std::min (last, k + kCapBins);
            for (int j = a; j <= b; ++j)
                m = std::max (m, pg[(size_t)j]);
            spare = tonal[K] * smoothstep ((fastDb (p / m) + kSkirtDb) / 15.0f);
        }
        if (dnOn)
        {
            const float x = fastDb (p / capped[K]);
            db -= depthDn * (1.0f - smoothstep ((x - loDb) / (hiDb - loDb))) * (1.0f - spare);
        }
        float& rv = rvGain[K];
        if (rvOn)
        {
            // Where the bin stands well clear of its expected tail (an onset) the gain follows at once;
            // near the tail's level, where noise-like power flickers either side of the estimate, it
            // is smoothed, or single bins would blink on and off.
            const float share = std::max (0.0f, tailDecay * hist[H + K] - capped[K]) / p;
            const float g = fastDb (std::max (gMin2, 1.0f - beta * (1.0f - spare) * share));
            rv = share < 0.25f ? g : aReverb * rv + (1.0f - aReverb) * g;
            db += rv;
        }
        else
            rv = 0.0f;
        hist[H + K] = p;
        gRaw[K] = db == 0.0f ? 1.0f : fastGain (std::max (db, -(float)kMaxDepthDb));
    }
    histPos = (histPos + 1) % histLen;

    // smooth across frequency, then let each gain rise at once and fall with a short release
    for (int k = 0; k < bins; ++k)
    {
        const float lo = gRaw[(size_t)(k > 0 ? k - 1 : 1)];
        const float hi = gRaw[(size_t)(k < last ? k + 1 : last - 1)];
        gTmp[(size_t)k] = 0.25f * (lo + hi) + 0.5f * gRaw[(size_t)k];
    }
    for (int k = 0; k < bins; ++k)
    {
        const float g = std::min (1.0f, gTmp[(size_t)k]);
        float& s = gain[(size_t)k];
        s = g >= s ? g : release * s + (1.0f - release) * g;
    }
}

// Iterative radix-2 decimation-in-time FFT on split real / imaginary arrays.
void Clean::fft (float* xr, float* xi) const
{
    for (int i = 0; i < N; ++i)
    {
        const int j = rev[(size_t)i];
        if (j > i)
        {
            std::swap (xr[i], xr[j]);
            std::swap (xi[i], xi[j]);
        }
    }
    for (int len = 2; len <= N; len <<= 1)
    {
        const int half = len >> 1;
        const int step = N / len;
        for (int i = 0; i < N; i += len)
        {
            for (int j = 0; j < half; ++j)
            {
                const float wr = cosT[(size_t)(j * step)], wi = -sinT[(size_t)(j * step)];
                const int a = i + j, b = a + half;
                const float tr = wr * xr[b] - wi * xi[b];
                const float ti = wr * xi[b] + wi * xr[b];
                xr[b] = xr[a] - tr;
                xi[b] = xi[a] - ti;
                xr[a] += tr;
                xi[a] += ti;
            }
        }
    }
}

} // namespace detonatr

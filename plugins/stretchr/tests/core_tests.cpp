// Headless tests for the Stretchr clip model and renderer. Run: ./stretchr_tests [filter]
#include "Clip.h"
#include "Params.h"
#include "Render.h"
#include "Session.h"
#include "Wav.h"

#include "smemplr/src/core/Fft.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace stretchr;

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        ++gChecks;                                                         \
        if (!(cond))                                                       \
        {                                                                  \
            ++gFailures;                                                   \
            std::printf ("    FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
        }                                                                  \
    } while (0)

struct TestCase
{
    const char* name;
    std::function<void ()> fn;
};
static std::vector<TestCase>& tests ()
{
    static std::vector<TestCase> t;
    return t;
}
struct Reg
{
    Reg (const char* n, std::function<void ()> f) { tests ().push_back ({n, std::move (f)}); }
};
#define TEST(name)                     \
    static void name ();               \
    static Reg reg_##name (#name, name); \
    static void name ()

constexpr double kSr = 48000.0;
static const char* kAlgoNames[] = {"Windowed", "Balanced", "Polyphonic", "Soloist", "Beats", "Extreme", "Tape", "Alien"};

static Clip clipFrom (std::vector<float> l, std::vector<float> r = {}, double sr = kSr)
{
    Clip c;
    c.audio = SampleData::fromBuffers (std::move (l), std::move (r), sr, "test");
    c.markers = identityMarkers (c.srcLength ());
    return c;
}

static std::vector<float> sine (double f, double secs, double amp = 0.5, double sr = kSr)
{
    std::vector<float> x ((size_t)(secs * sr));
    for (size_t i = 0; i < x.size (); ++i)
        x[i] = (float)(amp * std::sin (2.0 * M_PI * f * (double)i / sr));
    return x;
}

// Band-limited sawtooth (a voice-like harmonic source).
static std::vector<float> saw (double f, double secs, double amp = 0.3)
{
    std::vector<float> x ((size_t)(secs * kSr));
    for (int h = 1; f * h < 8000.0; ++h)
        for (size_t i = 0; i < x.size (); ++i)
            x[i] += (float)(amp * std::sin (2.0 * M_PI * f * h * (double)i / kSr) / h);
    return x;
}

// Short noise bursts at the given times (seconds), silence elsewhere.
static std::vector<float> clicks (std::vector<double> at, double secs)
{
    std::vector<float> x ((size_t)(secs * kSr), 0.0f);
    uint32_t seed = 7;
    for (double t : at)
    {
        const size_t s = (size_t)(t * kSr);
        for (size_t i = 0; i < (size_t)(0.03 * kSr) && s + i < x.size (); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float n = (float)((seed >> 9) & 0xffff) / 32768.0f - 1.0f;
            x[s + i] = 0.9f * n * std::exp (-(float)i / (float)(0.006 * kSr));
        }
    }
    return x;
}

static RenderSettings algo (int a)
{
    RenderSettings s;
    s.algorithm = a;
    return s;
}

static bool render (const Clip& c, const RenderSettings& s, Rendered& out, double outRate = kSr)
{
    AnalysisCache cache;
    return renderClip (c, s, outRate, out, cache);
}

static double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double acc = 0.0;
    b = std::min (b, x.size ());
    for (size_t i = a; i < b; ++i)
        acc += (double)x[i] * x[i];
    return b > a ? std::sqrt (acc / (double)(b - a)) : 0.0;
}

static bool allFinite (const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}

// Fundamental by normalised autocorrelation (60..2000 Hz) over [a, b).
static double pitchOf (const std::vector<float>& x, size_t a, size_t b, double sr = kSr)
{
    b = std::min (b, x.size ());
    const int maxLag = (int)(sr / 60.0), minLag = (int)(sr / 2000.0);
    const size_t n = b - a - (size_t)maxLag;
    std::vector<double> c ((size_t)maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = a; i < a + n; ++i)
        {
            xy += (double)x[i] * x[i + (size_t)lag];
            xx += (double)x[i] * x[i];
            yy += (double)x[i + (size_t)lag] * x[i + (size_t)lag];
        }
        c[(size_t)lag] = xy / std::sqrt (xx * yy + 1e-20);
    }
    // first peak above 0.8 of the best one (avoids octave errors)
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        best = std::max (best, c[(size_t)lag]);
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (c[(size_t)lag] >= 0.8 * best && c[(size_t)lag] >= c[(size_t)lag - 1] && c[(size_t)lag] >= c[(size_t)lag + 1])
        {
            const double y0 = c[(size_t)lag - 1], y1 = c[(size_t)lag], y2 = c[(size_t)lag + 1];
            const double den = y0 - 2 * y1 + y2;
            const double off = std::fabs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0;
            return sr / (lag + off);
        }
    return 0.0;
}

// Frequency of the strongest spectral peak over [a, a + 16384).
static double peakFreq (const std::vector<float>& x, size_t a, double sr = kSr)
{
    const int N = 16384;
    smemplr::Fft fft (N);
    std::vector<float> buf (N, 0.0f);
    for (int i = 0; i < N && a + (size_t)i < x.size (); ++i)
        buf[(size_t)i] = x[a + (size_t)i] * (0.5f - 0.5f * std::cos (2.0f * (float)M_PI * i / N));
    std::vector<smemplr::Fft::cf> spec (N / 2 + 1);
    fft.forward (buf.data (), spec.data ());
    int best = 1;
    for (int k = 1; k < N / 2; ++k)
        if (std::abs (spec[(size_t)k]) > std::abs (spec[(size_t)best]))
            best = k;
    const double y0 = std::abs (spec[(size_t)best - 1]), y1 = std::abs (spec[(size_t)best]),
                 y2 = std::abs (spec[(size_t)best + 1]);
    const double den = y0 - 2 * y1 + y2;
    const double off = std::fabs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0;
    return (best + off) * sr / N;
}

// The spectral peak of the power averaged over overlapping frames of [a, b): for granular output,
// whose single frames are speckled by the grains' random phases.
static double averagedPeakFreq (const std::vector<float>& x, size_t a, size_t b, double sr = kSr)
{
    const int N = 16384;
    smemplr::Fft fft (N);
    std::vector<float> buf (N, 0.0f);
    std::vector<smemplr::Fft::cf> spec (N / 2 + 1);
    std::vector<double> pw (N / 2 + 1, 0.0);
    for (size_t pos = a; pos + N <= b; pos += N / 4)
    {
        for (int i = 0; i < N; ++i)
            buf[(size_t)i] = x[pos + (size_t)i] * (0.5f - 0.5f * std::cos (2.0f * (float)M_PI * i / N));
        fft.forward (buf.data (), spec.data ());
        for (int k = 0; k <= N / 2; ++k)
            pw[(size_t)k] += std::norm (spec[(size_t)k]);
    }
    int best = 1;
    for (int k = 1; k < N / 2; ++k)
        if (pw[(size_t)k] > pw[(size_t)best])
            best = k;
    // the centre of mass of the peak (+-3 bins)
    double num = 0.0, den = 0.0;
    for (int k = std::max (1, best - 3); k <= std::min (N / 2, best + 3); ++k)
    {
        num += k * pw[(size_t)k];
        den += pw[(size_t)k];
    }
    return num / den * sr / N;
}

// Amplitude-weighted mean frequency (spectral centroid) over [a, a + 8192).
static double centroid (const std::vector<float>& x, size_t a)
{
    const int N = 8192;
    smemplr::Fft fft (N);
    std::vector<float> buf (N, 0.0f);
    for (int i = 0; i < N && a + (size_t)i < x.size (); ++i)
        buf[(size_t)i] = x[a + (size_t)i] * (0.5f - 0.5f * std::cos (2.0f * (float)M_PI * i / N));
    std::vector<smemplr::Fft::cf> spec (N / 2 + 1);
    fft.forward (buf.data (), spec.data ());
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double m = std::abs (spec[(size_t)k]);
        num += m * k * kSr / N;
        den += m;
    }
    return num / std::max (den, 1e-12);
}

// Amplitude of the component at frequency f over [a, a + n).
static double toneLevel (const std::vector<float>& x, double f, size_t a, size_t n)
{
    double c = 0, d = 0;
    for (size_t i = a; i < a + n && i < x.size (); ++i)
    {
        c += x[i] * std::cos (2.0 * M_PI * f * (double)i / kSr);
        d += x[i] * std::sin (2.0 * M_PI * f * (double)i / kSr);
    }
    return 2.0 * std::sqrt (c * c + d * d) / (double)n;
}

// Index of the first sample at or after `from` whose magnitude exceeds `thr`.
static long long firstAbove (const std::vector<float>& x, size_t from, float thr)
{
    for (size_t i = from; i < x.size (); ++i)
        if (std::fabs (x[i]) > thr)
            return (long long)i;
    return -1;
}

//==============================================================================

TEST (time_map_identity_and_markers)
{
    auto m = identityMarkers (2.0);
    TimeMap id (m, 1.0);
    CHECK (std::fabs (id.outLength () - 2.0) < 1e-12, "len %f", id.outLength ());
    CHECK (std::fabs (id.srcAt (1.3) - 1.3) < 1e-12, "identity");
    TimeMap half (m, 2.0); // speed 50 %
    CHECK (std::fabs (half.outLength () - 4.0) < 1e-12, "len %f", half.outLength ());
    CHECK (std::fabs (half.srcAt (3.0) - 1.5) < 1e-12, "src %f", half.srcAt (3.0));

    // pin source 1.0 s to output 1.5 s, end stays at 2.0 s
    m.insert (m.begin () + 1, {1.0, 1.5});
    sanitizeMarkers (m, 2.0);
    TimeMap tm (m, 1.0);
    CHECK (m.size () == 3, "markers %zu", m.size ());
    CHECK (std::fabs (tm.srcAt (1.5) - 1.0) < 1e-12, "pinned");
    CHECK (std::fabs (tm.srcAt (0.75) - 0.5) < 1e-12, "first segment %f", tm.srcAt (0.75));
    CHECK (std::fabs (tm.srcAt (1.75) - 1.5) < 1e-12, "second segment %f", tm.srcAt (1.75));
    CHECK (std::fabs (tm.stretchAt (0.5) - 1.5) < 1e-12, "stretch %f", tm.stretchAt (0.5));
    CHECK (std::fabs (tm.stretchAt (1.8) - 0.5) < 1e-12, "stretch %f", tm.stretchAt (1.8));
    for (double s = 0.0; s <= 2.0; s += 0.1)
        CHECK (std::fabs (tm.srcAt (tm.outAt (s)) - s) < 1e-9, "roundtrip at %f", s);
}

TEST (sanitize_markers_limits_and_order)
{
    std::vector<StretchMarker> m {{1.5, 0.2}, {0.5, 0.4}, {0.5, 0.9}, {3.0, 9.0}, {2.0, 50.0}};
    sanitizeMarkers (m, 2.0);
    CHECK (m.front ().src == 0.0 && m.front ().dst == 0.0, "starts at origin");
    CHECK (m.back ().src == 2.0, "ends at source length");
    bool ok = true;
    for (size_t i = 1; i < m.size (); ++i)
    {
        const double st = (m[i].dst - m[i - 1].dst) / (m[i].src - m[i - 1].src);
        ok = ok && m[i].src > m[i - 1].src && st >= kMinSegmentStretch - 1e-9 && st <= kMaxSegmentStretch + 1e-9;
    }
    CHECK (ok, "monotonic and within limits");
    CHECK (std::fabs (m.back ().dst - 2.0) > 0.0, "end dst %f", m.back ().dst);
}

TEST (pitch_envelope_interpolates)
{
    std::vector<PitchPoint> p {{1.0, 0.0}, {2.0, 12.0}};
    CHECK (pitchAt (p, 0.5) == 0.0, "before first");
    CHECK (std::fabs (pitchAt (p, 1.5) - 6.0) < 1e-12, "middle");
    CHECK (pitchAt (p, 3.0) == 12.0, "after last");
    CHECK (pitchAt ({}, 1.0) == 0.0, "empty");
    std::vector<PitchPoint> q {{5.0, 40.0}, {-1.0, 3.0}};
    sanitizePitch (q, 2.0);
    CHECK (q.size () == 2 && q[0].src == 0.0 && q[1].src == 2.0 && q[1].semis == kMaxPitchEnvelope, "clamped");
}

TEST (clip_serialisation_roundtrip)
{
    auto l = sine (440.0, 0.5, 0.7);
    auto r = sine (660.0, 0.5, 1.4); // over full scale on purpose
    Clip c = clipFrom (l, r);
    c.name = "Capture 1";
    c.start = 12.345;
    c.markers.insert (c.markers.begin () + 1, {0.2, 0.35});
    c.pitch = {{0.1, 2.0}, {0.4, -5.5}};
    std::vector<uint8_t> blob;
    writeClip (c, blob);
    Clip d;
    CHECK (readClip (blob.data (), blob.size (), d), "read");
    CHECK (d.name == c.name && d.start == c.start, "name/start");
    CHECK (d.markers == c.markers, "markers");
    CHECK (d.pitch == c.pitch, "pitch");
    CHECK (d.audio && d.audio->length == c.audio->length && d.audio->numChannels == 2, "audio shape");
    double err = 0.0;
    for (int i = 0; i < c.audio->length; ++i)
        for (int ch = 0; ch < 2; ++ch)
            err = std::max (err, (double)std::fabs (d.audio->ch[ch][(size_t)i] - c.audio->ch[ch][(size_t)i]));
    CHECK (err < 1e-6 * 1.4 + 2e-7, "24-bit error %g", err);
    CHECK (!readClip (blob.data (), blob.size () - 10, d), "truncated blob rejected");
    blob[0] ^= 0xff;
    CHECK (!readClip (blob.data (), blob.size (), d), "bad magic rejected");

    Clip empty;
    blob.clear ();
    writeClip (empty, blob);
    Clip e;
    CHECK (readClip (blob.data (), blob.size (), e) && e.empty (), "empty clip");
}

TEST (every_algorithm_follows_the_speed)
{
    Clip c = clipFrom (sine (330.0, 2.0));
    for (int a = 0; a < kNumAlgorithms; ++a)
        for (double speed : {0.5, 1.0, 1.6})
        {
            RenderSettings s = algo (a);
            s.speed = speed;
            Rendered out;
            CHECK (render (c, s, out), "%s renders", kAlgoNames[a]);
            const double expect = 2.0 / speed * kSr;
            CHECK (std::fabs ((double)out.length () - expect) <= 2.0, "%s speed %.1f: %lld vs %.0f", kAlgoNames[a], speed,
                   out.length (), expect);
            CHECK (allFinite (out.l) && allFinite (out.r), "%s finite", kAlgoNames[a]);
            const size_t n = out.l.size ();
            const double level = 20.0 * std::log10 (rms (out.l, n / 4, 3 * n / 4) / (0.5 / std::sqrt (2.0)));
            CHECK (std::fabs (level) < 3.0, "%s speed %.1f level %.2f dB", kAlgoNames[a], speed, level);
        }
}

TEST (pitch_shift_is_accurate)
{
    Clip c = clipFrom (sine (220.0, 2.0));
    for (int a = 0; a < kNumAlgorithms; ++a)
    {
        if (a == kTape)
            continue;
        for (double semis : {7.0, -5.0})
        {
            RenderSettings s = algo (a);
            s.semis = semis;
            s.speed = 0.8;
            if (a == kAlien)
                s.windowMs = 200.0; // a grain's spectrum is as wide as 1 / its length: long grains to measure
            Rendered out;
            render (c, s, out);
            const double expect = 220.0 * std::pow (2.0, semis / 12.0);
            const size_t mid = out.l.size () / 2;
            // plain overlap-add modulates the waveform, so measure its spectral peak instead of periodicity
            const bool spectral = a == kExtreme || a == kWindowed || a == kAlien;
            const double f = a == kAlien ? averagedPeakFreq (out.l, out.l.size () / 8, 7 * out.l.size () / 8)
                             : spectral ? peakFreq (out.l, mid - 8192) : pitchOf (out.l, mid - 6000, mid + 6000);
            // Unaligned grains step the phase every hop, which can move a pure tone by up to half
            // the hop rate (hop = window / 2 = 30 ms): the known price of Simple Windowed.
            const double tolHz = a == kWindowed ? 0.5 / 0.030 + 1.0 : expect * (a == kExtreme || a == kAlien ? 0.02 : 0.01);
            CHECK (std::fabs (f - expect) < tolHz, "%s %+.0f st: %.2f Hz, expected %.2f", kAlgoNames[a], semis, f,
                   expect);
        }
    }
    // Tape: pitch follows the speed
    RenderSettings s = algo (kTape);
    s.speed = 1.5;
    s.semis = 12.0; // ignored
    Rendered out;
    render (c, s, out);
    const size_t mid = out.l.size () / 2;
    const double f = pitchOf (out.l, mid - 6000, mid + 6000);
    CHECK (std::fabs (f / 330.0 - 1.0) < 0.005, "tape %.2f Hz", f);
}

TEST (soloist_tracks_a_voice_like_source)
{
    Clip c = clipFrom (saw (150.0, 2.0));
    AnalysisCache cache;
    analysePitchMarks (*c.audio, cache);
    int voiced = 0;
    double periodSum = 0.0;
    for (const auto& m : cache.marks)
        if (m.voiced)
        {
            ++voiced;
            periodSum += m.period;
        }
    CHECK (voiced > 250, "voiced marks %d", voiced);
    CHECK (std::fabs (periodSum / std::max (voiced, 1) - kSr / 150.0) < 1.0, "period %.2f", periodSum / voiced);

    for (double semis : {5.0, -7.0})
    {
        RenderSettings s = algo (kSoloist);
        s.semis = semis;
        s.speed = 1.3;
        Rendered out;
        render (c, s, out);
        const size_t mid = out.l.size () / 2;
        const double f = pitchOf (out.l, mid - 6000, mid + 6000);
        const double expect = 150.0 * std::pow (2.0, semis / 12.0);
        CHECK (std::fabs (f / expect - 1.0) < 0.01, "%+.0f st: %.2f Hz, expected %.2f", semis, f, expect);
    }
    // PSOLA keeps the spectral envelope: an octave up moves the centroid far less than an octave.
    RenderSettings s0 = algo (kSoloist), s1 = algo (kSoloist), s2 = algo (kSoloist);
    s1.semis = 12.0;
    s2.formantSemis = 7.0;
    Rendered o0, o1, o2;
    render (c, s0, o0);
    render (c, s1, o1);
    render (c, s2, o2);
    const size_t mid = o0.l.size () / 2;
    const double c0 = centroid (o0.l, mid), c1 = centroid (o1.l, mid), c2 = centroid (o2.l, mid);
    CHECK (c1 / c0 < 1.5, "octave up centroid ratio %.2f", c1 / c0);
    CHECK (c2 / c0 > 1.2, "formant shift centroid ratio %.2f", c2 / c0);
    CHECK (std::fabs (pitchOf (o2.l, mid - 6000, mid + 6000) / 150.0 - 1.0) < 0.01, "formant shift keeps pitch");
}

TEST (soloist_moves_the_harmonics_by_whole_octaves)
{
    // An octave down must create the new fundamental (grains longer than two periods would keep
    // the old harmonics and leave the pitch where it was).
    Clip c = clipFrom (saw (150.0, 2.0));
    for (double semis : {-12.0, 12.0})
    {
        RenderSettings s = algo (kSoloist);
        s.semis = semis;
        Rendered out;
        render (c, s, out);
        const double f0 = 150.0 * std::pow (2.0, semis / 12.0);
        const size_t a = out.l.size () / 4;
        const double h1 = toneLevel (out.l, f0, a, 48000), h2 = toneLevel (out.l, 2.0 * f0, a, 48000);
        CHECK (h1 > 0.3 * h2 && h1 > 0.02, "%+.0f st: fundamental %.3f, 2nd harmonic %.3f", semis, h1, h2);
    }
}

TEST (polyphonic_preserve_formants_keeps_the_envelope)
{
    Clip c = clipFrom (saw (150.0, 2.0));
    RenderSettings plain = algo (kPolyphonic), keep = algo (kPolyphonic), up = algo (kPolyphonic);
    plain.semis = keep.semis = 7.0;
    keep.preserveFormants = true;
    up.formantSemis = 7.0;
    Rendered o0, o1, o2, o3;
    render (c, algo (kPolyphonic), o0);
    render (c, plain, o1);
    render (c, keep, o2);
    render (c, up, o3);
    const size_t mid = o0.l.size () / 2;
    const double c0 = centroid (o0.l, mid), c1 = centroid (o1.l, mid), c2 = centroid (o2.l, mid),
                 c3 = centroid (o3.l, mid);
    CHECK (c1 / c0 > 1.3, "plain shift moves the envelope %.2f", c1 / c0);
    CHECK (c2 / c0 < c1 / c0 - 0.2, "preserved %.2f vs plain %.2f", c2 / c0, c1 / c0);
    CHECK (c3 / c0 > 1.2, "formant shift alone %.2f", c3 / c0);
    CHECK (std::fabs (pitchOf (o3.l, mid - 6000, mid + 6000) / 150.0 - 1.0) < 0.01, "formant shift keeps pitch");
}

TEST (transients_land_where_the_time_map_puts_them)
{
    Clip c = clipFrom (clicks ({0.5, 1.2}, 2.0));
    c.markers.insert (c.markers.begin () + 1, {1.0, 1.6}); // src 1.0 s -> out 1.6 s (at speed 1)
    sanitizeMarkers (c.markers, 2.0);
    // expected output times: 0.5 * 1.6 = 0.8 s; 1.6 + 0.2 * (0.4 / 1.0) = 1.68 s; then speed 80 %
    const double speed = 0.8;
    const double e1 = 0.8 / speed, e2 = 1.68 / speed;
    for (int a = 0; a < kNumAlgorithms; ++a)
    {
        if (a == kExtreme || a == kAlien) // they smear a click by design
            continue;
        RenderSettings s = algo (a);
        s.speed = speed;
        Rendered out;
        render (c, s, out);
        // Grain-based modes smear a transient by up to one grain (60 ms); compressing can also
        // land it on a grain edge, so detect with a lower threshold.
        const bool grains = a == kWindowed || a == kBalanced;
        const float thr = a == kWindowed ? 0.03f : (grains ? 0.1f : 0.3f);
        const long long t1 = firstAbove (out.l, 0, thr);
        const long long t2 = firstAbove (out.l, (size_t)((e1 + 0.2) * kSr), thr);
        const double tol = grains ? 0.065 : (a == kPolyphonic ? 0.03 : 0.015);
        CHECK (t1 >= 0 && std::fabs (t1 / kSr - e1) < tol, "%s first click at %.4f s, expected %.4f", kAlgoNames[a],
               t1 / kSr, e1);
        CHECK (t2 >= 0 && std::fabs (t2 / kSr - e2) < tol, "%s second click at %.4f s, expected %.4f", kAlgoNames[a],
               t2 / kSr, e2);
    }
}

TEST (pitch_envelope_changes_pitch_over_time)
{
    Clip c = clipFrom (sine (200.0, 3.0));
    c.pitch = {{1.4, 0.0}, {1.6, 12.0}};
    for (int a : {kPolyphonic, kBalanced, kSoloist})
    {
        Rendered out;
        render (c, algo (a), out);
        const double f0 = pitchOf (out.l, (size_t)(0.5 * kSr), (size_t)(0.8 * kSr));
        const double f1 = pitchOf (out.l, (size_t)(2.2 * kSr), (size_t)(2.5 * kSr));
        CHECK (std::fabs (f0 / 200.0 - 1.0) < 0.01, "%s before %.2f", kAlgoNames[a], f0);
        CHECK (std::fabs (f1 / 400.0 - 1.0) < 0.01, "%s after %.2f", kAlgoNames[a], f1);
    }
}

TEST (renders_at_the_host_rate)
{
    Clip c = clipFrom (sine (440.0, 1.0, 0.5, 44100.0), {}, 44100.0);
    for (int a : {kPolyphonic, kTape, kSoloist})
    {
        Rendered out;
        render (c, algo (a), out, 48000.0);
        CHECK (std::fabs ((double)out.length () - 48000.0) <= 2.0, "%s length %lld", kAlgoNames[a], out.length ());
        const double f = pitchOf (out.l, 12000, 36000, 48000.0);
        CHECK (std::fabs (f / 440.0 - 1.0) < 0.005, "%s frequency %.2f", kAlgoNames[a], f);
        CHECK (out.sampleRate == 48000.0, "rate");
    }
}

TEST (extreme_stretches_far)
{
    Clip c = clipFrom (saw (220.0, 1.0));
    RenderSettings s = algo (kExtreme);
    s.speed = 0.05; // 20x
    s.smearMs = 400.0;
    Rendered out;
    const auto t0 = std::chrono::steady_clock::now ();
    CHECK (render (c, s, out), "render");
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    CHECK (std::fabs ((double)out.length () - 20.0 * kSr) <= 2.0, "length %lld", out.length ());
    CHECK (allFinite (out.l), "finite");
    const double level = 20.0 * std::log10 (rms (out.l, out.l.size () / 4, 3 * out.l.size () / 4) /
                                            rms (c.audio->ch[0], 0, (size_t)c.audio->length));
    CHECK (std::fabs (level) < 3.0, "level %.2f dB", level);
    std::printf ("    20 s of Extreme output rendered in %.2f s\n", secs);
}

TEST (stereo_wide_or_same)
{
    // Extreme and Alien: Wide gives left and right their own smear / cloud (unrelated); Same plays one
    // on both
    Clip c = clipFrom (sine (330.0, 2.0));
    for (int a : {kExtreme, kAlien})
        for (int mode : {kStereoWide, kStereoSame})
        {
            RenderSettings s = algo (a);
            s.stereo = mode;
            s.speed = 0.5;
            Rendered out;
            render (c, s, out);
            const size_t n = out.l.size ();
            double lr = 0.0, ll = 0.0, rr = 0.0;
            for (size_t i = n / 4; i < 3 * n / 4; ++i)
            {
                lr += (double)out.l[i] * out.r[i];
                ll += (double)out.l[i] * out.l[i];
                rr += (double)out.r[i] * out.r[i];
            }
            const double corr = lr / std::sqrt (std::max (1e-30, ll * rr));
            if (mode == kStereoSame)
                CHECK (corr > 0.999, "%s Same: one channel on both (%f)", kAlgoNames[a], corr);
            else
                CHECK (corr < 0.6, "%s Wide: left and right apart (%f)", kAlgoNames[a], corr);
        }
}

TEST (silence_stays_silent_and_edges_fade)
{
    Clip c = clipFrom (std::vector<float> ((size_t)kSr, 0.0f));
    for (int a = 0; a < kNumAlgorithms; ++a)
    {
        Rendered out;
        render (c, algo (a), out);
        CHECK (rms (out.l, 0, out.l.size ()) < 1e-6, "%s silent", kAlgoNames[a]);
    }
    Clip d = clipFrom (std::vector<float> ((size_t)kSr, 0.5f)); // DC: edges must fade
    Rendered out;
    render (d, algo (kTape), out);
    CHECK (std::fabs (out.l.front ()) < 1e-6 && std::fabs (out.l.back ()) < 1e-6, "edges %g %g", out.l.front (),
           out.l.back ());
}

TEST (gain_and_cancel)
{
    Clip c = clipFrom (sine (440.0, 1.0));
    RenderSettings s = algo (kTape);
    Rendered a, b;
    render (c, s, a);
    s.gainDb = -6.0;
    render (c, s, b);
    const double d = 20.0 * std::log10 (rms (b.l, 10000, 30000) / rms (a.l, 10000, 30000));
    CHECK (std::fabs (d + 6.0) < 0.01, "gain %.3f", d);

    AnalysisCache cache;
    int calls = 0;
    Rendered x;
    const bool done = renderClip (c, algo (kPolyphonic), kSr, x, cache, [&] (float) { return ++calls < 2; });
    CHECK (!done, "cancelled render reports false");
}

TEST (follow_tempo_resolves_speed)
{
    double p[kNumParams];
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    p[kSpeed] = 0.5;
    CHECK (settingsFromParams (p, 140.0).speed == 0.5, "speed param");
    p[kFollowTempo] = 1.0;
    p[kSourceBpm] = 100.0;
    CHECK (std::fabs (settingsFromParams (p, 140.0).speed - 1.4) < 1e-12, "follow tempo");
    p[kPitch] = 3.0;
    p[kFine] = -25.0;
    CHECK (std::fabs (settingsFromParams (p, 140.0).semis - 2.75) < 1e-12, "pitch + fine");
}

TEST (long_clip_render_time)
{
    // 60 s stereo, Polyphonic: must stay well inside interactive rates.
    std::vector<float> l = saw (110.0, 60.0, 0.2), r = saw (165.0, 60.0, 0.2);
    Clip c = clipFrom (std::move (l), std::move (r));
    RenderSettings s = algo (kPolyphonic);
    s.semis = 3.0;
    s.speed = 0.9;
    for (int a : {kPolyphonic, kSoloist, kBalanced})
    {
        s.algorithm = a;
        Rendered out;
        const auto t0 = std::chrono::steady_clock::now ();
        render (c, s, out);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
        std::printf ("    60 s stereo %s: %.2f s\n", kAlgoNames[a], secs);
        CHECK (secs < 20.0, "%s too slow: %.2f s", kAlgoNames[a], secs);
    }
}

TEST (wav_export_roundtrip)
{
    Clip c = clipFrom (sine (440.0, 0.5), sine (660.0, 0.5));
    Rendered out;
    render (c, algo (kTape), out);
    const auto path = (std::filesystem::temp_directory_path () / "stretchr_test.wav").string ();
    std::string err;
    CHECK (writeWav (path, out, err), "write: %s", err.c_str ());
    std::vector<float> l, r;
    int nch = 0;
    double sr = 0;
    CHECK (smemplr::decodeAudioFile (path, l, r, nch, sr, err), "decode: %s", err.c_str ());
    CHECK (nch == 2 && sr == kSr && l.size () == out.l.size (), "shape %d %f %zu", nch, sr, l.size ());
    bool same = l.size () == out.l.size ();
    for (size_t i = 0; same && i < l.size (); ++i)
        same = l[i] == out.l[i] && r[i] == out.r[i];
    CHECK (same, "samples identical");
    std::filesystem::remove (path);
}

static bool waitFor (const std::function<bool ()>& cond, double seconds = 10.0)
{
    const auto end = std::chrono::steady_clock::now () + std::chrono::duration<double> (seconds);
    while (!cond ())
    {
        if (std::chrono::steady_clock::now () > end)
            return false;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return true;
}

TEST (session_capture_records_the_transport)
{
    auto* s = new Session ();
    s->hostRate.store (kSr);
    s->setArmed (true);
    CHECK (s->captureState () == Session::kArmed, "armed");
    auto in = sine (300.0, 1.5);
    std::vector<float> inR (in.size ());
    for (size_t i = 0; i < in.size (); ++i)
        inR[i] = -in[i];
    const long long startPos = 96000; // 2 s into the project
    // nothing is recorded while stopped
    s->captureBlock (in.data (), inR.data (), 480, startPos, false, kSr);
    CHECK (s->captureState () == Session::kArmed, "still armed while stopped");
    // the worker allocates the capture buffers: wait for enough of them for 1.5 s, however busy the machine
    CHECK (waitFor ([&] { return s->captureChunksReady () >= 3; }), "capture buffers ready");
    for (size_t i = 0; i + 480 <= in.size (); i += 480)
        s->captureBlock (in.data () + i, inR.data () + i, 480, startPos + (long long)i, true, kSr);
    CHECK (s->captureState () == Session::kRecording, "recording");
    CHECK (std::fabs (s->capturedSeconds () - 1.5) < 0.011, "captured %.3f", s->capturedSeconds ());
    s->captureBlock (in.data (), inR.data (), 480, startPos + 72000, false, kSr); // transport stops
    CHECK (waitFor ([&] { return s->captureState () == Session::kIdle; }), "finished");
    Clip c = s->clip ();
    CHECK (!c.empty () && c.audio->length == 72000 && c.audio->numChannels == 2, "clip %d",
           c.empty () ? -1 : c.audio->length);
    CHECK (std::fabs (c.start - 2.0) < 1e-9, "start %.4f", c.start);
    CHECK (!c.empty () && c.audio->ch[0][1000] == in[1000] && c.audio->ch[1][1000] == inR[1000], "samples");
    CHECK (s->canUndo (), "capture is undoable");

    // the worker renders it for the host rate
    CHECK (s->waitUntilRendered (10.0), "rendered");
    RenderedPtr r = s->latest ();
    CHECK (r && std::fabs ((double)r->length () - 72000.0) <= 2.0, "render length %lld", r ? r->length () : -1);

    // a loop jump ends a recording
    s->setArmed (true);
    waitFor ([&] { return s->captureState () == Session::kArmed; });
    CHECK (waitFor ([&] { return s->captureChunksReady () >= 1; }), "capture buffers ready");
    for (int b = 0; b < 20; ++b)
        s->captureBlock (in.data (), inR.data (), 480, 480LL * b, true, kSr);
    s->captureBlock (in.data (), inR.data (), 480, 0, true, kSr); // jumped back
    CHECK (waitFor ([&] { return s->captureState () == Session::kIdle; }), "loop jump finishes");
    CHECK (s->clip ().audio->length == 9600 && s->clip ().start == 0.0, "second capture %d", s->clip ().audio->length);

    // stop requested while the host has stopped calling process
    s->setArmed (true);
    waitFor ([&] { return s->captureState () == Session::kArmed; });
    CHECK (waitFor ([&] { return s->captureChunksReady () >= 1; }), "capture buffers ready");
    s->processBegin ();
    for (int b = 0; b < 20; ++b)
        s->captureBlock (in.data (), inR.data (), 480, 480LL * b, true, kSr);
    s->processEnd ();
    s->setArmed (false);
    CHECK (waitFor ([&] { return s->captureState () == Session::kIdle; }), "watchdog stops the recording");
    s->release ();
}

TEST (session_rerenders_on_changes_and_undoes)
{
    auto* s = new Session ();
    s->hostRate.store (kSr);
    Clip c = clipFrom (sine (440.0, 1.0));
    s->setClip (c, false);
    CHECK (s->waitUntilRendered (10.0), "first render");
    CHECK (s->latest () && s->latest ()->length () == 48000, "length");
    s->setParam (kSpeed, 0.5);
    CHECK (!s->upToDate (), "stale after a parameter change");
    CHECK (s->waitUntilRendered (10.0), "rerender");
    CHECK (s->latest ()->length () == 96000, "speed applied %lld", s->latest ()->length ());

    s->edit ([] (Clip& k) { k.markers.back ().dst = 0.5; });
    CHECK (s->waitUntilRendered (10.0), "marker render");
    CHECK (s->latest ()->length () == 48000, "end marker applied %lld", s->latest ()->length ());
    s->setClipStart (3.0);
    CHECK (s->upToDate (), "moving the clip does not need a render");
    CHECK (s->undo () && s->clipStart () == 0.0, "undo start");
    CHECK (s->undo () && std::fabs (s->clip ().markers.back ().dst - 1.0) < 1e-12, "undo marker");
    CHECK (!s->canUndo (), "loaded clip starts the history");
    CHECK (s->redo () && std::fabs (s->clip ().markers.back ().dst - 0.5) < 1e-12, "redo");
    const uint64_t before = s->changeCount ();
    s->edit ([] (Clip& k) { k.pitch.push_back ({0.5, 3.0}); });
    CHECK (s->changeCount () > before, "edits count as changes");

    s->setClip ({}, true);
    CHECK (s->upToDate () && !s->hasClip (), "cleared");
    s->release ();
}

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& t : tests ())
    {
        if (filter && std::string (t.name).find (filter) == std::string::npos)
            continue;
        const int before = gFailures;
        std::printf ("%s\n", t.name);
        t.fn ();
        std::printf ("  %s\n", gFailures == before ? "ok" : "FAILED");
        ++ran;
    }
    std::printf ("\n%d tests, %d checks, %d failures\n", ran, gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

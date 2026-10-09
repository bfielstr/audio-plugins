// Headless tests for the Smemplr DSP core. Run: ./smemplr_tests [filter]
#include "pluginkit/testing/CpuClock.h"
#include "Engine.h"
#include "Fft.h"
#include "Filter.h"
#include "Interp.h"
#include "Modulation.h"
#include "Params.h"
#include "Rack.h"
#include "SampleData.h"
#include "Slices.h"

#include "dr_wav.h"
#include "pluginkit/CrashDump.h"
#include "pluginkit/SampleFiles.h"
#include "pluginkit/SettingsText.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace smemplr;

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

// ---------------------------------------------------------------------------
// helpers
constexpr double kHostSr = 48000.0;

static std::shared_ptr<SampleData> sine (double freq, double secs, double sr = 44100.0, bool stereo = false)
{
    std::vector<float> l ((size_t)(secs * sr)), r;
    for (size_t i = 0; i < l.size (); ++i)
        l[i] = 0.5f * (float)std::sin (2.0 * M_PI * freq * i / sr);
    if (stereo)
        r = l;
    return SampleData::fromBuffers (l, r, sr, "sine");
}

// Decaying noise bursts at the given times (seconds): a synthetic drum loop.
static std::shared_ptr<SampleData> bursts (const std::vector<double>& times, double secs, double sr = 44100.0)
{
    std::vector<float> l ((size_t)(secs * sr), 0.0f);
    uint32_t seed = 42;
    for (double t : times)
    {
        const size_t s = (size_t)(t * sr);
        for (size_t i = 0; i < (size_t)(0.12 * sr) && s + i < l.size (); ++i)
        {
            const float env = std::exp (-(float)i / (float)(0.02 * sr));
            l[s + i] += 0.8f * env * randomBipolar (seed);
        }
    }
    return SampleData::fromBuffers (l, {}, sr, "bursts");
}

struct Out
{
    std::vector<float> l, r;
};

static Out run (Engine& e, int frames, HostInfo host = {}, int block = 256)
{
    Out o;
    o.l.resize ((size_t)frames);
    o.r.resize ((size_t)frames);
    for (int pos = 0; pos < frames; pos += block)
    {
        const int n = std::min (block, frames - pos);
        e.render (o.l.data () + pos, o.r.data () + pos, n, host);
        host.ppq += n * host.bpm / 60.0 / kHostSr;
    }
    return o;
}

static Engine* makeEngine (std::shared_ptr<SampleData> s)
{
    auto* e = new Engine ();
    // the first slot's Smacheratr as it was up to 0.24 (its Gentlr off, 12 / 12): the tests measure the
    // sampler's own levels and timing through it
    e->setParam (slotBlockParam (0, smacheratr::kClarity), 0.0);
    e->setParam (slotBlockParam (0, smacheratr::kClaritySlope), 0.0);
    e->prepare (kHostSr, 512);
    e->setSample (s);
    e->setParam (kVolume, 0.0);
    e->setParam (kVelVol, 0.0);
    e->setParam (kLoopOn, 0.0); // looping is on by default; tests switch it on where they need it
    e->setParam (kVoices, 7);   // 8 voices (the default is 1): the tests play chords
    return e;
}

static double rms (const std::vector<float>& x, size_t a = 0, size_t b = SIZE_MAX)
{
    b = std::min (b, x.size ());
    if (b <= a)
        return 0.0;
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return std::sqrt (s / (double)(b - a));
}

static double peak (const std::vector<float>& x)
{
    double p = 0.0;
    for (float v : x)
        p = std::max (p, (double)std::fabs (v));
    return p;
}

static bool finite (const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}

// Frequency from zero crossings between [a, b)
static double freqOf (const std::vector<float>& x, size_t a, size_t b, double sr = kHostSr)
{
    int crossings = 0;
    size_t first = 0, last = 0;
    for (size_t i = a + 1; i < b && i < x.size (); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    if (crossings < 2)
        return 0.0;
    return (crossings - 1) * sr / (double)(last - first);
}

// Index after which the signal stays below `thr` (end of sound).
static size_t soundEnd (const std::vector<float>& x, float thr = 1e-3f)
{
    size_t last = 0;
    for (size_t i = 0; i < x.size (); ++i)
        if (std::fabs (x[i]) > thr)
            last = i;
    return last;
}

static size_t soundStart (const std::vector<float>& x, float thr = 1e-3f)
{
    for (size_t i = 0; i < x.size (); ++i)
        if (std::fabs (x[i]) > thr)
            return i;
    return x.size ();
}

// ---------------------------------------------------------------------------
TEST (params_roundtrip)
{
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        const auto& p = paramInfo (id);
        CHECK (p.id == id, "table order mismatch at %u (%s)", id, p.name);
        for (double n : {0.0, 0.25, 0.5, 0.75, 1.0})
        {
            const double plain = toPlain (id, n);
            CHECK (plain >= p.min - 1e-9 && plain <= p.max + 1e-9, "%s out of range", p.name);
            const double back = toPlain (id, toNormalized (id, plain));
            CHECK (std::fabs (back - plain) < 1e-6 * std::max (1.0, std::fabs (plain)), "%s: %f -> %f", p.name, plain,
                   back);
        }
        const double d = toPlain (id, defaultNormalized (id));
        CHECK (std::fabs (d - p.def) < 1e-6 * std::max (1.0, std::fabs (p.def)), "%s default %f vs %f", p.name, d,
               p.def);
        double parsed = 0;
        const std::string txt = toText (id, p.def);
        if (p.disp != Disp::Degrees)
        {
            CHECK (fromText (id, txt, parsed), "%s cannot parse '%s'", p.name, txt.c_str ());
            CHECK (std::fabs (parsed - p.def) <= std::max (0.051 * std::fabs (p.def), 0.02),
                   "%s parse '%s' -> %f (def %f)", p.name, txt.c_str (), parsed, p.def);
        }
    }
    CHECK (toText (kWarpBeats, 16) == "4 Bars", "%s", toText (kWarpBeats, 16).c_str ());
    CHECK (toText (kWarpBeats, 3) == "3 Beats", "%s", toText (kWarpBeats, 3).c_str ());
    CHECK (toText (kPan, -0.5) == "25L", "%s", toText (kPan, -0.5).c_str ());
    CHECK (toText (kFilterFreq, 22000) == "22.00 kHz", "%s", toText (kFilterFreq, 22000).c_str ());
}

TEST (fft_roundtrip)
{
    for (int n : {256, 2048, 4096})
    {
        Fft f (n);
        std::vector<float> x ((size_t)n), y ((size_t)n);
        uint32_t seed = 7;
        for (auto& v : x)
            v = randomBipolar (seed);
        std::vector<Fft::cf> s ((size_t)f.bins ());
        f.forward (x.data (), s.data ());
        f.inverse (s.data (), y.data ());
        double err = 0;
        for (int i = 0; i < n; ++i)
            err = std::max (err, (double)std::fabs (x[(size_t)i] - y[(size_t)i]));
        CHECK (err < 1e-4, "n=%d err=%g", n, err);
        // a pure tone lands in its bin
        for (int i = 0; i < n; ++i)
            x[(size_t)i] = (float)std::cos (2 * M_PI * 10 * i / n);
        f.forward (x.data (), s.data ());
        CHECK (std::fabs (std::abs (s[10]) - n / 2.0) < 1e-2 * n, "bin10=%f", std::abs (s[10]));
        CHECK (std::abs (s[11]) < 1e-2 * n, "bin11=%f", std::abs (s[11]));
    }
}

// The FFT Smemplr used before the split-array one (Fft.h): a plain radix-2 transform on std::complex,
// every stage reading the one twiddle table with a stride. Kept as the reference the new one must match.
class RefFft
{
public:
    using cf = std::complex<float>;
    explicit RefFft (int size)
    {
        n = size;
        m = n / 2;
        int bits = 0;
        while ((1 << bits) < m)
            ++bits;
        rev.resize ((size_t)m);
        for (int i = 0; i < m; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev[(size_t)i] = r;
        }
        tw.resize ((size_t)m / 2 + 1);
        for (int i = 0; i <= m / 2; ++i)
        {
            double a = -2.0 * M_PI * i / m;
            tw[(size_t)i] = cf ((float)std::cos (a), (float)std::sin (a));
        }
        split.resize ((size_t)m + 1);
        for (int k = 0; k <= m; ++k)
        {
            double a = -2.0 * M_PI * k / n;
            split[(size_t)k] = cf ((float)std::cos (a), (float)std::sin (a));
        }
        z.resize ((size_t)m);
    }
    void forward (const float* in, cf* out)
    {
        for (int i = 0; i < m; ++i)
            z[(size_t)rev[(size_t)i]] = cf (in[2 * i], in[2 * i + 1]);
        transform (false);
        const cf z0 = z[0];
        out[0] = cf (z0.real () + z0.imag (), 0.0f);
        out[m] = cf (z0.real () - z0.imag (), 0.0f);
        for (int k = 1; k < m; ++k)
        {
            const cf a = z[(size_t)k];
            const cf b = std::conj (z[(size_t)(m - k)]);
            const cf e = (a + b) * 0.5f;
            const cf o = (a - b) * cf (0.0f, -0.5f);
            out[k] = e + split[(size_t)k] * o;
        }
    }
    void inverse (const cf* in, float* out)
    {
        for (int k = 0; k < m; ++k)
        {
            const cf a = in[k];
            const cf b = std::conj (in[m - k]);
            const cf e = (a + b) * 0.5f;
            const cf o = (a - b) * 0.5f * std::conj (split[(size_t)k]);
            z[(size_t)rev[(size_t)k]] = e + cf (0.0f, 1.0f) * o;
        }
        transform (true);
        const float s = 1.0f / (float)m;
        for (int i = 0; i < m; ++i)
        {
            out[2 * i] = z[(size_t)i].real () * s;
            out[2 * i + 1] = z[(size_t)i].imag () * s;
        }
    }

private:
    void transform (bool inverseDir)
    {
        for (int len = 2; len <= m; len <<= 1)
        {
            const int half = len >> 1;
            const int step = m / len;
            for (int i = 0; i < m; i += len)
                for (int j = 0; j < half; ++j)
                {
                    cf w = tw[(size_t)(j * step)];
                    if (inverseDir)
                        w = std::conj (w);
                    const cf u = z[(size_t)(i + j)];
                    const cf v = z[(size_t)(i + j + half)] * w;
                    z[(size_t)(i + j)] = u + v;
                    z[(size_t)(i + j + half)] = u - v;
                }
        }
    }
    int n = 0, m = 0;
    std::vector<int> rev;
    std::vector<cf> tw, split, z;
};

TEST (fft_matches_reference)
{
    // the split-array FFT computes the plain radix-2 one's transform (on x86 to the bit; a compiler that
    // fuses multiply-adds may round a little differently), forward and inverse, at every size used
    double worstF = 0.0, worstI = 0.0;
    for (int n : {16, 256, 512, 1024, 2048, 4096})
    {
        Fft f (n);
        RefFft r (n);
        std::vector<float> x ((size_t)n), y1 ((size_t)n), y2 ((size_t)n);
        std::vector<Fft::cf> s1 ((size_t)f.bins ()), s2 ((size_t)f.bins ());
        uint32_t seed = 11;
        for (int round = 0; round < 4; ++round)
        {
            for (auto& v : x)
                v = randomBipolar (seed) * (round == 3 ? 1e-3f : 1.0f);
            f.forward (x.data (), s1.data ());
            r.forward (x.data (), s2.data ());
            double peakS = 0.0, dS = 0.0;
            for (int k = 0; k < f.bins (); ++k)
            {
                peakS = std::max (peakS, (double)std::abs (s2[(size_t)k]));
                dS = std::max (dS, (double)std::abs (s1[(size_t)k] - s2[(size_t)k]));
            }
            // the same spectrum back (the reference's, so only the inverse differs)
            f.inverse (s2.data (), y1.data ());
            r.inverse (s2.data (), y2.data ());
            double peakY = 0.0, dY = 0.0;
            for (int i = 0; i < n; ++i)
            {
                peakY = std::max (peakY, (double)std::fabs (y2[(size_t)i]));
                dY = std::max (dY, (double)std::fabs (y1[(size_t)i] - y2[(size_t)i]));
            }
            worstF = std::max (worstF, dS / peakS);
            worstI = std::max (worstI, dY / peakY);
        }
    }
    std::printf ("    largest difference: forward %.3g, inverse %.3g (of the peak)\n", worstF, worstI);
    CHECK (worstF < 1e-6 && worstI < 1e-6, "forward %g, inverse %g", worstF, worstI);
}

// readSinc / readSincRing as they were before the kernel's range check went (the table now goes on
// with zeros) and the taps' weights were worked out ahead of the sums
static void refReadSinc (const float* a, const float* b, int len, double pos, float cutoff, float& outA, float& outB)
{
    const auto& t = SincTable::get ();
    cutoff = std::clamp (cutoff, SincTable::kMinCutoff, 1.0f);
    const int base = (int)std::floor (pos);
    const float frac = (float)(pos - base);
    const int half = (int)std::ceil (SincTable::kHalf / cutoff);
    float sa = 0.0f, sb = 0.0f, wsum = 0.0f;
    for (int i = base - half + 1; i <= base + half; ++i)
    {
        const float w = t.at (std::fabs ((float)(i - base) - frac) * cutoff);
        wsum += w;
        if (i < 0 || i >= len)
            continue;
        sa += w * a[i];
        if (b)
            sb += w * b[i];
    }
    const float g = wsum > 1e-6f ? 1.0f / wsum : 0.0f;
    outA = sa * g;
    outB = b ? sb * g : outA;
}

TEST (sinc_reads_match_reference)
{
    std::vector<float> a (4096), b (4096);
    uint32_t seed = 3;
    for (size_t i = 0; i < a.size (); ++i)
    {
        a[i] = randomBipolar (seed);
        b[i] = randomBipolar (seed);
    }
    double worst = 0.0;
    int n = 0;
    for (float cutoff : {1.0f, 0.97f, 0.7f, 0.5f, 0.31f, 0.25f, 0.1f})
        for (int k = 0; k < 2000; ++k)
        {
            const double pos = -40.0 + 4176.0 * (0.5 + 0.5 * randomBipolar (seed)); // past both ends too
            float l1, r1, l2, r2, m1, m1r, m2, m2r;
            readSinc (a.data (), b.data (), (int)a.size (), pos, cutoff, l1, r1);
            refReadSinc (a.data (), b.data (), (int)a.size (), pos, cutoff, l2, r2);
            readSinc (a.data (), nullptr, (int)a.size (), pos, cutoff, m1, m1r);
            refReadSinc (a.data (), nullptr, (int)a.size (), pos, cutoff, m2, m2r);
            worst = std::max ({worst, (double)std::fabs (l1 - l2), (double)std::fabs (r1 - r2), (double)std::fabs (m1 - m2)});
            // the ring read: the same kernel round an absolute position of a power-of-two buffer
            const double rp = 9000.0 + 3000.0 * (0.5 + 0.5 * randomBipolar (seed));
            readSincRing (a.data (), b.data (), 4095, rp, cutoff, l1, r1);
            const long long base = (long long)std::floor (rp);
            std::vector<float> wa (128), wb (128);
            for (int j = 0; j < 128; ++j)
            {
                wa[(size_t)j] = a[(size_t)((base - 64 + j) & 4095)];
                wb[(size_t)j] = b[(size_t)((base - 64 + j) & 4095)];
            }
            refReadSinc (wa.data (), wb.data (), 128, rp - (double)(base - 64), cutoff, l2, r2);
            worst = std::max ({worst, (double)std::fabs (l1 - l2), (double)std::fabs (r1 - r2)});
            ++n;
        }
    std::printf ("    %d reads, largest difference %.3g\n", n, worst);
    CHECK (worst < 1e-6, "%g", worst);
}

static double toneAmp (const std::vector<float>& x, double f, size_t a, size_t b);

// Loads an effect into a rack slot with its own defaults, on; sets one of its values (plain).
static void loadFx (Engine& e, int slot, int type)
{
    e.setParam (slotParam (slot, kSlotType), (double)type);
    e.setParam (slotParam (slot, kSlotOn), 1.0);
    const auto& t = fxBlockTable (type);
    for (uint32_t j = 0; j < kSlotBlockAll; ++j)
        e.setParam (slotBlockParam (slot, j), j < t.size () ? t.defaultNormalized (j) : 0.0);
}
static void setFx (Engine& e, int slot, uint32_t id, double plain)
{
    e.setParam (slotBlockParam (slot, (uint32_t)fxBlockOf (e.rackType (slot), id)), fxTable (e.rackType (slot)).toNormalized (id, plain));
}

TEST (rack_effects)
{
    // a 440 Hz sample; Para's notch between its filters lands on it, and stays put with the note
    // (after a new Smemplr's Smacheratr in the first slot: the effects here go in slots 2 and 3)
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    const int base = e->latency ();
    CHECK (e->rackType (0) == kFxSmacheratr && base > 0, "the first slot's Smacheratr reports its latency: %d", base);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 24000);
    const double dry = rms (o.l, 12000, 24000);
    loadFx (*e, 1, kFxPara);
    setFx (*e, 1, para::kHpFreq, 700.0);
    setFx (*e, 1, para::kLpFreq, 275.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 24000);
    const double notched = rms (o.l, 12000, 24000);
    CHECK (notched < dry * 0.4, "notch on the sample: %f vs %f", notched, dry);
    // the same effect twice: two notches are deeper than one
    loadFx (*e, 2, kFxPara);
    setFx (*e, 2, para::kHpFreq, 700.0);
    setFx (*e, 2, para::kLpFreq, 275.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 24000);
    CHECK (rms (o.l, 12000, 24000) < notched * 0.5, "two Paras: %f vs %f", rms (o.l, 12000, 24000), notched);
    // off: the slot passes the sound
    e->setParam (slotParam (1, kSlotOn), 0.0);
    e->setParam (slotParam (2, kSlotOn), 0.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 24000);
    CHECK (std::fabs (rms (o.l, 12000, 24000) / dry - 1.0) < 0.02, "both off: untouched (%f vs %f)", rms (o.l, 12000, 24000), dry);
    // Multidyn in the second slot instead: its preset lifts a quiet sample, and its latency counts
    e->setParam (slotParam (2, kSlotType), (double)kFxEmpty);
    loadFx (*e, 1, kFxMultidyn);
    CHECK (e->latency () > base, "Multidyn's look-ahead is reported: %d", e->latency ());
    e->setParam (kGain, -30.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 48000);
    const double lifted = rms (o.l, 24000, 48000);
    e->setParam (slotParam (1, kSlotType), (double)kFxEmpty);
    CHECK (e->latency () == base, "taken out: back to %d (%d)", base, e->latency ());
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 48000);
    CHECK (lifted > rms (o.l, 24000, 48000) * 2.0, "upward compression lifts it: %f vs %f", lifted, rms (o.l, 24000, 48000));
}

TEST (rack_smoothr)
{
    // Smoothr in a slot: its latency counts, on or off; it holds a loud sample under its ceiling; its
    // own saturator stays off (a Smacheratr slot before it does that)
    auto s = sine (110.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (slotParam (0, kSlotType), (double)kFxEmpty);
    const int base = e->latency ();
    loadFx (*e, 0, kFxSmoothr);
    smoothr::Engine alone;
    alone.prepare (kHostSr, 512);
    alone.setParam (smoothr::kTailBase + pk::kTailOn, 0.0);
    CHECK (e->latency () == base + alone.latency (), "its latency is reported: %d (%d + %d)", e->latency (), base, alone.latency ());
    e->setParam (slotParam (0, kSlotOn), 0.0);
    CHECK (e->latency () == base + alone.latency (), "off, the same: %d", e->latency ());
    e->setParam (slotParam (0, kSlotOn), 1.0);
    e->setParam (kGain, 18.0);
    setFx (*e, 0, smoothr::kCeiling, -6.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    auto o = run (*e, 48000);
    const double pk = std::max (peak (o.l), peak (o.r));
    CHECK (pk <= std::pow (10.0, -6.0 / 20.0) * 1.02, "held under -6 dB: peak %.2f dB", 20.0 * std::log10 (pk));
    // off: the sound passes, delayed by the same latency (so it lines up with the sound on)
    e->setParam (slotParam (0, kSlotOn), 0.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 48000);
    CHECK (std::max (peak (o.l), peak (o.r)) > 1.0, "off: not limited (%.2f)", std::max (peak (o.l), peak (o.r)));
    CHECK (finite (o.l) && finite (o.r), "finite");
    // the hidden saturator: every Smoothr tail parameter is listed as not shown on the page
    const auto& hidden = rackHiddenParams (kFxSmoothr);
    for (uint32_t id = 0; id < smoothr::kNumParams; ++id)
    {
        bool listed = false;
        for (const auto& h : hidden)
            listed |= id >= h.first && id <= h.last;
        CHECK (listed == smoothr::isTailParam (id), "smoothr %u hidden: %d", id, listed);
    }
}

TEST (rack_gentlr)
{
    // Gentlr in a slot: its latency counts, on or off; a band on the sample's pitch turns a loud note
    // down; off, the slot is dry (same latency)
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (slotParam (0, kSlotType), (double)kFxEmpty);
    const int base = e->latency ();
    e->setParam (kGain, 12.0);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 48000);
    const double dry = rms (o.l, 24000, 48000);
    loadFx (*e, 0, kFxGentlr);
    gentlr::Engine alone (false);
    alone.prepare (kHostSr, 512);
    CHECK (e->latency () == base + alone.latency (), "its latency is reported: %d (%d + %d)", e->latency (), base, alone.latency ());
    setFx (*e, 0, gentlr::bandParam (0, gentlr::kFreq), 440.0);
    setFx (*e, 0, gentlr::bandParam (0, gentlr::kRange), 12.0);
    setFx (*e, 0, gentlr::bandParam (1, gentlr::kOn), 0.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 48000);
    const double cut = rms (o.l, 24000, 48000);
    CHECK (cut < dry * 0.7, "the band turns it down: %.2f dB", 20.0 * std::log10 (cut / dry));
    e->setParam (slotParam (0, kSlotOn), 0.0);
    CHECK (e->latency () == base + alone.latency (), "off, the same latency: %d", e->latency ());
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 48000);
    CHECK (std::fabs (rms (o.l, 24000, 48000) / dry - 1.0) < 0.01, "off: dry (%.3f)", rms (o.l, 24000, 48000) / dry);
}

TEST (rack_levlr_bands_and_drives)
{
    // Levlr's latency (its drives' oversampling) counts, on or off; an old state's Levlr slot gets 4
    // bands and no drive (its Bands read 0 there: 1 band)
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (slotParam (0, kSlotType), (double)kFxEmpty);
    const int base = e->latency ();
    loadFx (*e, 0, kFxLevlr);
    levlr::Engine alone (false);
    alone.prepare (kHostSr, 512);
    CHECK (alone.latency () > 0 && e->latency () == base + alone.latency (), "reported: %d (%d + %d)", e->latency (), base, alone.latency ());
    e->setParam (slotParam (0, kSlotOn), 0.0);
    CHECK (e->latency () == base + alone.latency (), "off, the same: %d", e->latency ());
    auto st = std::make_unique<std::array<double, kNumParams>> ();
    auto has = std::make_unique<std::array<bool, kNumParams>> ();
    st->fill (0.0);
    has->fill (false);
    (*st)[slotParam (2, kSlotType)] = toNormalized (slotParam (2, kSlotType), (double)kFxLevlr);
    (*has)[slotParam (2, kSlotType)] = true;
    migrateLevlrInSlots (*st, *has, 12);
    CHECK ((*st)[slotBlockParam (2, levlr::kBandCount)] == 1.0 && (*st)[slotBlockParam (2, levlr::driveParam (1, levlr::kDriveDb))] == 0.0,
           "4 bands, no drive");
    (*st)[slotBlockParam (2, levlr::kBandCount)] = 0.0;
    migrateLevlrInSlots (*st, *has, 13);
    CHECK ((*st)[slotBlockParam (2, levlr::kBandCount)] == 0.0, "a new state is left as it is");
}

TEST (old_para_slots_keep_their_slope_and_drive)
{
    // a Para slot saved before version 13: its slope was one of 12 / 18 / 24 dB (normalized 0 / 0.5 / 1),
    // its one drive now both filters'
    auto st = std::make_unique<std::array<double, kNumParams>> ();
    auto has = std::make_unique<std::array<bool, kNumParams>> ();
    st->fill (0.0);
    has->fill (false);
    auto put = [&] (uint32_t id, double v) {
        (*st)[id] = v;
        (*has)[id] = true;
    };
    put (slotParam (1, kSlotType), toNormalized (slotParam (1, kSlotType), (double)kFxPara));
    put (slotBlockParam (1, para::kHpSlope), 0.5); // 18 dB
    put (slotBlockParam (1, para::kHpDriveOn), 1.0);
    put (slotBlockParam (1, para::kHpDrive), para::toNormalized (para::kHpDrive, 9.0));
    put (slotBlockParam (1, para::kFade), std::log (12.0) / std::log (36.0)); // 12 semitones over 1 .. 36
    migrateParaInSlots (*st, *has, 12);
    CHECK (std::lround (para::toPlain (para::kHpSlope, (*st)[slotBlockParam (1, para::kHpSlope)])) == para::kSlope18, "18 dB stays 18 dB");
    CHECK ((*st)[slotBlockParam (1, para::kLpDriveOn)] == 1.0 &&
               std::fabs (para::toPlain (para::kLpDrive, (*st)[slotBlockParam (1, para::kLpDrive)]) - 9.0) < 1e-9,
           "the low-pass drive is the old drive");
    // and on through version 15's: the low-pass at the slot's slope too, the high-pass locked (0 dB, never
    // saved: the default), Fade 12 semitones on the new range
    CHECK (std::lround (para::toPlain (para::kLpSlope, (*st)[slotBlockParam (1, para::kLpSlope)])) == para::kSlope18 &&
               (*st)[slotBlockParam (1, para::kHpGainLock)] == 1.0 && (*st)[slotBlockParam (1, para::kLpGainLock)] == 0.0 &&
               std::fabs (para::toPlain (para::kFade, (*st)[slotBlockParam (1, para::kFade)]) - 12.0) < 1e-9,
           "the chain through 15: LP slope %f, Fade %f", (*st)[slotBlockParam (1, para::kLpSlope)],
           para::toPlain (para::kFade, (*st)[slotBlockParam (1, para::kFade)]));
    const auto now = *st;
    migrateParaInSlots (*st, *has, 15);
    CHECK (*st == now, "a new state is left as it is");
}

TEST (para_slots_separate_slopes_and_gain_locks)
{
    // Para's Low-Pass Slope and Gain Locks (IDs 62 .. 64, after its block of 62) sit in the slot's extension,
    // at block positions 62 .. 64 (Para's IDs), and map back
    CHECK (para::kNumParams == 76 && para::kNumParams > kSlotBlock && para::kNumParams <= kSlotBlockAll, "Para has %u parameters",
           (unsigned)para::kNumParams);
    for (uint32_t id : {para::kLpSlope, para::kHpGainLock, para::kLpGainLock})
    {
        const int64_t j = fxBlockOf (kFxPara, id);
        CHECK (j == (int64_t)id && j >= (int64_t)kSlotBlock && fxIdAt (kFxPara, (uint32_t)j) == (int64_t)id, "para %u at %lld", id, (long long)j);
        CHECK (slotBlockParam (3, (uint32_t)j) == kRackExtBase + 3 * kSlotExt + (id - kSlotBlock), "in slot 3's extension");
        CHECK (std::string (fxBlockTable (kFxPara).info ((uint32_t)j).name) == para::paramTable ().info (id).name &&
                   fxBlockTable (kFxPara).info ((uint32_t)j).def == para::paramTable ().info (id).def,
               "%s", para::paramTable ().info (id).name);
    }
    CHECK (fxIdAt (kFxPara, para::kNumParams) == -1 && fxBlockOf (kFxPara, para::kNumParams) == -1, "nothing after Para's last");
    // the old fixed Para's Fade (0.5) keeps its range of then, so its saved values keep their meaning
    CHECK (paramTable ().info (kParaFade).max == 36.0 && paramTable ().info (kParaFade).def == 12.0, "Old Para Vocal Fade 1 .. 36, 12");
    // they reach the slot's Para: the high-pass alone (the low-pass at -inf, the high-pass at 20 Hz: all of a
    // 440 Hz sample) at +6 dB plays at 0 dB locked (a new slot's default), at +6 dB unlocked
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (slotParam (0, kSlotType), (double)kFxEmpty);
    auto level = [&] () {
        e->reset ();
        e->noteOn (60, 1.0f);
        const auto o = run (*e, 24000);
        return rms (o.l, 12000, 24000);
    };
    const double dry = level ();
    loadFx (*e, 1, kFxPara);
    setFx (*e, 1, para::kHpFreq, 20.0);
    setFx (*e, 1, para::kLpGain, para::kGainMinDb);
    setFx (*e, 1, para::kHpGain, 6.0);
    const double locked = level ();
    setFx (*e, 1, para::kHpGainLock, 0.0);
    const double unlocked = level ();
    CHECK (std::fabs (20.0 * std::log10 (locked / dry)) < 0.3 && std::fabs (20.0 * std::log10 (unlocked / dry) - 6.0) < 0.3,
           "locked %f dB, unlocked %f dB", 20.0 * std::log10 (locked / dry), 20.0 * std::log10 (unlocked / dry));
    // the low-pass's own slope: alone (the high-pass at -inf) at 110 Hz, at 6 dB it lets more of 440 Hz through than at 96
    setFx (*e, 1, para::kHpGain, para::kGainMinDb);
    setFx (*e, 1, para::kLpGain, 0.0);
    setFx (*e, 1, para::kLpFreq, 110.0);
    setFx (*e, 1, para::kLpSlope, para::kSlope6);
    const double gentle = level ();
    setFx (*e, 1, para::kLpSlope, para::kSlope96);
    const double steep = level ();
    CHECK (20.0 * std::log10 (gentle / dry) > -15.0 && 20.0 * std::log10 (steep / dry) < -80.0, "LP 6 dB %f dB, 96 dB %f dB at two octaves",
           20.0 * std::log10 (gentle / dry), 20.0 * std::log10 (steep / dry));

    // a Para slot saved in version 13 or 14: one slope (Brickwall), the high-pass at +6 dB, Fade 30 semitones
    // over 1 .. 36: the low-pass gets Brickwall, the high-pass stays unlocked (nothing gets quieter), the
    // low-pass unlocked, Fade 30 semitones on the new range
    for (int version : {13, 14})
    {
        auto st = std::make_unique<std::array<double, kNumParams>> ();
        auto has = std::make_unique<std::array<bool, kNumParams>> ();
        st->fill (0.0);
        has->fill (false);
        auto put = [&] (uint32_t id, double v) {
            (*st)[id] = v;
            (*has)[id] = true;
        };
        put (slotParam (2, kSlotType), toNormalized (slotParam (2, kSlotType), (double)kFxPara));
        put (slotBlockParam (2, para::kHpSlope), para::toNormalized (para::kHpSlope, para::kSlopeBrickwall));
        put (slotBlockParam (2, para::kHpGain), para::toNormalized (para::kHpGain, 6.0));
        put (slotBlockParam (2, para::kFade), std::log (30.0) / std::log (36.0));
        put (slotBlockParam (2, para::kHpDriveOn), 1.0);
        // a slot of another kind is left alone
        put (slotParam (3, kSlotType), toNormalized (slotParam (3, kSlotType), (double)kFxMsEq));
        put (slotBlockParam (3, para::kFade), 0.25);
        migrateParaInSlots (*st, *has, version);
        CHECK (std::lround (para::toPlain (para::kLpSlope, (*st)[slotBlockParam (2, para::kLpSlope)])) == para::kSlopeBrickwall &&
                   std::lround (para::toPlain (para::kHpSlope, (*st)[slotBlockParam (2, para::kHpSlope)])) == para::kSlopeBrickwall,
               "version %d: Brickwall, both", version);
        CHECK ((*st)[slotBlockParam (2, para::kHpGainLock)] == 0.0 && (*st)[slotBlockParam (2, para::kLpGainLock)] == 0.0 &&
                   (*has)[slotBlockParam (2, para::kHpGainLock)] && (*has)[slotBlockParam (2, para::kLpGainLock)],
               "version %d: the boosted high-pass unlocked", version);
        CHECK (std::fabs (para::toPlain (para::kFade, (*st)[slotBlockParam (2, para::kFade)]) - 30.0) < 1e-9,
               "version %d: Fade 30 st: %f", version, para::toPlain (para::kFade, (*st)[slotBlockParam (2, para::kFade)]));
        CHECK ((*st)[slotBlockParam (2, para::kLpDriveOn)] == 0.0 && !(*has)[slotBlockParam (2, para::kLpDriveOn)],
               "version %d: the per-band drive of 13 is not redone", version);
        CHECK ((*st)[slotBlockParam (3, para::kFade)] == 0.25 && !(*has)[slotBlockParam (3, para::kHpGainLock)], "the M/S EQ slot untouched");
        // at 0 dB the high-pass is locked
        put (slotBlockParam (2, para::kHpGain), para::toNormalized (para::kHpGain, 0.0));
        put (slotBlockParam (2, para::kFade), std::log (30.0) / std::log (36.0));
        migrateParaInSlots (*st, *has, version);
        CHECK ((*st)[slotBlockParam (2, para::kHpGainLock)] == 1.0, "version %d: 0 dB locked", version);
    }
}

TEST (rack_multidyn_later_params)
{
    // Slope, Soften Color and the Sub band sit in the slot's extension, in order, and work there (its
    // saturator's fourth block after them is not in the rack)
    for (uint32_t id = multidyn::kSatExt3Base; id < multidyn::kNumParams; ++id)
        CHECK (fxBlockOf (kFxMultidyn, id) == -1, "multidyn %u (its saturator's) is not in the rack", id);
    for (uint32_t id = multidyn::kXoverSlope; id < multidyn::kSatExt3Base; ++id)
        CHECK (fxBlockOf (kFxMultidyn, id) == (int64_t)(kSlotBlock + id - multidyn::kXoverSlope) &&
                   fxBlockTable (kFxMultidyn).info ((uint32_t)fxBlockOf (kFxMultidyn, id)).def == multidyn::paramTable ().info (id).def,
               "multidyn %u", id);
    auto s = sine (35.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (slotParam (0, kSlotType), (double)kFxEmpty);
    loadFx (*e, 0, kFxMultidyn);
    CHECK (std::lround (fxTable (kFxMultidyn).toPlain (multidyn::kXoverSlope, e->param (slotBlockParam (0, (uint32_t)fxBlockOf (kFxMultidyn, multidyn::kXoverSlope))))) == 3,
           "24 dB by default");
    // (the Sub band takes the sample from the low band: against itself with its threshold at the top)
    e->setParam (kGain, 6.0);
    setFx (*e, 0, multidyn::kSubOn, 1.0);
    setFx (*e, 0, multidyn::kSubThresh, 0.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    auto o = run (*e, 48000);
    const double without = rms (o.l, 24000, 48000);
    setFx (*e, 0, multidyn::kSubThresh, -40.0);
    setFx (*e, 0, multidyn::kSubRatio, 8.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 48000);
    CHECK (rms (o.l, 24000, 48000) < without * 0.7, "the Sub band compresses a 35 Hz sample: %.2f dB",
           20.0 * std::log10 (rms (o.l, 24000, 48000) / without));
    // an old state's Multidyn slot: its extension read 0 (6 dB slope); it gets the defaults, and its
    // band Outputs move by the change of the baked gains
    auto st = std::make_unique<std::array<double, kNumParams>> ();
    auto has = std::make_unique<std::array<bool, kNumParams>> ();
    st->fill (0.0);
    has->fill (false);
    (*st)[slotParam (3, kSlotType)] = toNormalized (slotParam (3, kSlotType), (double)kFxMultidyn);
    (*has)[slotParam (3, kSlotType)] = true;
    const auto& md = multidyn::paramTable ();
    for (uint32_t id = 0; id < multidyn::kXoverSlope; ++id)
        if (const int64_t j = fxBlockOf (kFxMultidyn, id); j >= 0)
        {
            (*st)[slotBlockParam (3, (uint32_t)j)] = md.defaultNormalized (id);
            (*has)[slotBlockParam (3, (uint32_t)j)] = true;
        }
    const uint32_t out1 = slotBlockParam (3, multidyn::bandParam (1, multidyn::kBandOutput));
    const double before = md.toPlain (multidyn::bandParam (1, multidyn::kBandOutput), (*st)[out1]);
    migrateMultidynInSlots (*st, *has, 12);
    const double after = md.toPlain (multidyn::bandParam (1, multidyn::kBandOutput), (*st)[out1]);
    CHECK (std::fabs (after - before - multidyn::oldBakedShiftDb (multidyn::bandParam (1, multidyn::kBandOutput))) < 1e-6,
           "band 2's Output moved by %.2f dB", after - before);
    const uint32_t slope = slotBlockParam (3, (uint32_t)fxBlockOf (kFxMultidyn, multidyn::kXoverSlope));
    CHECK ((*st)[slope] == md.defaultNormalized (multidyn::kXoverSlope) && (*has)[slope], "the slope's default (24 dB)");
    // Style: an old slot (before 14) keeps Multidyn's own sound, Character; a new slot is OTT
    const uint32_t style = slotBlockParam (3, (uint32_t)fxBlockOf (kFxMultidyn, multidyn::kStyle));
    CHECK (std::lround (md.toPlain (multidyn::kStyle, (*st)[style])) == multidyn::kStyleCharacter && (*has)[style], "version 12: Character");
    (*st)[style] = md.defaultNormalized (multidyn::kStyle);
    migrateMultidynInSlots (*st, *has, 13);
    CHECK (std::lround (md.toPlain (multidyn::kStyle, (*st)[style])) == multidyn::kStyleCharacter, "version 13: Character");
    (*st)[style] = md.defaultNormalized (multidyn::kStyle);
    migrateMultidynInSlots (*st, *has, 14);
    CHECK (std::lround (md.toPlain (multidyn::kStyle, (*st)[style])) == multidyn::kStyleOtt, "version 14: as saved");
    CHECK (md.info (multidyn::kStyle).def == multidyn::kStyleOtt, "a new slot: OTT");
    // Sub Input: an old slot (before 16) read 0 there (-24 dB) and gets 0 dB; a version 16 slot keeps it
    const uint32_t subIn = slotBlockParam (3, (uint32_t)fxBlockOf (kFxMultidyn, multidyn::kSubInput));
    (*st)[subIn] = 0.0;
    migrateMultidynInSlots (*st, *has, 15);
    CHECK (std::fabs (md.toPlain (multidyn::kSubInput, (*st)[subIn])) < 1e-9 && (*has)[subIn], "version 15: Sub Input 0 dB");
    (*st)[subIn] = 0.0;
    migrateMultidynInSlots (*st, *has, 16);
    CHECK ((*st)[subIn] == 0.0, "version 16: as saved");
}

TEST (settings_text_roundtrip)
{
    // the text Copy Settings puts on the clipboard (a plug-in's menu, or a rack page), read back
    pk::SettingValues v {{0, 0.25}, {3, 1.0}, {17, 0.123456789012345678}};
    const std::string t = pk::settingsToText ("Para", v, &para::paramTable ());
    pk::SettingValues back;
    CHECK (pk::settingsFromText (t, "para", back) && back == v, "round trip (the effect's name in any case)");
    CHECK (!pk::settingsFromText (t, "multidyn", back), "another effect's settings are refused");
    CHECK (!pk::settingsFromText ("hello", "para", back), "not settings");
    CHECK (pk::settingsEffect (t) == "Para", "the effect: %s", pk::settingsEffect (t).c_str ());
    CHECK (t.find ("High-Pass") != std::string::npos, "names for the reader");
    // Gentlr was called Gently: settings copied from a Gently (its plug-in or a rack slot, same IDs)
    // still paste into Gentlr, in both places
    const std::string old = pk::settingsToText ("Gently", v, &gentlr::paramTable ());
    CHECK (pk::settingsFromText (old, "Gentlr", back, "Gently") && back == v, "the plug-in takes Gently's settings");
    CHECK (pk::settingsFromText (old, fxName (kFxGentlr), back, fxFormerName (kFxGentlr)) && back == v, "a rack slot takes them");
    CHECK (!pk::settingsFromText (old, "Gentlr", back), "only with the former name given");
    CHECK (!pk::settingsFromText (old, fxName (kFxPara), back, fxFormerName (kFxPara)), "no other kind takes them");
    const std::string now = pk::settingsToText ("Gentlr", v, &gentlr::paramTable ());
    CHECK (pk::settingsFromText (now, fxName (kFxGentlr), back, fxFormerName (kFxGentlr)) && back == v, "and its new name");
}

TEST (old_slot_types_keep_their_effect)
{
    // the slot type was stored normalized over the kinds there were: a state from before Gentlr and
    // Smoothr (version 12: 8 kinds) keeps Levlr as Levlr on the longer list (StateIO.cpp does it; here
    // the arithmetic it relies on)
    const uint32_t typeId = slotParam (0, kSlotType);
    const double old = (double)kFxLevlr / (kFxTypesBeforeGentlr - 1);
    CHECK (std::lround (toPlain (typeId, toNormalized (typeId, std::round (old * (kFxTypesBeforeGentlr - 1))))) == kFxLevlr, "Levlr");
    CHECK (std::lround (toPlain (typeId, 1.0)) == kNumFxTypes - 1 && kFxSmoothr == kNumFxTypes - 1, "Smoothr last");
}

TEST (rack_block_mapping)
{
    // every block position maps to one effect parameter and back; Multidyn's RMS Window and Soften
    // sit where its (unused) saturator's first two are
    for (int type = kFxPara; type < kNumFxTypes; ++type)
        for (uint32_t j = 0; j < kSlotBlockAll; ++j)
        {
            const int64_t id = fxIdAt (type, j);
            if (id >= 0)
                CHECK (fxBlockOf (type, (uint32_t)id) == (int64_t)j, "type %d block %u", type, j);
        }
    for (uint32_t id = 0; id < multidyn::kNumParams; ++id)
    {
        const bool sat = (id >= multidyn::kSatOn && id <= multidyn::kSatPreLimitThreshold) ||
                         (id >= multidyn::kSatExtBase && id < multidyn::kXoverSlope) || id >= multidyn::kSatExt3Base;
        CHECK ((fxBlockOf (kFxMultidyn, id) < 0) == sat, "multidyn %u", id);
    }
    const auto& t = fxBlockTable (kFxMultidyn);
    CHECK (t.info (multidyn::kSatOn).def == 50.0 && t.info (multidyn::kSatPreLimit).def == 0.5, "RMS Window, Soften defaults");
    // and the rack's Multidyn runs with them
    auto e = std::make_unique<Engine> ();
    e->prepare (48000.0, 256);
    loadFx (*e, 0, kFxMultidyn);
    setFx (*e, 0, multidyn::kSoften, 1.0);
    setFx (*e, 0, multidyn::kRmsWindow, 80.0);
    CHECK (std::fabs (e->param (slotBlockParam (0, multidyn::kSatPreLimit)) - 1.0) < 1e-9, "Soften stored in its place");
}

TEST (gentlr_sub_band_in_slots)
{
    // Gentlr's Sub band parameters have places in a Smacheratr slot's block, and keep their values there
    static_assert (smacheratr::kNumParams <= kSlotBlock, "Smacheratr's block");
    auto e = std::make_unique<Engine> ();
    e->prepare (48000.0, 256);
    loadFx (*e, 1, kFxSmacheratr);
    const uint32_t ids[] = {smacheratr::kClaritySub, smacheratr::kClaritySubFreq, smacheratr::kClaritySubRange, smacheratr::kClaritySubThreshold};
    const double vals[] = {1.0, 60.0, 15.0, -30.0};
    for (int k = 0; k < 4; ++k)
    {
        CHECK (fxBlockOf (kFxSmacheratr, ids[k]) == (int64_t)ids[k] && fxIdAt (kFxSmacheratr, ids[k]) == (int64_t)ids[k], "block place of %u", ids[k]);
        setFx (*e, 1, ids[k], vals[k]);
    }
    for (int k = 0; k < 4; ++k)
    {
        const double got = smacheratr::paramTable ().toPlain (ids[k], e->param (slotBlockParam (1, ids[k])));
        CHECK (std::fabs (got - vals[k]) < 1e-6, "Sub value %u: %f", ids[k], got);
    }
    // a new slot has Sub off with its defaults
    loadFx (*e, 2, kFxSmacheratr);
    CHECK (e->param (slotBlockParam (2, smacheratr::kClaritySub)) == smacheratr::defaultNormalized (smacheratr::kClaritySub) &&
               smacheratr::toPlain (smacheratr::kClaritySub, e->param (slotBlockParam (2, smacheratr::kClaritySub))) == 0.0,
           "a new slot: Sub off");

    // a state from before the Sub band (versions 11 and 10 too): its Smacheratr slot's Sub places held
    // zeros (or nothing); they load as Sub off with the defaults, other effects' places are untouched
    for (int version : {11, 10})
    {
        std::array<double, kNumParams> norm {};
        std::array<bool, kNumParams> has {};
        for (uint32_t id = 0; id < kNumParams; ++id)
            norm[id] = 0.0;
        const uint32_t typeId = slotParam (0, kSlotType);
        norm[typeId] = toNormalized (typeId, kFxSmacheratr);
        has[typeId] = true;
        const uint32_t multiType = slotParam (1, kSlotType);
        norm[multiType] = toNormalized (multiType, kFxWidr);
        has[multiType] = true;
        for (uint32_t id = smacheratr::kClarityAdvanced; id <= smacheratr::kClarityDriveAmount; ++id)
        {
            norm[slotBlockParam (0, id)] = 0.7;
            has[slotBlockParam (0, id)] = true;
        }
        norm[slotBlockParam (1, smacheratr::kClaritySubFreq)] = 0.7; // Widr's own value in that place
        migrateGentlrInSlots (norm, has, version);
        for (uint32_t id = smacheratr::kClaritySub; id <= smacheratr::kClaritySubThreshold; ++id)
            CHECK (norm[slotBlockParam (0, id)] == smacheratr::defaultNormalized (id) && has[slotBlockParam (0, id)], "version %d: %u default", version, id);
        CHECK (smacheratr::toPlain (smacheratr::kClaritySub, norm[slotBlockParam (0, smacheratr::kClaritySub)]) == 0.0, "Sub off");
        CHECK (norm[slotBlockParam (1, smacheratr::kClaritySubFreq)] == 0.7, "another effect's place untouched");
        // Advanced values from version 11 are kept; from 10 they were reset too
        CHECK ((norm[slotBlockParam (0, smacheratr::kClarityAdvanced)] == 0.7) == (version == 11), "version %d: Advanced", version);
    }
    // a current state keeps its Sub values
    {
        std::array<double, kNumParams> norm {};
        std::array<bool, kNumParams> has {};
        const uint32_t typeId = slotParam (0, kSlotType);
        norm[typeId] = toNormalized (typeId, kFxSmacheratr);
        has[typeId] = true;
        norm[slotBlockParam (0, smacheratr::kClaritySub)] = 1.0;
        migrateGentlrInSlots (norm, has, 12);
        CHECK (norm[slotBlockParam (0, smacheratr::kClaritySub)] == 1.0, "version 12 untouched");
    }
    // states from before 17: the High band and No Overlap in a Smacheratr slot and a Gentlr slot get their
    // defaults (off), whatever the places held; the Sub band's values from 12 on are kept
    for (int version : {13, 16, 17})
    {
        std::array<double, kNumParams> norm {};
        std::array<bool, kNumParams> has {};
        const uint32_t smType = slotParam (0, kSlotType), gType = slotParam (1, kSlotType);
        norm[smType] = toNormalized (smType, kFxSmacheratr);
        norm[gType] = toNormalized (gType, kFxGentlr);
        has[smType] = has[gType] = true;
        for (uint32_t j = 0; j < kSlotBlockAll; ++j)
            for (int slot : {0, 1})
            {
                norm[slotBlockParam (slot, j)] = 1.0;
                has[slotBlockParam (slot, j)] = true;
            }
        migrateGentlrInSlots (norm, has, version);
        const bool migrated = version < 17;
        for (uint32_t id = smacheratr::kClarityHigh; id <= smacheratr::kClarityNoOverlap; ++id)
            CHECK ((norm[slotBlockParam (0, id)] == smacheratr::defaultNormalized (id)) == migrated, "version %d: Smacheratr %u", version, id);
        for (uint32_t id = gentlr::kHighOn; id <= gentlr::kNoOverlap; ++id)
            CHECK ((norm[slotBlockParam (1, id)] == gentlr::defaultNormalized (id)) == migrated, "version %d: Gentlr %u", version, id);
        CHECK (norm[slotBlockParam (0, smacheratr::kClaritySub)] == 1.0 && norm[slotBlockParam (1, gentlr::kSubOn)] == 1.0,
               "version %d: the Sub bands kept", version);
        if (migrated)
            CHECK (smacheratr::toPlain (smacheratr::kClarityHigh, norm[slotBlockParam (0, smacheratr::kClarityHigh)]) == 0.0 &&
                       gentlr::toPlain (gentlr::kHighOn, norm[slotBlockParam (1, gentlr::kHighOn)]) == 0.0,
                   "version %d: High off", version);
    }
}

TEST (slope_in_slots)
{
    // states from before 20: a rack Smacheratr's and Gentlr's band Slope gets Classic, the shape their bands
    // had (whatever the place held); 20 on, nothing changes. Slot 0 Smacheratr, slot 1 Gentlr, slot 2 Para
    // (left alone)
    for (int version : {16, 19, 20})
    {
        std::array<double, kNumParams> norm {};
        std::array<bool, kNumParams> has {};
        const int types[3] = {kFxSmacheratr, kFxGentlr, kFxPara};
        for (int slot = 0; slot < 3; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            norm[typeId] = toNormalized (typeId, types[slot]);
            has[typeId] = true;
            for (uint32_t j = 0; j < kSlotBlockAll; ++j)
            {
                norm[slotBlockParam (slot, j)] = 0.0;
                has[slotBlockParam (slot, j)] = true;
            }
        }
        migrateSlopeInSlots (norm, has, version);
        const double classic = smacheratr::classicSlopeNorm (), want = version < 20 ? classic : 0.0;
        CHECK (norm[slotBlockParam (0, smacheratr::kClaritySlope)] == want && norm[slotBlockParam (1, (uint32_t)fxBlockOf (kFxGentlr, gentlr::kSlope))] == want,
               "version %d: the Smacheratr's and the Gentlr's Slope %s", version, version < 20 ? "Classic" : "as saved");
        int touched = 0;
        for (uint32_t j = 0; j < kSlotBlockAll; ++j)
            touched += norm[slotBlockParam (2, j)] != 0.0;
        CHECK (touched == 0, "version %d: the Para left alone", version);
    }
    // Gentlr's Slope sits in the slot's extension (its ID 65, after its end saturator's blocks), shown in the rack
    auto rackHides = [] (int type, uint32_t id) {
        for (const RackHidden& r : rackHiddenParams (type))
            if (id >= r.first && id <= r.last)
                return true;
        return false;
    };
    CHECK (fxBlockOf (kFxGentlr, gentlr::kSlope) == (int64_t)gentlr::kSlope && gentlr::kSlope >= kSlotBlock && gentlr::kSlope < kSlotBlockAll &&
               !rackHides (kFxGentlr, gentlr::kSlope) && rackHides (kFxGentlr, gentlr::kTailExt3Base + pk::kTailExt3Slope),
           "Gentlr's Slope in the rack, its end saturator's not");
    CHECK (!rackHides (kFxSmacheratr, smacheratr::kClaritySlope), "Smacheratr's Slope in the rack");
    // the saturator after the rack (before 0.9) moved into a slot: Classic, the shape its bands had
    std::array<double, kNumParams> norm {};
    for (uint32_t id = 0; id < kNumParams; ++id)
        norm[id] = defaultNormalized (id);
    endSaturatorToSlot (
        3, [&] (uint32_t id) { return norm[id]; }, [&] (uint32_t id, double v) { norm[id] = v; });
    CHECK (norm[slotBlockParam (3, smacheratr::kClaritySlope)] == smacheratr::classicSlopeNorm (), "the old saturator in a slot: Classic");
}

TEST (glue_in_slots)
{
    // states from before 21: a rack Smacheratr's and Gentlr's glue switches get off (whatever the place
    // held); 21 on, nothing changes. Slot 0 Smacheratr, slot 1 Gentlr, slot 2 Para (left alone). The
    // switches have places in the slots (Gentlr's in the extension) and show on the pages
    for (int version : {19, 20, 21})
    {
        std::array<double, kNumParams> norm {};
        std::array<bool, kNumParams> has {};
        const int types[3] = {kFxSmacheratr, kFxGentlr, kFxPara};
        for (int slot = 0; slot < 3; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            norm[typeId] = toNormalized (typeId, types[slot]);
            has[typeId] = true;
            for (uint32_t j = 0; j < kSlotBlockAll; ++j)
            {
                norm[slotBlockParam (slot, j)] = 1.0;
                has[slotBlockParam (slot, j)] = true;
            }
        }
        migrateGlueInSlots (norm, has, version);
        int off = 0;
        for (int g = 0; g < smacheratr::kGluePairs; ++g)
            off += (norm[slotBlockParam (0, smacheratr::kClarityGlueIds[g])] == 0.0) +
                   (norm[slotBlockParam (1, (uint32_t)fxBlockOf (kFxGentlr, gentlr::kGlueIds[g]))] == 0.0);
        CHECK (off == (version < 21 ? 2 * smacheratr::kGluePairs : 0), "version %d: %d switches off", version, off);
        int touched = 0;
        for (uint32_t j = 0; j < kSlotBlockAll; ++j)
            touched += norm[slotBlockParam (2, j)] != 1.0;
        CHECK (touched == 0, "version %d: the Para left alone", version);
    }
    for (int g = 0; g < smacheratr::kGluePairs; ++g)
        CHECK (fxBlockOf (kFxGentlr, gentlr::kGlueIds[g]) == (int64_t)gentlr::kGlueIds[g] && gentlr::kGlueIds[g] < kSlotBlockAll &&
                   fxBlockOf (kFxSmacheratr, smacheratr::kClarityGlueIds[g]) == (int64_t)smacheratr::kClarityGlueIds[g],
               "glue switch %d has a place in both slots", g);
}

TEST (sub_high_without_buttons_in_slots)
{
    // states from before 19: a rack Smacheratr's and Gentlr's Sub and High bands that were off get Range 0,
    // those that were on keep their Range (the same sound); 19 on, nothing changes. Slot 0 Smacheratr,
    // slot 1 Gentlr, slot 2 Para (its own saturator is not used in the rack: left alone)
    for (int version : {17, 18, 19})
    {
        std::array<double, kNumParams> norm {};
        std::array<bool, kNumParams> has {};
        const int types[3] = {kFxSmacheratr, kFxGentlr, kFxPara};
        for (int slot = 0; slot < 3; ++slot)
        {
            const uint32_t typeId = slotParam (slot, kSlotType);
            norm[typeId] = toNormalized (typeId, types[slot]);
            has[typeId] = true;
            for (uint32_t j = 0; j < kSlotBlockAll; ++j)
            {
                norm[slotBlockParam (slot, j)] = 0.5;
                has[slotBlockParam (slot, j)] = true;
            }
        }
        auto set = [&] (int slot, uint32_t id, double v) { norm[slotBlockParam (slot, id)] = v; };
        set (0, smacheratr::kClaritySub, 0.0), set (0, smacheratr::kClarityHigh, 1.0);
        set (1, gentlr::kSubOn, 1.0), set (1, gentlr::kHighOn, 0.0);
        const uint32_t paraSub = para::kTailExt2Base + pk::kTailExt2SubRange;
        set (2, para::kTailExt2Base + pk::kTailExt2Sub, 0.0);
        migrateSubHighInSlots (norm, has, version);
        auto at = [&] (int slot, uint32_t id) { return norm[slotBlockParam (slot, id)]; };
        const bool migrated = version < 19;
        CHECK (at (0, smacheratr::kClaritySubRange) == (migrated ? 0.0 : 0.5) && at (0, smacheratr::kClarityHighRange) == 0.5,
               "version %d: Smacheratr's Sub (off) %s, High (on) kept", version, migrated ? "0" : "kept");
        CHECK (at (1, gentlr::kSubRange) == 0.5 && at (1, gentlr::kHighRange) == (migrated ? 0.0 : 0.5),
               "version %d: Gentlr's Sub (on) kept, High (off) %s", version, migrated ? "0" : "kept");
        CHECK (at (2, paraSub) == 0.5, "version %d: Para's own saturator left alone", version);
    }
    // and the rack's Smacheratr and Gentlr pages have no Sub or High buttons: those IDs are not in the rack
    auto hidden = [] (int type, uint32_t id) {
        for (const auto& h : rackHiddenParams (type))
            if (id >= h.first && id <= h.last)
                return true;
        return false;
    };
    CHECK (hidden (kFxSmacheratr, smacheratr::kClaritySub) && hidden (kFxSmacheratr, smacheratr::kClarityHigh) &&
               !hidden (kFxSmacheratr, smacheratr::kClaritySubRange) && !hidden (kFxSmacheratr, smacheratr::kClarityHighRange),
           "Smacheratr: the buttons hidden, the Ranges there");
    CHECK (hidden (kFxGentlr, gentlr::kSubOn) && hidden (kFxGentlr, gentlr::kHighOn) && !hidden (kFxGentlr, gentlr::kSubRange) &&
               !hidden (kFxGentlr, gentlr::kHighRange) && !hidden (kFxGentlr, gentlr::bandParam (0, gentlr::kOn)),
           "Gentlr: the Sub and High On hidden, the Ranges and band 1's On there");
}

TEST (rack_order_and_widr)
{
    auto s = sine (440.0, 1.0);
    // Smacheratr driven hard, then Para's notch, or the other way round: the harmonics the saturator
    // makes after the notch are not notched, so the two orders sound different
    auto render = [&] (bool satFirst) {
        std::unique_ptr<Engine> e (makeEngine (s));
        loadFx (*e, satFirst ? 0 : 1, kFxSmacheratr);
        setFx (*e, satFirst ? 0 : 1, smacheratr::kDrive, 24.0);
        loadFx (*e, satFirst ? 1 : 0, kFxPara);
        setFx (*e, satFirst ? 1 : 0, para::kHpFreq, 1600.0);
        setFx (*e, satFirst ? 1 : 0, para::kLpFreq, 700.0);
        e->noteOn (60, 1.0f);
        return run (*e, 24000);
    };
    const auto a = render (true), b = render (false);
    CHECK (std::fabs (toneAmp (a.l, 1320.0, 12000, 24000) - toneAmp (b.l, 1320.0, 12000, 24000)) >
               0.3 * std::max (toneAmp (a.l, 1320.0, 12000, 24000), toneAmp (b.l, 1320.0, 12000, 24000)),
           "the order matters: 3rd harmonic %f vs %f", toneAmp (a.l, 1320.0, 12000, 24000), toneAmp (b.l, 1320.0, 12000, 24000));
    // Widr in the rack widens a mono sample; off, it is mono again
    std::unique_ptr<Engine> e (makeEngine (s));
    loadFx (*e, 0, kFxWidr);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 36000);
    double sideE = 0.0, midE = 0.0;
    for (size_t i = 12000; i < o.l.size (); ++i)
    {
        sideE += 0.25 * (o.l[i] - o.r[i]) * (o.l[i] - o.r[i]);
        midE += 0.25 * (o.l[i] + o.r[i]) * (o.l[i] + o.r[i]);
    }
    CHECK (sideE > 0.02 * midE, "Widr widens it: side %f of the mid", sideE / midE);
    e->setParam (slotParam (0, kSlotOn), 0.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 36000);
    double side2 = 0.0;
    for (size_t i = 12000; i < o.l.size (); ++i)
        side2 += 0.25 * (o.l[i] - o.r[i]) * (o.l[i] - o.r[i]);
    CHECK (side2 < 1e-9 * midE + 1e-12, "off: mono again (%g)", side2);
}

// --- Wubr in the rack -------------------------------------------------------
// Level (dB rms) of x in windows of `win` samples.
static std::vector<double> dbWindows (const std::vector<float>& x, size_t win)
{
    std::vector<double> d;
    for (size_t s = 0; s + win <= x.size (); s += win)
        d.push_back (20.0 * std::log10 (rms (x, s, s + win) + 1e-12));
    return d;
}
// Level of a over ref (dB), window by window.
static std::vector<double> dbOver (const std::vector<float>& a, const std::vector<float>& ref, size_t win)
{
    auto d = dbWindows (a, win);
    const auto r = dbWindows (ref, win);
    for (size_t i = 0; i < d.size () && i < r.size (); ++i)
        d[i] -= r[i];
    d.resize (std::min (d.size (), r.size ()));
    return d;
}
static int peaksOver (const std::vector<double>& v, double thr)
{
    int n = 0;
    for (size_t i = 1; i + 1 < v.size (); ++i)
        if (v[i] > v[i - 1] && v[i] >= v[i + 1] && v[i] > thr)
            ++n;
    return n;
}
static double meanOf (const std::vector<double>& v, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += v[i];
    return b > a ? s / (double)(b - a) : 0.0;
}
constexpr size_t kWin = 1200; // 25 ms: 3 whole cycles of 120 Hz at 48 kHz
// The Wubr the sound tests below measure (not its defaults): band 1 on, band 2 off, both moving their
// Gain, each at its own rate (Link Rates off), Sync 1/4
static void wubrGainSync (Engine& e, int slot)
{
    for (int b = 0; b < wubr::kBands; ++b)
    {
        setFx (e, slot, wubr::bandParam (b, wubr::kBandOn), b == 0 ? 1.0 : 0.0);
        setFx (e, slot, wubr::bandParam (b, wubr::kTarget), wubr::kTargetGain);
        setFx (e, slot, wubr::bandParam (b, wubr::kRateMode), wubr::kSynced);
        setFx (e, slot, wubr::bandParam (b, wubr::kSync), 8.0); // 1/4
    }
    setFx (e, slot, wubr::kLinkRate, 0.0);
}

TEST (rack_wubr_mapping)
{
    // Wubr's 80 parameters without its end saturator: 62 in the slot's block, 18 in its extension
    // (the last Link Rates, the one after its end saturator's block)
    const auto& t = fxBlockTable (kFxWubr);
    const auto& wt = wubr::paramTable ();
    CHECK (t.size () == 80 && t.size () <= kSlotBlockAll, "Wubr's block table: %u", (unsigned)t.size ());
    int inExt = 0;
    for (uint32_t j = 0; j < kSlotBlockAll; ++j)
    {
        const int64_t id = fxIdAt (kFxWubr, j);
        if (j >= t.size ())
        {
            CHECK (id < 0, "position %u after Wubr's: %lld", j, (long long)id);
            continue;
        }
        CHECK (id >= 0 && id < wubr::kNumParams && fxBlockOf (kFxWubr, (uint32_t)id) == (int64_t)j, "position %u -> id %lld", j,
               (long long)id);
        if (id < 0)
            continue;
        CHECK (!wubr::isTailParam ((uint32_t)id), "position %u is its end saturator's %lld", j, (long long)id);
        const auto& a = t.info (j);
        const auto& b = wt.info ((uint32_t)id);
        CHECK (a.id == j && std::string (a.name) == std::string (b.name) && a.min == b.min && a.max == b.max && a.def == b.def,
               "position %u reads as %s", j, std::string (b.name).c_str ());
        for (int s = 0; s < kRackSlots; ++s)
        {
            const uint32_t pid = slotBlockParam (s, j);
            const RackField rf = rackField (pid);
            CHECK (pid < kNumParams && isRackParam (pid) && !isTailParam (pid) && rf.slot == s && rf.field == kSlotParams + j,
                   "slot %d position %u: id %u -> slot %d field %u", s, j, pid, rf.slot, rf.field);
            if (j >= kSlotBlock)
                CHECK (pid >= kRackExtBase && pid < kRackExtBase + kRackSlots * kSlotExt, "slot %d position %u in the extension: %u", s, j, pid);
            else
                CHECK (pid >= kRackBase && pid < kTailExtBase, "slot %d position %u in the rack block: %u", s, j, pid);
        }
        inExt += j >= kSlotBlock ? 1 : 0;
    }
    CHECK (inExt == 18, "18 in the extension: %d", inExt);
    // back: every Wubr ID has a position, except its end saturator's
    for (uint32_t id = 0; id < wubr::kNumParams; ++id)
        CHECK ((fxBlockOf (kFxWubr, id) < 0) == wubr::isTailParam (id), "wubr %u (%s): %lld", id, wt.info (id).name,
               (long long)fxBlockOf (kFxWubr, id));
    // the extension is where band 2's shape runs on: its third point's level is the first position there,
    // its last point's curve at 78; then Link Rates, the last
    CHECK (fxBlockOf (kFxWubr, wubr::pointParam (1, 2, wubr::kPtY)) == (int64_t)kSlotBlock, "%lld",
           (long long)fxBlockOf (kFxWubr, wubr::pointParam (1, 2, wubr::kPtY)));
    CHECK (fxBlockOf (kFxWubr, wubr::pointParam (1, wubr::kMaxPoints - 1, wubr::kPtCurve)) == 78, "%lld",
           (long long)fxBlockOf (kFxWubr, wubr::pointParam (1, wubr::kMaxPoints - 1, wubr::kPtCurve)));
    CHECK (fxBlockOf (kFxWubr, wubr::kLinkRate) == 79 && fxIdAt (kFxWubr, 79) == (int64_t)wubr::kLinkRate && t.size () > 79 &&
               std::string (t.info (79).name) == "Link Rates",
           "Link Rates at 79: %lld", (long long)fxBlockOf (kFxWubr, wubr::kLinkRate));
    // the end saturator's second block, between the rack and the extensions, is not the rack's
    for (uint32_t id = kTailExtBase; id < kRackExtBase; ++id)
        CHECK (!isRackParam (id) && isTailParam (id), "end saturator %u", id);
    // the rack's extensions end at kRackExtEnd; what came after them is the sampler's own (the Transpose
    // high-pass, the modulation LFOs, the envelopes' Loop Locks), not the rack's, and the IDs run on to
    // kNumParams with no gap, through the hidden MIDI parameters (1000 .. 1002: in the table, but not the
    // instrument's) to the ones after them
    CHECK (kRackExtEnd == kRackExtBase + kRackSlots * kSlotExt && kTransHpOn == kRackExtEnd && kModLfoBase == kTransHpSlope + 1 &&
               kFiltLoopLock == kModLfoEnd && kPitchLoopLock == kFiltLoopLock + 1 && kMidiPitchBend == kPitchLoopLock + 1 &&
               kMidiModWheel + 1 == kGridOn && paramTable ().size () == kNumParams,
           "kNumParams %u", (unsigned)kNumParams);
    for (uint32_t id = kRackExtEnd; id < kNumParams; ++id)
        CHECK (!isRackParam (id) && !isTailParam (id) && isValidParam (id) == !isMidiParam (id), "%u is the sampler's", id);
    CHECK (!isValidParam (kMidiPitchBend) && !isValidParam (kMidiSustain) && !isValidParam (kMidiModWheel) &&
               isMidiParam (kMidiSustain) && !isMidiParam (kPitchLoopLock) && !isMidiParam (kGridOn) &&
               !canModulate (kMidiModWheel, kFxEmpty) && !canModulate (kMidiPitchBend, kFxEmpty),
           "the MIDI parameters are not the instrument's");
    CHECK (isRackParam (kRackExtEnd - 1) && rackField (kRackExtEnd - 1).slot == kRackSlots - 1 &&
               rackField (kRackExtEnd - 1).field == kSlotParams + kSlotBlockAll - 1,
           "the last extension ID is the last slot's last position");
    // what the rack page does not show: Wubr's own IDs, and every one it does show has a block position
    for (const auto& h : rackHiddenParams (kFxWubr))
        CHECK (h.first <= h.last && h.last < wubr::kNumParams, "hidden %u..%u", h.first, h.last);
    for (uint32_t id = 0; id < wubr::kNumParams; ++id)
    {
        bool hidden = false;
        for (const auto& h : rackHiddenParams (kFxWubr))
            hidden |= id >= h.first && id <= h.last;
        CHECK (hidden || fxBlockOf (kFxWubr, id) >= 0, "wubr %u (%s) is shown but has no place in the slot", id, wt.info (id).name);
        CHECK (!wubr::isTailParam (id) || hidden, "wubr %u: its end saturator is listed as hidden", id);
    }
}

TEST (rack_wubr_moves_the_sound)
{
    // a 120 Hz sample through Wubr in slot 3: band 1's triangle (1/4, +-12 dB around 120 Hz) swings it,
    // two cycles a second at 120 BPM and one at 60; switched off, the slot leaves it untouched (Wubr set
    // up by wubrGainSync: band 1 moving its gain, band 2 off)
    auto s = sine (120.0, 3.0);
    auto render = [&] (int type, bool on, HostInfo host, const std::function<void (Engine&)>& setup) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kVolume, -20.0); // room for Wubr's +12 dB before the end saturator
        if (type != kFxEmpty)
            loadFx (*e, 2, type);
        if (type == kFxWubr)
            wubrGainSync (*e, 2);
        e->setParam (slotParam (2, kSlotOn), on ? 1.0 : 0.0);
        if (setup)
            setup (*e);
        e->noteOn (60, 1.0f);
        return run (*e, 96000, host);
    };
    HostInfo h120;
    h120.bpm = 120.0;
    const auto dry = render (kFxEmpty, true, h120, nullptr);
    const auto off = render (kFxWubr, false, h120, nullptr);
    double offDiff = 0.0;
    for (size_t i = 0; i < dry.l.size (); ++i)
        offDiff = std::max (offDiff, (double)std::fabs (off.l[i] - dry.l[i]));
    CHECK (offDiff < 1e-6, "off: untouched (%g)", offDiff);
    const auto offLv = dbWindows (off.l, kWin);
    const auto [offLo, offHi] = std::minmax_element (offLv.begin () + 2, offLv.end ());
    CHECK (*offHi - *offLo < 0.5, "off: steady %.2f .. %.2f dB", *offLo, *offHi);

    const auto on = render (kFxWubr, true, h120, nullptr);
    const auto lv = dbOver (on.l, off.l, kWin);
    const auto [lo, hi] = std::minmax_element (lv.begin () + 1, lv.end ());
    std::printf ("    on: %.1f .. %.1f dB against off, %d peaks in 2 s\n", *lo, *hi, peaksOver (lv, 6.0));
    CHECK (*hi > 10.0 && *lo < -10.0, "swings +-12 dB: %.1f .. %.1f dB", *lo, *hi);
    CHECK (peaksOver (lv, 6.0) == 4, "two cycles a second at 120 BPM: %d peaks", peaksOver (lv, 6.0));
    // the host's tempo reaches it
    HostInfo h60;
    h60.bpm = 60.0;
    const auto slow = dbOver (render (kFxWubr, true, h60, nullptr).l, off.l, kWin);
    CHECK (peaksOver (slow, 6.0) == 2, "one a second at 60 BPM: %d peaks", peaksOver (slow, 6.0));
    // and its song position while it plays: half a beat in, the 1/4 triangle is at its top; at 0 at its bottom
    HostInfo at0 = h120, atHalf = h120;
    at0.playing = atHalf.playing = true;
    at0.ppqValid = atHalf.ppqValid = true;
    atHalf.ppq = 0.5;
    const auto top = dbOver (render (kFxWubr, true, atHalf, nullptr).l, off.l, kWin);
    const auto bottom = dbOver (render (kFxWubr, true, at0, nullptr).l, off.l, kWin);
    std::printf ("    the song position: %.1f dB half a beat in, %.1f dB at 0\n", top[1], bottom[1]);
    CHECK (top[1] > 5.0 && bottom[1] < -5.0, "follows the song: %.1f / %.1f dB", top[1], bottom[1]);

    // band 2 instead (band 1 off), moved to 120 Hz: the same triangle, until its third point's level (a
    // value in the slot's extension) goes to the top: then the second half of each cycle stays up
    const uint32_t ext = (uint32_t)fxBlockOf (kFxWubr, wubr::pointParam (1, 2, wubr::kPtY));
    CHECK (ext >= kSlotBlock && ext < kSlotBlockAll, "band 2's point 3 level is in the extension: %u", ext);
    auto band2 = [&] (bool raise) {
        return [raise] (Engine& e) {
            setFx (e, 2, wubr::bandParam (0, wubr::kBandOn), 0.0);
            setFx (e, 2, wubr::bandParam (1, wubr::kBandOn), 1.0);
            setFx (e, 2, wubr::bandParam (1, wubr::kFreq), 120.0);
            if (raise)
                setFx (e, 2, wubr::pointParam (1, 2, wubr::kPtY), 1.0);
        };
    };
    const auto tri = dbOver (render (kFxWubr, true, h120, band2 (false)).l, off.l, kWin);
    const auto raised = dbOver (render (kFxWubr, true, h120, band2 (true)).l, off.l, kWin);
    const auto [tLo, tHi] = std::minmax_element (tri.begin () + 1, tri.end ());
    const double triMean = meanOf (tri, 1, tri.size ()), raisedMean = meanOf (raised, 1, raised.size ());
    std::printf ("    band 2: %.1f .. %.1f dB, mean %.1f dB; its point 3 raised: mean %.1f dB\n", *tLo, *tHi, triMean, raisedMean);
    CHECK (*tHi > 10.0 && *tLo < -10.0 && std::fabs (triMean) < 1.5, "band 2 swings: %.1f .. %.1f, mean %.1f dB", *tLo, *tHi, triMean);
    CHECK (raisedMean > triMean + 4.0, "the extension's value reaches the engine: mean %.1f vs %.1f dB", raisedMean, triMean);
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        loadFx (*e, 2, kFxWubr);
        band2 (true) (*e);
        CHECK (e->param (slotBlockParam (2, ext)) == 1.0 && e->param (slotBlockParam (1, ext)) == 0.0 &&
                   e->param (slotBlockParam (3, ext)) == 0.0,
               "stored in slot 3's extension only");
    }
}

TEST (rack_wubr_envelope_follows_notes)
{
    // Wubr in Envelope mode, MIDI: the sampler's note-on starts band 1's shape (from -12 dB up to its
    // hold point, the top, +12 dB, a quarter second in at 120 BPM) and it stays there while a note is
    // held; a second note starts it again; letting go of the last note plays the rest (down to -12 dB).
    // The sampler's release is long, so the sound carries on; the level is the 120 Hz of the first note,
    // against the same notes with Wubr off.
    auto s = sine (120.0, 6.0);
    auto render = [&] (bool on) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kVolume, -20.0);
        e->setParam (kAmpR, 5000.0);
        loadFx (*e, 0, kFxWubr);
        wubrGainSync (*e, 0); // band 1 moving its gain, band 2 off
        setFx (*e, 0, wubr::kMode, wubr::kEnvelope);
        setFx (*e, 0, wubr::bandParam (0, wubr::kHold), 2.0);
        e->setParam (slotParam (0, kSlotOn), on ? 1.0 : 0.0);
        Out o;
        auto more = [&] (int frames) {
            const Out p = run (*e, frames);
            o.l.insert (o.l.end (), p.l.begin (), p.l.end ());
        };
        more (4800); // nothing playing yet
        e->noteOn (60, 1.0f);
        more (48000);
        e->noteOn (72, 1.0f); // an octave up (240 Hz: whole cycles in the windows, so it does not count)
        more (24000);
        e->noteOff (72);
        more (24000);
        e->noteOff (60);
        more (48000);
        return o;
    };
    const auto on = render (true), off = render (false);
    // the 120 Hz level against Wubr off, per 25 ms from `t` seconds after the first note, over `secs`
    auto level = [&] (double t, double secs) {
        std::vector<double> d;
        for (size_t a = 4800 + (size_t)(t * kHostSr); a + kWin <= 4800 + (size_t)((t + secs) * kHostSr); a += kWin)
            d.push_back (20.0 * std::log10 ((toneAmp (on.l, 120.0, a, a + kWin) + 1e-12) / (toneAmp (off.l, 120.0, a, a + kWin) + 1e-12)));
        return d;
    };
    auto range = [] (const std::vector<double>& v) { return std::make_pair (*std::min_element (v.begin (), v.end ()), *std::max_element (v.begin (), v.end ())); };
    // (the second note's window starts 25 ms in: the drop from the top takes a few ms, and the output
    // is late by the end saturator's latency)
    const auto start = level (0.0, 0.025), held = level (0.4, 0.6), again = level (1.025, 0.025), again2 = level (1.4, 0.1),
               stillHeld = level (1.6, 0.4), rest = level (2.35, 0.65);
    std::printf ("    note on %.1f dB, held %.1f .. %.1f, 2nd note %.1f, 2nd note off %.1f .. %.1f, let go %.1f .. %.1f dB\n",
                 start[0], range (held).first, range (held).second, again[0], range (stillHeld).first, range (stillHeld).second,
                 range (rest).first, range (rest).second);
    CHECK (start[0] < -6.0, "a note starts the shape at its bottom: %.1f dB", start[0]);
    CHECK (range (held).first > 10.0, "held at the top: %.1f dB", range (held).first);
    CHECK (again[0] < -6.0 && range (again2).first > 10.0, "a second note starts it again: %.1f, then %.1f dB", again[0],
           range (again2).first);
    CHECK (range (stillHeld).first > 10.0, "the first note still holds it after the second lets go: %.1f dB", range (stillHeld).first);
    CHECK (range (rest).second < -10.0, "let go: the rest of the shape, down to the bottom: %.1f dB", range (rest).second);
    const double released = rms (on.l, 4800 + (size_t)(2.35 * kHostSr), on.l.size ());
    CHECK (released > 1e-3, "the sampler still sounds after the last note-off: %f", released);
}

TEST (rack_wubr_slots_and_moves)
{
    // every value of every slot (Type, On, each block position, the extension too) is a parameter of
    // its own: no two slots share one, and none is the end saturator's
    std::vector<int> owner (kNumParams, -1);
    for (int s = 0; s < kRackSlots; ++s)
        for (uint32_t k = 0; k < kSlotParams + kSlotBlockAll; ++k)
        {
            const uint32_t id = k < kSlotParams ? slotParam (s, k) : slotBlockParam (s, k - kSlotParams);
            CHECK (id < kNumParams && owner[id] < 0 && isRackParam (id) && !isTailParam (id), "slot %d value %u: id %u (slot %d had it)", s,
                   k, id, id < kNumParams ? owner[id] : -2);
            if (id < kNumParams)
                owner[id] = s;
            CHECK (rackField (id).slot == s && rackField (id).field == k, "slot %d value %u decodes to %d / %u", s, k, rackField (id).slot,
                   rackField (id).field);
        }
    // a Wubr slot moved the way the editor moves one (every value copied to the other slot, the old slot
    // emptied) sounds the same, its extension values with it
    auto s = sine (120.0, 2.0);
    const uint32_t ext = (uint32_t)fxBlockOf (kFxWubr, wubr::pointParam (1, 2, wubr::kPtY));
    auto setup = [&] (Engine& e, int slot) {
        e.setParam (kVolume, -20.0);
        loadFx (e, slot, kFxWubr);
        wubrGainSync (e, slot);
        setFx (e, slot, wubr::bandParam (0, wubr::kBandOn), 0.0);
        setFx (e, slot, wubr::bandParam (1, wubr::kBandOn), 1.0);
        setFx (e, slot, wubr::bandParam (1, wubr::kFreq), 120.0);
        setFx (e, slot, wubr::pointParam (1, 2, wubr::kPtY), 1.0); // in the extension
        setFx (e, slot, wubr::pointParam (1, 1, wubr::kPtCurve), 0.6);
    };
    auto play = [&] (Engine& e) {
        e.reset ();
        e.noteOn (60, 1.0f);
        return run (e, 48000);
    };
    std::unique_ptr<Engine> a (makeEngine (s)), b (makeEngine (s)), c (makeEngine (s));
    setup (*a, 1);
    const auto ref = play (*a);
    setup (*b, 1);
    for (uint32_t k = 0; k < kSlotParams + kSlotBlockAll; ++k)
    {
        const uint32_t from = k < kSlotParams ? slotParam (1, k) : slotBlockParam (1, k - kSlotParams);
        const uint32_t to = k < kSlotParams ? slotParam (4, k) : slotBlockParam (4, k - kSlotParams);
        b->setParam (to, b->param (from));
    }
    b->setParam (slotParam (1, kSlotType), (double)kFxEmpty);
    CHECK (b->rackType (4) == kFxWubr && b->rackType (1) == kFxEmpty && b->param (slotBlockParam (4, ext)) == 1.0,
           "moved: slot 5 is Wubr (%d), slot 2 empty (%d)", b->rackType (4), b->rackType (1));
    const auto moved = play (*b);
    double diff = 0.0;
    for (size_t i = 0; i < ref.l.size (); ++i)
        diff = std::max (diff, (double)std::fabs (moved.l[i] - ref.l[i]));
    CHECK (diff < 1e-6, "moved, it sounds the same: %g", diff);
    // without the extension's value it would not
    setup (*c, 4);
    setFx (*c, 4, wubr::pointParam (1, 2, wubr::kPtY), -1.0);
    const auto plain = play (*c);
    double diff2 = 0.0;
    for (size_t i = 0; i < ref.l.size (); ++i)
        diff2 = std::max (diff2, (double)std::fabs (plain.l[i] - ref.l[i]));
    std::printf ("    moved: %g apart; without its extension value: %g\n", diff, diff2);
    CHECK (diff2 > 0.01, "the extension's value matters: %g", diff2);
}

TEST (rack_wubr_defaults_and_link)
{
    // Wubr loads into a slot with its defaults: both bands on, moving their centres (Frequency), free at
    // 0.75 Hz, Link Rates on
    auto s = sine (120.0, 3.0);
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        loadFx (*e, 3, kFxWubr);
        auto plain = [&] (uint32_t id) {
            return wubr::toPlain (id, e->param (slotBlockParam (3, (uint32_t)fxBlockOf (kFxWubr, id))));
        };
        for (int b = 0; b < wubr::kBands; ++b)
        {
            CHECK (plain (wubr::bandParam (b, wubr::kBandOn)) == 1.0, "band %d on", b + 1);
            CHECK (std::lround (plain (wubr::bandParam (b, wubr::kTarget))) == wubr::kTargetFreq, "band %d: Frequency (%f)", b + 1,
                   plain (wubr::bandParam (b, wubr::kTarget)));
            CHECK (std::lround (plain (wubr::bandParam (b, wubr::kRateMode))) == wubr::kFree, "band %d: Free (%f)", b + 1,
                   plain (wubr::bandParam (b, wubr::kRateMode)));
            CHECK (std::fabs (plain (wubr::bandParam (b, wubr::kRateHz)) - 0.75) < 1e-6, "band %d: 0.75 Hz (%f)", b + 1,
                   plain (wubr::bandParam (b, wubr::kRateHz)));
        }
        CHECK (plain (wubr::kLinkRate) == 1.0, "Link Rates on (%f)", plain (wubr::kLinkRate));
    }
    // linked, band 2 runs at band 1's rate: band 2 alone (band 1 off) moving the gain at 120 Hz, free,
    // its own rate 0.5 Hz; band 1's at 2 Hz, then 1 Hz. Peaks of the triangle in 2 s: 4, then 2; unlinked
    // it keeps its own 0.5 Hz (1)
    auto render = [&] (bool link, double hz1) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kVolume, -20.0);
        loadFx (*e, 3, kFxWubr);
        setFx (*e, 3, wubr::bandParam (0, wubr::kBandOn), 0.0);
        setFx (*e, 3, wubr::bandParam (0, wubr::kRateHz), hz1);
        setFx (*e, 3, wubr::bandParam (1, wubr::kTarget), wubr::kTargetGain);
        setFx (*e, 3, wubr::bandParam (1, wubr::kFreq), 120.0);
        setFx (*e, 3, wubr::bandParam (1, wubr::kRateHz), 0.5);
        setFx (*e, 3, wubr::kLinkRate, link ? 1.0 : 0.0);
        e->setParam (slotParam (3, kSlotOn), 1.0);
        e->noteOn (60, 1.0f);
        const auto on = run (*e, 96000);
        e->setParam (slotParam (3, kSlotOn), 0.0);
        e->reset ();
        e->noteOn (60, 1.0f);
        const auto off = run (*e, 96000);
        return dbOver (on.l, off.l, kWin);
    };
    const auto fast = render (true, 2.0), slower = render (true, 1.0), own = render (false, 2.0);
    const auto [fLo, fHi] = std::minmax_element (fast.begin () + 1, fast.end ());
    std::printf ("    band 2 linked to band 1 at 2 Hz: %d peaks (%.1f .. %.1f dB); at 1 Hz: %d; unlinked (its own 0.5 Hz): %d\n",
                 peaksOver (fast, 6.0), *fLo, *fHi, peaksOver (slower, 6.0), peaksOver (own, 6.0));
    CHECK (*fHi > 10.0 && *fLo < -10.0, "band 2 swings: %.1f .. %.1f dB", *fLo, *fHi);
    CHECK (peaksOver (fast, 6.0) == 4, "linked: band 1's 2 Hz: %d peaks", peaksOver (fast, 6.0));
    CHECK (peaksOver (slower, 6.0) == 2, "linked: band 1's 1 Hz: %d peaks", peaksOver (slower, 6.0));
    CHECK (peaksOver (own, 6.0) == 1, "unlinked: band 2's own 0.5 Hz: %d peaks", peaksOver (own, 6.0));
}

TEST (defaults_one_voice_and_root_note)
{
    const auto& t = paramTable ();
    CHECK (voicesFromIndex ((int)t.info (kVoices).def) == 1, "one voice by default");
    CHECK (t.info (kRootKey).def == 60.0 && t.toText (kRootKey, 60.0) == "C3", "root C3");
    // a new Smemplr: Smacheratr in the first slot, on, with its own defaults (the other slots empty),
    // and no saturator after the rack (the old one's parameters are off)
    CHECK (t.info (kTailBase + pk::kTailOn).def == 0.0, "the old end saturator is off");
    CHECK (t.info (slotParam (0, kSlotType)).def == (double)kFxSmacheratr && t.info (slotParam (0, kSlotOn)).def == 1.0,
           "Smacheratr in the first slot, on");
    for (uint32_t j = 0; j < kSlotBlockAll; ++j)
    {
        const double want = j < smacheratr::kNumParams ? smacheratr::paramTable ().defaultNormalized (j) : 0.0;
        CHECK (std::fabs (defaultNormalized (slotBlockParam (0, j)) - want) < 1e-12, "first slot position %u: %f, want %f", j,
               defaultNormalized (slotBlockParam (0, j)), want);
    }
    CHECK (smacheratr::paramTable ().info (smacheratr::kPreLimit).def == 1.0 &&
               defaultNormalized (slotBlockParam (0, smacheratr::kPreLimit)) == 1.0,
           "its Pre-Limit is on");
    for (int slot = 1; slot < kRackSlots; ++slot)
        CHECK (t.info (slotParam (slot, kSlotType)).def == (double)kFxEmpty, "slot %d empty", slot + 1);
    CHECK (std::string (t.info (kTailBase + pk::kTailOn).name).rfind ("Old ", 0) == 0 &&
               std::string (t.info (kTailExtBase + pk::kTailExtClarity).name).rfind ("Old ", 0) == 0,
           "the old end saturator's parameters are named Old: %s, %s", t.info (kTailBase + pk::kTailOn).name,
           t.info (kTailExtBase + pk::kTailExtClarity).name);
    {
        // and the engine starts that way: the Smacheratr's latency, nothing after the rack
        Engine fresh;
        fresh.prepare (kHostSr, 256);
        CHECK (fresh.rackType (0) == kFxSmacheratr && fresh.rackType (1) == kFxEmpty && fresh.latency () > 0,
               "a new engine's rack: %d %d, latency %d", fresh.rackType (0), fresh.rackType (1), fresh.latency ());
    }
    // a 440 Hz sample with the root on C4: C4 plays 440 Hz, C3 an octave down
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kRootKey, 72.0);
    e->noteOn (72, 1.0f);
    auto o = run (*e, 12000);
    CHECK (std::fabs (freqOf (o.l, 2000, 12000) / 440.0 - 1.0) < 0.003, "root plays the sample's pitch: %f", freqOf (o.l, 2000, 12000));
    e->allNotesOff ();
    run (*e, 9600);
    e->reset ();
    e->noteOn (60, 1.0f);
    o = run (*e, 12000);
    CHECK (std::fabs (freqOf (o.l, 2000, 12000) / 220.0 - 1.0) < 0.003, "an octave below the root: %f", freqOf (o.l, 2000, 12000));
}

// Amplitude of frequency f in x[a, b).
static double toneAmp (const std::vector<float>& x, double f, size_t a, size_t b)
{
    double s = 0, c = 0;
    for (size_t i = a; i < b; ++i)
    {
        s += x[i] * std::sin (2.0 * M_PI * f * i / kHostSr);
        c += x[i] * std::cos (2.0 * M_PI * f * i / kHostSr);
    }
    return 2.0 * std::sqrt (s * s + c * c) / (double)(b - a);
}

TEST (mid_side_eq_tapers_the_sides)
{
    // mid: 100 Hz; side: 50 Hz and 3 kHz. The side high-pass at 300 Hz removes the 50 Hz side
    // and keeps the rest.
    const double sr = kHostSr;
    std::vector<float> l ((size_t)sr), r ((size_t)sr);
    for (size_t i = 0; i < l.size (); ++i)
    {
        const double t = (double)i / sr;
        const double m = 0.2 * std::sin (2 * M_PI * 100 * t), sd = 0.2 * std::sin (2 * M_PI * 50 * t) + 0.1 * std::sin (2 * M_PI * 3000 * t);
        l[i] = (float)(m + sd);
        r[i] = (float)(m - sd);
    }
    auto smp = SampleData::fromBuffers (l, r, sr);
    auto measure = [&] (bool msOn, int slope, double sideDb) {
        std::unique_ptr<Engine> e (makeEngine (smp));
        e->setParam (kFilterOn, 0.0);
        loadFx (*e, 0, kFxMsEq);
        e->setParam (slotParam (0, kSlotOn), msOn ? 1.0 : 0.0);
        setFx (*e, 0, mseq::kSideHp, 300.0);
        setFx (*e, 0, mseq::kSlope, slope);
        setFx (*e, 0, mseq::kSideGain, sideDb);
        e->noteOn (60, 1.0f);
        auto o = run (*e, 36000);
        std::vector<float> mid (o.l.size ()), side (o.l.size ());
        for (size_t i = 0; i < o.l.size (); ++i)
        {
            mid[i] = 0.5f * (o.l[i] + o.r[i]);
            side[i] = 0.5f * (o.l[i] - o.r[i]);
        }
        struct R { double mid100, side50, side3k; };
        return R {toneAmp (mid, 100.0, 12000, 36000), toneAmp (side, 50.0, 12000, 36000), toneAmp (side, 3000.0, 12000, 36000)};
    };
    const auto off = measure (false, MsEq::k24, 0.0);
    const auto on = measure (true, MsEq::k24, 0.0);
    CHECK (off.side50 > 0.1, "off: the low side is there: %f", off.side50);
    CHECK (on.side50 < off.side50 * 0.03, "24 dB: the low side is gone: %f vs %f", on.side50, off.side50);
    CHECK (std::fabs (on.side3k / off.side3k - 1.0) < 0.05, "the high side stays: %f vs %f", on.side3k, off.side3k);
    CHECK (std::fabs (on.mid100 / off.mid100 - 1.0) < 0.02, "the mid stays: %f vs %f", on.mid100, off.mid100);
    const auto gentle = measure (true, MsEq::k6, 0.0);
    CHECK (gentle.side50 > on.side50 * 3.0 && gentle.side50 < off.side50 * 0.3, "6 dB tapers more gently: %f", gentle.side50);
    const auto quiet = measure (true, MsEq::k24, -6.0);
    CHECK (std::fabs (quiet.side3k / on.side3k - 0.501) < 0.03, "side gain -6 dB: %f", quiet.side3k / on.side3k);
    for (int sl = MsEq::k6; sl <= MsEq::k96; ++sl)
        CHECK (std::fabs (MsEq::responseDb (300.0, 300.0, sl) + 3.01) < 0.05, "slope %d: -3 dB at the cutoff", sl);
    const auto brick = measure (true, MsEq::kBrickwall, 0.0);
    CHECK (brick.side50 < on.side50 * 0.01, "Brickwall: far less low side than 24 dB: %g vs %g", brick.side50, on.side50);
    CHECK (std::fabs (brick.side3k / off.side3k - 1.0) < 0.05 && std::fabs (brick.mid100 / off.mid100 - 1.0) < 0.02,
           "Brickwall: the high side and the mid stay: %f, %f", brick.side3k / off.side3k, brick.mid100 / off.mid100);
}

TEST (mid_side_eq_slopes)
{
    // the rack's M/S EQ has ten slopes: 6 .. 96 dB per octave and Brickwall
    const auto& t = mseq::paramTable ();
    CHECK (t.info (mseq::kSlope).choices.size () == (size_t)MsEq::kNumSlopes && t.info (mseq::kSlope).def == (double)MsEq::k24,
           "%u slopes, 24 dB by default", (unsigned)t.info (mseq::kSlope).choices.size ());
    CHECK (std::string (t.info (mseq::kSlope).choices[MsEq::k96]) == "96 dB" &&
               std::string (t.info (mseq::kSlope).choices[MsEq::kBrickwall]) == "Brickwall",
           "the names");
    // a side-only sine through the EQ at a few frequencies around a 400 Hz cutoff: what the filter does
    // is what the display draws (within 1 dB, down to -120 dB), each slope steeper than the one before
    const double sr = kHostSr, fc = 400.0;
    auto sideGainDb = [&] (int slope, double f) {
        MsEq eq;
        eq.prepare (sr);
        const int n = 48000, blk = 256;
        std::vector<float> l ((size_t)n), r ((size_t)n);
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.5f * (float)std::sin (2.0 * M_PI * f * i / sr);
            l[(size_t)i] = v;
            r[(size_t)i] = -v;
        }
        for (int i = 0; i < n; i += blk)
            eq.process (l.data () + i, r.data () + i, std::min (blk, n - i), fc, slope, 0.0, 0.0);
        std::vector<double> side ((size_t)n);
        for (int i = 0; i < n; ++i)
            side[(size_t)i] = 0.5 * ((double)l[(size_t)i] - r[(size_t)i]);
        double s = 0, c = 0;
        for (int i = n / 2; i < n; ++i)
        {
            s += side[(size_t)i] * std::sin (2.0 * M_PI * f * i / sr);
            c += side[(size_t)i] * std::cos (2.0 * M_PI * f * i / sr);
        }
        return 20.0 * std::log10 (2.0 * std::sqrt (s * s + c * c) / (n / 2) / 0.5 + 1e-15);
    };
    double prevOctave = 1.0;
    for (int slope = 0; slope < MsEq::kNumSlopes; ++slope)
    {
        for (double f : {200.0, 300.0, 360.0, 400.0, 600.0, 2000.0})
        {
            const double want = MsEq::responseDb (f, fc, slope), got = sideGainDb (slope, f);
            if (want > -120.0)
                CHECK (std::fabs (got - want) < 1.0, "slope %d at %.0f Hz: %.2f dB, the display says %.2f", slope, f, got, want);
            else
                CHECK (got < -100.0, "slope %d at %.0f Hz: %.2f dB (the display: %.1f)", slope, f, got, want);
        }
        const double octave = MsEq::responseDb (fc / 2, fc, slope);
        CHECK (octave < prevOctave, "slope %d is steeper than the one before: %.1f dB an octave down (%.1f)", slope, octave, prevOctave);
        prevOctave = octave;
        if (slope <= MsEq::k96)
        {
            // a Butterworth of order n: 6n dB per octave well below the cutoff
            const double perOctave = MsEq::responseDb (fc / 8, fc, slope) - MsEq::responseDb (fc / 16, fc, slope);
            CHECK (std::fabs (perOctave - 6.02 * MsEq::order (slope)) < 0.1, "slope %d: %.2f dB per octave", slope, perOctave);
        }
    }
    // Brickwall: flat to the cutoff, then gone
    CHECK (MsEq::responseDb (fc, fc, MsEq::kBrickwall) > -0.06 && MsEq::responseDb (fc * 1.5, fc, MsEq::kBrickwall) > -0.06,
           "Brickwall passes from the cutoff up");
    CHECK (MsEq::responseDb (fc * 0.9, fc, MsEq::kBrickwall) < -35.0 && MsEq::responseDb (fc * 0.8, fc, MsEq::kBrickwall) < -60.0,
           "Brickwall: %.1f dB at 0.9 x the cutoff, %.1f at 0.8 x", MsEq::responseDb (fc * 0.9, fc, MsEq::kBrickwall),
           MsEq::responseDb (fc * 0.8, fc, MsEq::kBrickwall));
    // the mid is never filtered: a mono signal passes untouched with any slope (so the mono fold is too)
    for (int slope : {(int)MsEq::k96, (int)MsEq::kBrickwall})
    {
        MsEq eq;
        eq.prepare (sr);
        std::vector<float> l (4096), r (4096);
        for (size_t i = 0; i < l.size (); ++i)
            l[i] = r[i] = 0.4f * (float)std::sin (2.0 * M_PI * 60.0 * (double)i / sr);
        const std::vector<float> in = l;
        eq.process (l.data (), r.data (), (int)l.size (), 2000.0, slope, 0.0, 0.0);
        double err = 0.0;
        for (size_t i = 0; i < l.size (); ++i)
            err = std::max (err, (double)std::max (std::fabs (l[i] - in[i]), std::fabs (r[i] - in[i])));
        CHECK (err < 1e-6, "slope %d: the mid passes untouched (%g)", slope, err);
    }
    // states before version 9 stored the slope over three choices: the same slopes now
    CHECK (std::lround (t.toPlain (mseq::kSlope, mseq::slopeFromThreeChoices (0.0))) == MsEq::k6 &&
               std::lround (t.toPlain (mseq::kSlope, mseq::slopeFromThreeChoices (0.5))) == MsEq::k12 &&
               std::lround (t.toPlain (mseq::kSlope, mseq::slopeFromThreeChoices (1.0))) == MsEq::k24,
           "old slopes 6 / 12 / 24 dB");
}

TEST (old_end_saturator_moves_into_the_rack)
{
    // a state from before 0.9: its values, and whether it had them
    auto oldState = [] (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has) {
        for (uint32_t id = 0; id < kNumParams; ++id)
        {
            norm[id] = defaultNormalized (id);
            has[id] = true;
        }
        for (int s = 0; s < kRackSlots; ++s) // an empty rack, as 0.8 saved it
        {
            norm[slotParam (s, kSlotType)] = 0.0;
            for (uint32_t j = 0; j < kSlotBlockAll; ++j)
                norm[slotBlockParam (s, j)] = 0.0;
        }
    };
    auto typeOf = [] (const std::array<double, kNumParams>& norm, int s) {
        return (int)std::lround (toPlain (slotParam (s, kSlotType), norm[slotParam (s, kSlotType)]));
    };
    auto satValue = [] (const std::array<double, kNumParams>& norm, int s, uint32_t id) {
        return smacheratr::paramTable ().toPlain (id, norm[slotBlockParam (s, id)]);
    };
    const uint32_t onId = kTailBase + pk::kTailOn;
    std::array<double, kNumParams> norm {};
    std::array<bool, kNumParams> has {};
    {
        // on, with Para and Multidyn in the rack: a Smacheratr in the third slot with its settings
        oldState (norm, has);
        norm[slotParam (0, kSlotType)] = toNormalized (slotParam (0, kSlotType), kFxPara);
        norm[slotParam (1, kSlotType)] = toNormalized (slotParam (1, kSlotType), kFxMultidyn);
        norm[onId] = 1.0;
        norm[kTailBase + pk::kTailDrive] = toNormalized (kTailBase + pk::kTailDrive, 9.0);
        norm[kTailBase + pk::kTailPreLimit] = 0.0;
        norm[kTailBase + pk::kTailMix] = 0.6;
        norm[kTailExtBase + pk::kTailExtMidSide] = 1.0;
        norm[kTailExtBase + pk::kTailExtOutput] = toNormalized (kTailExtBase + pk::kTailExtOutput, -4.0);
        norm[kTailExtBase + pk::kTailExtClarityFreq] = toNormalized (kTailExtBase + pk::kTailExtClarityFreq, 900.0);
        moveEndSaturatorIntoRack (norm, has);
        CHECK (typeOf (norm, 0) == kFxPara && typeOf (norm, 1) == kFxMultidyn && typeOf (norm, 2) == kFxSmacheratr &&
                   typeOf (norm, 3) == kFxEmpty,
               "the rack: %d %d %d %d", typeOf (norm, 0), typeOf (norm, 1), typeOf (norm, 2), typeOf (norm, 3));
        CHECK (norm[onId] == 0.0 && norm[slotParam (2, kSlotOn)] == 1.0, "the old one off, the slot on");
        CHECK (std::fabs (satValue (norm, 2, smacheratr::kDrive) - 9.0) < 1e-6 && satValue (norm, 2, smacheratr::kPreLimit) == 0.0 &&
                   std::fabs (satValue (norm, 2, smacheratr::kDryWet) - 0.6) < 1e-6 && satValue (norm, 2, smacheratr::kMidSide) == 1.0 &&
                   std::fabs (satValue (norm, 2, smacheratr::kOutput) + 4.0) < 1e-6 &&
                   std::fabs (satValue (norm, 2, smacheratr::kClarityFreq) - 900.0) < 1e-3,
               "its settings: drive %f, pre-limit %f, mix %f, m/s %f, output %f, clarity %f", satValue (norm, 2, smacheratr::kDrive),
               satValue (norm, 2, smacheratr::kPreLimit), satValue (norm, 2, smacheratr::kDryWet), satValue (norm, 2, smacheratr::kMidSide),
               satValue (norm, 2, smacheratr::kOutput), satValue (norm, 2, smacheratr::kClarityFreq));
        // the colour filters were off after the rack (Smacheratr's own default is on)
        CHECK (satValue (norm, 2, smacheratr::kColorOn) == 0.0, "colour off, as it was");
        // and it sounds the same: the old saturator after the rack against the slot
        auto s = sine (220.0, 1.0);
        std::array<double, kNumParams> before {};
        std::array<bool, kNumParams> beforeHas {};
        oldState (before, beforeHas);
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (isTailParam (id))
                before[id] = norm[id];
        before[onId] = 1.0;
        before[slotParam (0, kSlotType)] = norm[slotParam (0, kSlotType)];
        auto render = [&] (const std::array<double, kNumParams>& v) {
            std::unique_ptr<Engine> e (makeEngine (s));
            for (uint32_t id = 0; id < kNumParams; ++id)
                if (isRackParam (id) || isTailParam (id))
                    e->setParam (id, toPlain (id, v[id]));
            for (uint32_t j = 0; j < kSlotBlockAll; ++j) // Para with its defaults in the first slot
                e->setParam (slotBlockParam (0, j), j < para::kNumParams ? para::paramTable ().defaultNormalized (j) : 0.0);
            e->setParam (slotParam (1, kSlotType), (double)kFxEmpty); // (no Multidyn: it would make the two differ in time)
            e->reset (); // (the settings from the start, not glided into)
            e->noteOn (57, 1.0f);
            return std::make_pair (run (*e, 24000), e->latency ());
        };
        std::array<double, kNumParams> after = norm;
        const auto [a, la] = render (before);
        const auto [b, lb] = render (after);
        double diff = 0.0;
        for (size_t i = 0; i < a.l.size (); ++i)
            diff = std::max (diff, (double)std::fabs (a.l[i] - b.l[i]));
        CHECK (la == lb && diff < 1e-4 && rms (a.l, 0, a.l.size ()) > 0.01, "the same sound in the rack: latency %d / %d, difference %g",
               la, lb, diff);
    }
    {
        // off: nothing added, the rack as it was
        oldState (norm, has);
        norm[slotParam (0, kSlotType)] = toNormalized (slotParam (0, kSlotType), kFxWidr);
        norm[onId] = 0.0;
        moveEndSaturatorIntoRack (norm, has);
        CHECK (typeOf (norm, 0) == kFxWidr && typeOf (norm, 1) == kFxEmpty && norm[onId] == 0.0, "off: nothing added");
    }
    {
        // a state without the rack's parameters (before 0.6, its fixed effects not on) and without the
        // saturator's switch (it played on): the rack is empty but for the saturator, in the first slot
        oldState (norm, has);
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (isRackParam (id) || id == onId)
                has[id] = false;
        moveEndSaturatorIntoRack (norm, has);
        CHECK (typeOf (norm, 0) == kFxSmacheratr && typeOf (norm, 1) == kFxEmpty && norm[onId] == 0.0 && has[slotParam (5, kSlotType)],
               "no rack in the state: %d %d", typeOf (norm, 0), typeOf (norm, 1));
    }
    {
        // a gap in the rack: after the last effect, not in the gap
        oldState (norm, has);
        norm[slotParam (0, kSlotType)] = toNormalized (slotParam (0, kSlotType), kFxPara);
        norm[slotParam (3, kSlotType)] = toNormalized (slotParam (3, kSlotType), kFxWubr);
        norm[onId] = 1.0;
        moveEndSaturatorIntoRack (norm, has);
        CHECK (typeOf (norm, 1) == kFxEmpty && typeOf (norm, 4) == kFxSmacheratr, "after the last effect: %d %d", typeOf (norm, 1),
               typeOf (norm, 4));
    }
    {
        // the last slot used: no room, the old saturator stays on (it keeps running after the rack)
        oldState (norm, has);
        norm[slotParam (kRackSlots - 1, kSlotType)] = toNormalized (slotParam (kRackSlots - 1, kSlotType), kFxMsEq);
        norm[onId] = 1.0;
        moveEndSaturatorIntoRack (norm, has);
        CHECK (norm[onId] == 1.0 && typeOf (norm, 0) == kFxEmpty, "no room: it stays after the rack");
        std::unique_ptr<Engine> e (makeEngine (sine (220.0, 0.5)));
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (isRackParam (id) || isTailParam (id))
                e->setParam (id, toPlain (id, norm[id]));
        CHECK (e->latency () > 0, "and it runs (its latency): %d", e->latency ());
        e->setParam (onId, 0.0);
        CHECK (e->latency () == 0, "off: nothing after the rack, no latency: %d", e->latency ());
    }
}

TEST (effects_fuzz)
{
    // every effect on, random settings and automation: finite output
    auto s = sine (220.0, 1.0, 44100.0, true);
    uint32_t seed = 77;
    auto r01 = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (float)((seed >> 8) & 0xFFFFFF) / 16777216.0f;
    };
    for (int iter = 0; iter < 30; ++iter)
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        for (uint32_t id = kRackBase; id < kTailExtBase; ++id)
            e->setParam (id, toPlain (id, r01 ()));
        for (int slot = 0; slot < kRackSlots; ++slot) // every kind of effect somewhere
            e->setParam (slotParam (slot, kSlotType), (double)(1 + (slot + iter) % (kNumFxTypes - 1)));
        e->setParam (kTailBase + pk::kTailOn, 1.0);
        bool finite = true;
        for (int step = 0; step < 6; ++step)
        {
            e->noteOn (36 + (int)(r01 () * 48), 1.0f);
            const uint32_t id = kRackBase + (uint32_t)(r01 () * (kTailExtBase - kRackBase - 1));
            e->setParam (id, toPlain (id, r01 ()));
            auto o = run (*e, 2000);
            for (float v : o.l)
                finite &= std::isfinite (v);
        }
        CHECK (finite, "iteration %d produced non-finite output", iter);
    }
}

TEST (classic_pitch_and_samplerate)
{
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    for (auto [note, expect] : std::vector<std::pair<int, double>> {{60, 440.0}, {72, 880.0}, {48, 220.0}, {67, 440.0 * std::pow (2, 7 / 12.0)}})
    {
        e->reset ();
        e->noteOn (note, 1.0f);
        auto o = run (*e, 12000);
        const double f = freqOf (o.l, 2000, 12000);
        CHECK (std::fabs (f / expect - 1.0) < 0.003, "note %d: %f Hz (want %f)", note, f, expect);
        e->allNotesOff ();
        run (*e, 9600);
    }
    // duration halves an octave up: 1 s sample -> 0.5 s
    e->reset ();
    e->noteOn (72, 1.0f);
    auto o = run (*e, 48000);
    const double end = soundEnd (o.l) / kHostSr;
    CHECK (std::fabs (end - 0.5) < 0.01, "end %f s", end);
}

TEST (classic_loop_sustains_and_releases)
{
    auto s = sine (220.0, 0.5, 44100.0, true);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kLoopOn, 1);
    e->setParam (kLength, 0.5);
    e->setParam (kAmpR, 100.0);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 96000); // 2 s, sample is only 0.5 s
    CHECK (rms (o.l, 90000, 96000) > 0.2, "loop not sustaining: rms %f", rms (o.l, 90000, 96000));
    e->noteOff (60);
    auto rel = run (*e, 24000);
    CHECK (rms (rel.l, 12000, 24000) < 1e-4, "release didn't finish: %f", rms (rel.l, 12000, 24000));
    CHECK (e->activeVoices () == 0, "voices %d", e->activeVoices ());
}

TEST (envelopes_locked_to_the_loop)
{
    // a 1 kHz sine looping over its first 0.25 s, the pitch envelope +12 semitones falling to 0: Off, it
    // falls once; Restart, it starts again at every pass of the loop; Fit, a decay far longer than a pass
    // is squeezed into each pass (so it reaches the bottom before every wrap)
    auto s = sine (1000.0, 1.0, 44100.0);
    auto freqs = [&] (int lock, double decayMs) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kLoopOn, 1);
        e->setParam (kLength, 0.25);
        e->setParam (kPitchA, 0.1);
        e->setParam (kPitchD, decayMs);
        e->setParam (kPitchS, 0.0);
        e->setParam (kPitchEnvAmt, 12.0);
        e->setParam (kPitchLoopLock, lock);
        e->noteOn (60, 1.0f);
        auto o = run (*e, 96000); // 2 s
        std::vector<double> f;
        for (size_t a = 24000; a + 960 <= o.l.size (); a += 960) // 20 ms windows from 0.5 s
            f.push_back (freqOf (o.l, a, a + 960));
        return f;
    };
    auto maxOf = [] (const std::vector<double>& v) { return *std::max_element (v.begin (), v.end ()); };
    auto minOf = [] (const std::vector<double>& v) { return *std::min_element (v.begin (), v.end ()); };
    const double unit = 1000.0; // the sine at its root (the sample's rate is made up for)
    const auto off = freqs (kLoopLockOff, 60.0), restart = freqs (kLoopLockRestart, 60.0);
    std::printf ("    Off: %.0f .. %.0f Hz, Restart: %.0f .. %.0f Hz\n", minOf (off), maxOf (off), minOf (restart), maxOf (restart));
    CHECK (maxOf (off) < unit * 1.05, "Off: the envelope falls once (%.0f Hz at most after 0.5 s)", maxOf (off));
    CHECK (maxOf (restart) > unit * 1.25 && minOf (restart) < unit * 1.05, // (20 ms windows average the fast fall)
           "Restart: up again at every pass (%.0f .. %.0f Hz)", minOf (restart), maxOf (restart));
    // a 20 s decay: Off, still high after 2 s; Fit, back at the root before each wrap
    const auto slow = freqs (kLoopLockOff, 20000.0), fit = freqs (kLoopLockFit, 20000.0);
    std::printf ("    20 s decay: Off %.0f .. %.0f Hz, Fit %.0f .. %.0f Hz\n", minOf (slow), maxOf (slow), minOf (fit), maxOf (fit));
    CHECK (minOf (slow) > unit * 1.5, "Off: a 20 s decay is still high (%.0f Hz)", minOf (slow));
    CHECK (minOf (fit) < unit * 1.1 && maxOf (fit) > unit * 1.5, "Fit: the decay fits a pass (%.0f .. %.0f Hz)", minOf (fit), maxOf (fit));
    // the filter envelope's lock defaults Off as well (old projects keep their sound)
    CHECK (paramTable ().info (kFiltLoopLock).def == kLoopLockOff && paramTable ().info (kPitchLoopLock).def == kLoopLockOff, "Off by default");
}

TEST (loop_crossfade_smooths_discontinuity)
{
    // A ramp has a big jump at any loop boundary.
    std::vector<float> ramp (44100);
    for (size_t i = 0; i < ramp.size (); ++i)
        ramp[i] = -0.5f + (float)i / ramp.size ();
    auto s = SampleData::fromBuffers (ramp, {}, 44100.0);
    auto maxJump = [&] (double fade) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kLoopOn, 1);
        e->setParam (kLength, 0.3);
        e->setParam (kLoopFade, fade);
        e->noteOn (60, 1.0f);
        auto o = run (*e, 144000);
        double j = 0;
        for (size_t i = 60000; i < o.l.size (); ++i)
            j = std::max (j, (double)std::fabs (o.l[i] - o.l[i - 1]));
        return j;
    };
    const double hard = maxJump (0.0), soft = maxJump (0.5);
    CHECK (hard > 0.15, "expected a jump without fade: %f", hard);
    CHECK (soft < hard * 0.2, "fade didn't smooth: %f vs %f", soft, hard);
}

TEST (length_is_the_loop_and_fade_fades_the_start_in)
{
    // a constant level: the loop's crossfade is inaudible, the fade-in at the start is not
    auto s = SampleData::fromBuffers (std::vector<float> (44100, 0.5f), {}, 44100.0);
    auto play = [&] (double length, double fade, bool loop, int frames) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kLoopOn, loop ? 1 : 0);
        e->setParam (kLength, length);
        e->setParam (kLoopFade, fade);
        e->noteOn (60, 1.0f);
        return run (*e, frames);
    };
    CHECK (paramTable ().info (kLoopFade).def == 0.0, "Fade off by default");
    // Length no longer shortens the sample: with Loop off it plays to the end (1 s)
    auto o = play (0.25, 0.0, false, 60000);
    const double end = soundEnd (o.l) / kHostSr;
    CHECK (std::fabs (end - 1.0) < 0.02, "plays to the end flag: %f s", end);
    // with Fade the loop's start fades in on the first pass; without, it starts at once
    const double hard = rms (play (0.3, 0.0, true, 4800).l, 480, 960);
    const double soft = rms (play (0.3, 0.5, true, 4800).l, 480, 960);
    CHECK (hard > 0.3, "no fade: starts at full level (%f)", hard);
    CHECK (soft < hard * 0.4, "fade: the start fades in (%f vs %f)", soft, hard);
    auto held = play (0.3, 0.5, true, 48000);
    CHECK (rms (held.l, 24000, 48000) > hard * 0.9, "then holds the level through the loop (%f)", rms (held.l, 24000, 48000));
}

TEST (start_moved_back_while_playing_does_not_beep)
{
    // Start automated backwards while a note plays (the playhead moving forwards): the loop moves with
    // it. It used to be held at the note's start while its end moved back with Start, leaving a loop of
    // 16 samples: a beep at about 2.8 kHz
    auto s = sine (220.0, 4.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kFilterOn, 0);
    e->setParam (kLoopOn, 1);
    e->setParam (kLoopFade, 0.1);
    e->setParam (kStart, 0.6);
    e->setParam (kLength, 0.1);
    e->noteOn (60, 1.0f);
    std::vector<float> all;
    auto block = [&] (int frames) {
        auto o = run (*e, frames);
        all.insert (all.end (), o.l.begin (), o.l.end ());
    };
    block (22050);
    for (int k = 0; k < 40; ++k)
    {
        e->setParam (kStart, 0.6 - 0.5 * (k + 1) / 40.0); // back to 0.1 over about a second
        block (1102);
    }
    block (22050);
    // a 220 Hz sine's sample-to-sample steps are about 3 % of its level; a beep's are many times that
    double sq = 0.0, dsq = 0.0, worst = 0.0;
    const size_t win = 2205;
    for (size_t a = 22050; a + win < all.size (); a += win)
    {
        double w = 0.0, dw = 0.0;
        for (size_t i = a; i < a + win; ++i)
        {
            w += (double)all[i] * all[i];
            dw += (double)(all[i] - all[i - 1]) * (all[i] - all[i - 1]);
        }
        sq += w;
        dsq += dw;
        if (w > 1e-6)
            worst = std::max (worst, std::sqrt (dw / w));
    }
    std::printf ("    steps / level: %.3f overall, worst 50 ms %.3f\n", std::sqrt (dsq / std::max (1e-12, sq)), worst);
    CHECK (worst < 0.2, "no beep while Start moves back: %.3f", worst);
    CHECK (sq > 1.0, "still playing");
    CHECK (finite (all), "finite");
}

TEST (moved_loop_hands_over_after_its_pass)
{
    // A 220 Hz sine looping over 0.4 .. 0.8 s of 4 s; while the playhead is in it, the loop moves (Start
    // and Length automated, as the mouse moves them): the playhead finishes its pass of the old loop,
    // then goes on in the new one through the loop's crossfade (or, without one, a short crossfade from
    // where it was), with no click. Moved back behind the playhead it used to jump at once.
    auto s = sine (220.0, 4.0);
    struct Move
    {
        double at, start, length; // seconds into the note, the new Start / Length (shares of the 4 s)
    };
    auto play = [&] (double fade, std::vector<Move> moves, double& lastInOld, double& firstInNew, double& worstStep,
                     bool& strayed, double newA, double newB) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kLoopOn, 1);
        e->setParam (kLoopFade, fade);
        e->setParam (kStart, 0.1);
        e->setParam (kLength, 0.1);
        e->noteOn (60, 1.0f);
        lastInOld = firstInNew = -1.0;
        worstStep = 0.0;
        strayed = false;
        float prev = 0.0f;
        size_t m = 0;
        const int block = 64;
        for (int k = 0; k * block < (int)(kHostSr * 1.2); ++k)
        {
            const double t = k * block / kHostSr;
            while (m < moves.size () && moves[m].at <= t)
            {
                e->setParam (kStart, moves[m].start);
                e->setParam (kLength, moves[m].length);
                ++m;
            }
            auto o = run (*e, block);
            for (size_t i = 0; i < o.l.size (); ++i)
            {
                if (k > 0 || i > 0)
                    worstStep = std::max (worstStep, (double)std::fabs (o.l[i] - prev));
                prev = o.l[i];
            }
            float pos = 0.0f;
            if (e->playPositions (&pos, 1) != 1)
                continue;
            const double sec = pos * 4.0;
            const bool inOld = sec >= 0.4 - 1e-3 && sec <= 0.8 + 1e-3, inNew = sec >= newA - 1e-3 && sec <= newB + 1e-3;
            if (firstInNew < 0.0 && inOld)
                lastInOld = t;
            else if (inNew && firstInNew < 0.0 && t > 0.15)
                firstInNew = t;
            else if (!inNew && firstInNew >= 0.0)
                strayed = true; // back out of the new loop
            if (!inOld && !inNew)
                strayed = true;
        }
    };
    // the sine's own steps are at most 0.5 * 2 pi * 220 / 48000 = 0.0144; a click is many times that
    const double sineStep = 0.5 * 2.0 * M_PI * 220.0 / kHostSr;
    for (double fade : {0.1, 0.0})
    {
        double lastOld, firstNew, worst;
        bool strayed;
        // at 0.2 s (the playhead at 0.6 s) the loop moves to 2.4 .. 2.8 s: it plays on to 0.8 s first (its
        // last 40 ms crossfading into the new loop's head, with Fade), 0.2 s later, then the new loop
        play (fade, {{0.2, 0.6, 0.1}}, lastOld, firstNew, worst, strayed, 2.4, 2.8);
        std::printf ("    fade %.1f, moved later: old loop until %.3f s, new from %.3f s, worst step %.4f\n", fade, lastOld, firstNew, worst);
        const double passEnd = 0.4; // seconds into the note: the first pass, 0.4 .. 0.8 s
        CHECK (lastOld > passEnd - 0.01 && lastOld < passEnd + 0.005 && firstNew > 0.0 && firstNew < passEnd + 0.01 && !strayed,
               "fade %.1f: the pass ends (%.3f s, want %.3f) before the new loop (%.3f s)", fade, lastOld, passEnd, firstNew);
        CHECK (worst < sineStep * 2.5, "fade %.1f: no click at the handover (%.4f, the sine's steps %.4f)", fade, worst, sineStep);
        // moved back behind the playhead (to 0 .. 0.2 s): it used to jump at once; now it finishes the pass
        play (fade, {{0.2, 0.0, 0.05}}, lastOld, firstNew, worst, strayed, 0.0, 0.2);
        CHECK (lastOld > passEnd - 0.01 && firstNew > 0.0 && firstNew < passEnd + 0.01 && !strayed && worst < sineStep * 2.5,
               "fade %.1f, moved back: old until %.3f s, new from %.3f s, worst step %.4f", fade, lastOld, firstNew, worst);
        // moved three times within the pass (and resized): it goes where the loop is when the pass ends
        play (fade, {{0.12, 0.3, 0.1}, {0.2, 0.0, 0.1}, {0.28, 0.5, 0.15}}, lastOld, firstNew, worst, strayed, 2.0, 2.6);
        CHECK (lastOld > passEnd - 0.01 && firstNew > 0.0 && firstNew < passEnd + 0.01 && !strayed && worst < sineStep * 2.5,
               "fade %.1f, moved three times: old until %.3f s, new (the last place) from %.3f s, worst step %.4f", fade,
               lastOld, firstNew, worst);
    }
}

// 2 s: a 300 Hz sine, then (from 1 s) a 1200 Hz one; stereo: 300 Hz on the left, 1200 Hz on the right
static std::shared_ptr<SampleData> twoTones (bool stereo)
{
    const double sr = 44100.0;
    std::vector<float> l ((size_t)(2.0 * sr)), r;
    for (size_t i = 0; i < l.size (); ++i)
    {
        const double t = i / sr;
        l[i] = 0.4f * (float)std::sin (2.0 * M_PI * (stereo || t < 1.0 ? 300.0 : 1200.0) * t);
    }
    if (stereo)
    {
        r.resize (l.size ());
        for (size_t i = 0; i < r.size (); ++i)
            r[i] = 0.4f * (float)std::sin (2.0 * M_PI * 1200.0 * i / sr);
    }
    return SampleData::fromBuffers (l, r, sr, "two tones");
}

TEST (playheads_one_is_the_sampler_as_before)
{
    // Playheads 1: the other playheads' settings (Spread, their regions and channels) change nothing,
    // bit for bit (the sound of every older project). (The output was also compared with the build
    // before the playheads over classic, every warp mode, one-shot and slicing scenarios.)
    for (bool warp : {false, true})
    {
        auto render = [&] (bool touch) {
            std::unique_ptr<Engine> e (makeEngine (sine (220.0, 1.0, 44100.0, true)));
            e->setParam (kLoopOn, 1);
            e->setParam (kLength, 0.4);
            e->setParam (kLoopFade, 0.2);
            e->setParam (kWarp, warp ? 1 : 0);
            e->setParam (kWarpMode, kWarpTexture);
            if (touch)
            {
                e->setParam (kHeadSpread, 1.0);
                for (int h = 1; h < kMaxPlayheads; ++h)
                {
                    e->setParam (headParam (h, kHeadStart), 0.1 * h);
                    e->setParam (headParam (h, kHeadLength), 0.3);
                    e->setParam (headChannelParam (h), kHeadRight);
                }
            }
            e->noteOn (60, 1.0f);
            e->noteOn (64, 0.7f);
            Out o = run (*e, 24000, {}, 333);
            o.l.insert (o.l.end (), o.r.begin (), o.r.end ());
            return o.l;
        };
        CHECK (render (false) == render (true), "warp %d: the other playheads' settings change the sound", (int)warp);
    }
    CHECK (paramTable ().info (kPlayheads).def == 0.0 && paramTable ().info (kHeadSpread).def == 0.0 &&
               paramTable ().info (headChannelParam (0)).def == kHeadStereo,
           "1 playhead, centred, stereo by default");
}

TEST (playheads_spread_and_channels)
{
    // the main loop over the 300 Hz half, playhead 2's region the 1200 Hz half (a mono sample: more
    // playheads play it in stereo)
    auto s = twoTones (false);
    auto play = [&] (int heads, double spread, int mode, bool filter, int frames = 24000) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, filter ? 1 : 0);
        e->setParam (kFilterFreq, 200.0);
        e->setParam (kFilterSlope, 1);
        e->setParam (kLoopOn, 1);
        e->setParam (kStart, 0.0);
        e->setParam (kLength, 0.5);
        e->setParam (kPlayheads, heads - 1);
        e->setParam (kHeadSpread, spread);
        e->setParam (headParam (1, kHeadStart), 0.5);
        e->setParam (headParam (1, kHeadLength), 0.5);
        if (mode >= 0)
        {
            e->setParam (kWarp, 1);
            e->setParam (kWarpMode, mode);
            e->setParam (kWarpBeats, 4); // 2 s at 120 BPM: the sample's own speed
        }
        e->noteOn (60, 1.0f);
        Out o = run (*e, frames);
        float pos[8];
        const int n = e->playPositions (pos, 8);
        CHECK (n == heads, "%d playheads shown (want %d)", n, heads);
        if (heads == 2 && n == 2)
            CHECK (pos[0] <= 0.501f && pos[1] >= 0.499f, "the playheads in their regions: %f, %f", pos[0], pos[1]);
        return o;
    };
    // Spread +100 %: the main playhead hard left, the second hard right; -100 %: the other way round
    for (double spread : {1.0, -1.0})
    {
        Out o = play (2, spread, -1, false);
        const double fl = freqOf (o.l, 4800, 24000), fr = freqOf (o.r, 4800, 24000);
        const double wantL = spread > 0 ? 300.0 : 1200.0, wantR = spread > 0 ? 1200.0 : 300.0;
        std::printf ("    Spread %+.0f %%: left %.0f Hz, right %.0f Hz\n", spread * 100.0, fl, fr);
        CHECK (std::fabs (fl - wantL) < wantL * 0.02 && std::fabs (fr - wantR) < wantR * 0.02,
               "Spread %+.0f %%: left %.0f Hz (want %.0f), right %.0f Hz (want %.0f)", spread * 100.0, fl, wantL, fr, wantR);
        CHECK (rms (o.l, 4800) > 0.2 && rms (o.r, 4800) > 0.2, "both sides play: %f, %f", rms (o.l, 4800), rms (o.r, 4800));
    }
    // Spread 0: both playheads in the centre, the same in both channels (both regions together)
    {
        Out o = play (2, 0.0, -1, false);
        // (to rounding: MSVC may compute the two channels' identical arithmetic in a different order)
        double most = 0.0;
        for (size_t i = 0; i < o.l.size (); ++i)
            most = std::max (most, (double)std::fabs (o.l[i] - o.r[i]));
        CHECK (most < 1e-6 && rms (o.l, 4800) > 0.2, "centred: the same both sides (%g apart, rms %f)", most, rms (o.l, 4800));
    }
    // the filter and the envelope are the voice's: a low-pass at 200 Hz takes the 1200 Hz playhead (right)
    // down far more than the 300 Hz one, and both end with the note's release
    {
        Out dry = play (2, 1.0, -1, false), wet = play (2, 1.0, -1, true);
        const double r1200 = rms (wet.r, 4800) / rms (dry.r, 4800), r300 = rms (wet.l, 4800) / rms (dry.l, 4800);
        std::printf ("    low-pass at 200 Hz: the 300 Hz playhead x%.3f, the 1200 Hz one x%.4f\n", r300, r1200);
        CHECK (r1200 < 0.02 && r300 > r1200 * 10.0, "the filter on every playhead: %f, %f", r300, r1200);
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kLoopOn, 1);
        e->setParam (kLength, 0.5);
        e->setParam (kPlayheads, 1);
        e->setParam (kHeadSpread, 1.0);
        e->setParam (headParam (1, kHeadStart), 0.5);
        e->setParam (kAmpA, 300.0);
        e->setParam (kAmpR, 50.0);
        e->noteOn (60, 1.0f);
        Out a = run (*e, 24000);
        CHECK (rms (a.l, 0, 960) < rms (a.l, 19200) * 0.1 && rms (a.r, 0, 960) < rms (a.r, 19200) * 0.1,
               "the attack on both playheads: %f / %f, %f / %f", rms (a.l, 0, 960), rms (a.l, 19200), rms (a.r, 0, 960), rms (a.r, 19200));
        e->noteOff (60);
        Out b = run (*e, 24000);
        CHECK (rms (b.l, 12000) < 1e-4 && rms (b.r, 12000) < 1e-4 && e->activeVoices () == 0, "the release ends both: %f, %f",
               rms (b.l, 12000), rms (b.r, 12000));
    }
    // the granular warp modes (and Beats) render both regions, one each side
    for (int mode : {kWarpTexture, kWarpTones, kWarpBeatsMode})
    {
        Out o = play (2, 1.0, mode, false, 48000);
        const double fl = freqOf (o.l, 9600, 48000), fr = freqOf (o.r, 9600, 48000);
        std::printf ("    warp mode %d: left %.0f Hz, right %.0f Hz\n", mode, fl, fr);
        CHECK (std::fabs (fl - 300.0) < 30.0 && std::fabs (fr - 1200.0) < 120.0 && rms (o.l, 9600) > 0.1 && rms (o.r, 9600) > 0.1,
               "warp mode %d with 2 playheads: left %.0f Hz (%f), right %.0f Hz (%f)", mode, fl, rms (o.l, 9600), fr, rms (o.r, 9600));
    }
    // three and four playheads spread evenly: at 100 % the outer ones at the edges (3: one in the centre)
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kLoopOn, 1);
        e->setParam (kLength, 0.5);
        e->setParam (kPlayheads, 2); // 3
        e->setParam (kHeadSpread, 1.0);
        for (int h = 1; h < 3; ++h) // both extra ones over the 1200 Hz half
        {
            e->setParam (headParam (h, kHeadStart), 0.5);
            e->setParam (headParam (h, kHeadLength), 0.5);
        }
        e->noteOn (60, 1.0f);
        Out o = run (*e, 24000);
        // left: the main playhead (300 Hz) and half the centre one; right: the last one and half the centre
        CHECK (rms (o.l, 4800) > 0.2 && rms (o.r, 4800) > 0.2 && std::fabs (freqOf (o.r, 4800, 24000) - 1200.0) < 24.0,
               "three playheads: %f, %f, right %.0f Hz", rms (o.l, 4800), rms (o.r, 4800), freqOf (o.r, 4800, 24000));
    }
    // Channel: a stereo sample's left (300 Hz) or right (1200 Hz) in both outputs; a mono sample ignores it
    auto st = twoTones (true);
    for (int ch : {kHeadLeft, kHeadRight})
    {
        std::unique_ptr<Engine> e (makeEngine (st));
        e->setParam (kFilterOn, 0);
        e->setParam (kLoopOn, 1);
        e->setParam (headChannelParam (0), ch);
        e->noteOn (60, 1.0f);
        Out o = run (*e, 24000);
        const double want = ch == kHeadLeft ? 300.0 : 1200.0;
        CHECK (o.l == o.r && std::fabs (freqOf (o.l, 4800, 24000) - want) < want * 0.02,
               "Channel %s: %.0f Hz in both", ch == kHeadLeft ? "Left" : "Right", freqOf (o.l, 4800, 24000));
    }
    {
        auto render = [&] (int ch) {
            std::unique_ptr<Engine> e (makeEngine (s));
            e->setParam (kLoopOn, 1);
            e->setParam (headChannelParam (0), ch);
            e->noteOn (60, 1.0f);
            return run (*e, 4800).l;
        };
        CHECK (render (kHeadLeft) == render (kHeadStereo) && render (kHeadRight) == render (kHeadStereo), "a mono sample ignores Channel");
    }
}

TEST (playheads_cpu)
{
    // CPU scales with the playheads: four of them in every voice stay real-time (best of five)
    auto s = sine (220.0, 4.0, 44100.0, true);
    auto timeOnce = [&] (int voices, int heads, bool warp) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kVoices, 14); // 32
        e->setParam (kFilterFreq, 3000.0);
        e->setParam (kFilterSlope, 1);
        e->setParam (kLoopOn, 1);
        e->setParam (kWarp, warp ? 1 : 0);
        e->setParam (kWarpMode, kWarpTexture);
        e->setParam (kPlayheads, heads - 1);
        e->setParam (kHeadSpread, 0.7);
        for (int i = 0; i < voices; ++i)
            e->noteOn (40 + i, 0.8f);
        const pk::testing::CpuClock t0 = pk::testing::cpuClock ();
        run (*e, (int)kHostSr * 4);
        return 100.0 * (double)(pk::testing::cpuClock () - t0) / pk::testing::kCpuClocksPerSec / 4.0;
    };
    auto timeIt = [&] (int voices, int heads, bool warp) {
        double best = 1e9;
        for (int i = 0; i < 5; ++i)
            best = std::min (best, timeOnce (voices, heads, warp));
        return best;
    };
    const double one = timeIt (8, 1, false), four = timeIt (8, 4, false), tex1 = timeIt (4, 1, true), tex4 = timeIt (4, 4, true);
    std::printf ("    CPU, 8 classic voices: %.1f%% with 1 playhead, %.1f%% with 4; 4 texture voices: %.1f%%, %.1f%%\n", one, four,
                 tex1, tex4);
    CHECK (four < 25.0, "8 classic voices with 4 playheads too slow: %.1f%%", four); // (about 13 % here)
    CHECK (tex4 < 25.0, "4 texture voices with 4 playheads too slow: %.1f%%", tex4); // (about 12 % here)
}

TEST (low_pass_does_not_click_when_a_deep_note_stops)
{
    // a deep note through the low-pass at a low cutoff: a new note taking over (one voice) and the
    // release's end used to cut the filter while it still rang (up to +58 dB of clicks over the note's
    // own steps); now the steal fades after the filter and a finished note's filter rings out
    const double sr = 44100.0;
    std::vector<float> saw ((size_t)(4 * sr));
    for (size_t i = 0; i < saw.size (); ++i)
        saw[i] = 0.5f * (float)(2.0 * std::fmod (110.0 * i / sr, 1.0) - 1.0);
    auto s = SampleData::fromBuffers (saw, {}, sr, "saw");
    for (int circuit : {0, 3})
        for (int slope : {0, 1})
        {
            std::unique_ptr<Engine> e (makeEngine (s));
            e->setParam (kVoices, 0); // one voice
            e->setParam (kFilterFreq, 150.0);
            e->setParam (kFilterCircuit, circuit);
            e->setParam (kFilterSlope, slope);
            std::vector<float> all;
            auto block = [&] (int frames) {
                auto o = run (*e, frames);
                all.insert (all.end (), o.l.begin (), o.l.end ());
            };
            block (2400);
            e->noteOn (36, 1.0f);
            block (24000);
            e->noteOn (31, 1.0f);
            block (16800);
            e->noteOff (31);
            block (48000 * 2 - 43200);
            // the second difference in 1 ms windows: the loudest around the events vs. the steady note
            auto peak = [&] (double t0, double t1) {
                double m = 0.0;
                for (size_t a = (size_t)(t0 * kHostSr); a + 48 < (size_t)(t1 * kHostSr); a += 48)
                {
                    double d = 0.0;
                    for (size_t i = a; i < a + 48; ++i)
                    {
                        const double x = all[i] - 2.0 * all[i - 1] + all[i - 2];
                        d += x * x;
                    }
                    m = std::max (m, std::sqrt (d / 48.0));
                }
                return m;
            };
            const double steady = peak (0.2, 0.5);
            const double steal = 20.0 * std::log10 (peak (0.54, 0.58) / steady);
            const double end = 20.0 * std::log10 (std::max (1e-12, peak (0.94, 1.05)) / steady);
            std::printf ("    circuit %d, %s dB: new note %+.1f dB, release end %+.1f dB (vs. the note)\n", circuit, slope ? "24" : "12", steal, end);
            CHECK (steal < 6.0 && end < 0.0, "no click: %+.1f / %+.1f dB", steal, end);
        }
}

TEST (oneshot_trigger_and_gate)
{
    auto s = sine (300.0, 0.5);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kMode, kModeOneShot);
    e->noteOn (60, 1.0f);
    run (*e, 480);
    e->noteOff (60);
    auto o = run (*e, 48000);
    const double end = (soundEnd (o.l) + 480) / kHostSr;
    CHECK (std::fabs (end - 0.5) < 0.01, "trigger should play whole sample, ended at %f", end);

    e->setParam (kTriggerGate, 1);
    e->setParam (kFadeOut, 20.0);
    e->noteOn (60, 1.0f);
    run (*e, 4800);
    e->noteOff (60);
    auto g = run (*e, 4800);
    const double gend = soundEnd (g.l) / kHostSr;
    CHECK (gend < 0.025, "gate should stop within fade-out, ended %f s after note-off", gend);

    // fade in
    e->setParam (kTriggerGate, 0);
    e->setParam (kFadeIn, 100.0);
    e->setParam (kFadeOut, 0.0);
    e->noteOn (60, 1.0f);
    auto f = run (*e, 9600);
    CHECK (rms (f.l, 0, 1200) < 0.5 * rms (f.l, 4800, 9600), "fade in missing: %f vs %f", rms (f.l, 0, 1200),
           rms (f.l, 4800, 9600));

    // one-shot is monophonic
    e->setParam (kFadeIn, 0.0);
    e->noteOn (60, 1.0f);
    e->noteOn (64, 1.0f);
    run (*e, 960);
    CHECK (e->activeVoices () == 1, "one-shot voices %d", e->activeVoices ());
}

TEST (voice_limit_and_retrig)
{
    auto s = sine (200.0, 2.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kVoices, 1); // index 1 -> 2 voices
    e->noteOn (60, 1.0f);
    e->noteOn (64, 1.0f);
    e->noteOn (67, 1.0f);
    run (*e, 960);
    CHECK (e->activeVoices () == 2, "expected 2 voices, got %d", e->activeVoices ());

    e->reset ();
    e->setParam (kVoices, 7);
    e->setParam (kRetrig, 1);
    e->setParam (kAmpR, 2000.0);
    e->noteOn (60, 1.0f);
    e->noteOff (60);
    e->noteOn (60, 1.0f);
    run (*e, 960);
    CHECK (e->activeVoices () == 1, "retrig should cut the old voice, got %d", e->activeVoices ());
    e->reset ();
    e->setParam (kRetrig, 0);
    e->noteOn (60, 1.0f);
    e->noteOff (60);
    e->noteOn (60, 1.0f);
    run (*e, 960);
    CHECK (e->activeVoices () == 2, "without retrig notes overlap, got %d", e->activeVoices ());
}

TEST (spread_is_stereo)
{
    auto s = sine (300.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->noteOn (60, 1.0f);
    auto a = run (*e, 9600);
    double diff = 0;
    for (size_t i = 0; i < a.l.size (); ++i)
        diff = std::max (diff, (double)std::fabs (a.l[i] - a.r[i]));
    CHECK (diff < 1e-6, "mono sample without spread should be centred: %f", diff);
    e->reset ();
    e->setParam (kSpread, 1.0);
    e->noteOn (60, 1.0f);
    run (*e, 9600);
    CHECK (e->activeVoices () == 2, "spread uses two voices, got %d", e->activeVoices ());
    auto b = run (*e, 9600);
    diff = 0;
    for (size_t i = 0; i < b.l.size (); ++i)
        diff = std::max (diff, (double)std::fabs (b.l[i] - b.r[i]));
    CHECK (diff > 0.05, "spread should decorrelate L/R: %f", diff);
}

TEST (pitch_bend_and_transpose)
{
    auto s = sine (440.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setPitchBend (1.0f);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 12000);
    const double want = 440.0 * std::pow (2.0, 5.0 / 12.0);
    CHECK (std::fabs (freqOf (o.l, 2000, 12000) / want - 1) < 0.003, "bend %f", freqOf (o.l, 2000, 12000));
    e->reset ();
    e->setPitchBend (0.0f);
    e->setParam (kTranspose, -12);
    e->setParam (kDetune, 50);
    e->noteOn (60, 1.0f);
    o = run (*e, 12000);
    const double want2 = 220.0 * std::pow (2.0, 0.5 / 12.0);
    CHECK (std::fabs (freqOf (o.l, 2000, 12000) / want2 - 1) < 0.003, "transpose %f", freqOf (o.l, 2000, 12000));
}

TEST (glide_moves_gradually)
{
    auto s = sine (440.0, 2.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kGlideMode, kGlideMono);
    e->setParam (kGlideTime, 200.0);
    e->noteOn (60, 1.0f);
    run (*e, 4800);
    e->noteOn (72, 1.0f); // legato
    auto o = run (*e, 24000);
    CHECK (e->activeVoices () == 1, "mono glide keeps one voice: %d", e->activeVoices ());
    const double f0 = freqOf (o.l, 0, 2400), fmid = freqOf (o.l, 3600, 6000), fend = freqOf (o.l, 14400, 24000);
    CHECK (f0 < 560 && fmid > 500 && fmid < 820 && std::fabs (fend - 880) < 5, "glide %f %f %f", f0, fmid, fend);
    e->noteOff (72); // back to held 60
    auto back = run (*e, 24000);
    CHECK (std::fabs (freqOf (back.l, 14400, 24000) - 440) < 5, "return to held note %f",
           freqOf (back.l, 14400, 24000));
}

TEST (sustain_pedal)
{
    auto s = sine (300.0, 0.5, 44100.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kLoopOn, 1);
    e->setSustain (true);
    e->noteOn (60, 1.0f);
    run (*e, 480);
    e->noteOff (60);
    auto o = run (*e, 9600);
    CHECK (rms (o.l, 4800, 9600) > 0.1, "pedal should hold the note");
    e->setSustain (false);
    o = run (*e, 9600);
    CHECK (rms (o.l, 7200, 9600) < 1e-4, "pedal up should release");
}

TEST (envelope_timing)
{
    auto s = sine (1000.0, 2.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kAmpA, 100.0);
    e->setParam (kFilterOn, 0);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 14400);
    const double early = rms (o.l, 2200, 2600), mid = rms (o.l, 4600, 5000), full = rms (o.l, 9600, 14400);
    CHECK (std::fabs (mid / full - 1.0) < 0.1, "attack should be complete at 100 ms: %f", mid / full);
    CHECK (early / full > 0.35 && early / full < 0.65, "half way through a linear attack: %f", early / full);
    e->setParam (kAmpR, 200.0);
    e->noteOff (60);
    auto r = run (*e, 14400);
    CHECK (soundEnd (r.l, 1e-4f) < 0.21 * kHostSr, "release longer than 200 ms: %zu", soundEnd (r.l, 1e-4f));
    CHECK (e->activeVoices () == 0, "voice should end after release");
}

TEST (amp_envelope_loop)
{
    auto s = sine (1000.0, 3.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kFilterOn, 0);
    e->setParam (kAmpA, 10.0);
    e->setParam (kAmpD, 100.0);
    e->setParam (kAmpS, 0.0);
    e->setParam (kAmpLoopMode, kAmpLoopLoop);
    e->setParam (kAmpLoopTime, 1.0);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 48000);
    // without looping a 0 sustain would be silent after ~110 ms; with looping it pulses on
    CHECK (rms (o.l, 36000, 48000) > 0.05, "loop mode should keep re-triggering: %f", rms (o.l, 36000, 48000));
    e->reset ();
    e->setParam (kAmpLoopMode, kAmpLoopNone);
    e->noteOn (60, 1.0f);
    o = run (*e, 48000);
    CHECK (rms (o.l, 36000, 48000) < 1e-3, "no loop: silent after decay (%f)", rms (o.l, 36000, 48000));
}

TEST (envelope_breakpoints_and_curves)
{
    // attack 10 ms -> peak, breakpoint 1: 100 ms down to 0.2, breakpoint 2: 100 ms up to 0.8,
    // decay 50 ms to sustain 0.5
    Envelope env;
    EnvSettings s;
    s.attackMs = 10.0f;
    s.points = 2;
    s.ptMs[0] = 100.0f;
    s.ptLevel[0] = 0.2f;
    s.ptCurve[0] = 0.0f;
    s.ptMs[1] = 100.0f;
    s.ptLevel[1] = 0.8f;
    s.ptCurve[1] = 0.0f;
    s.decayMs = 50.0f;
    s.sustain = 0.5f;
    const float sr = 1000.0f; // 1 sample per ms
    env.noteOn ();
    std::vector<float> v;
    for (int i = 0; i < 400; ++i)
        v.push_back (env.process (s, sr));
    CHECK (std::fabs (v[9] - 1.0f) < 0.02f, "peak at 10 ms: %f", v[9]);
    CHECK (std::fabs (v[60] - 0.6f) < 0.03f, "halfway down to breakpoint 1 (linear): %f", v[60]);
    CHECK (std::fabs (v[109] - 0.2f) < 0.02f, "breakpoint 1 level: %f", v[109]);
    CHECK (std::fabs (v[209] - 0.8f) < 0.02f, "breakpoint 2 level: %f", v[209]);
    CHECK (std::fabs (v[300] - 0.5f) < 0.02f, "sustain: %f", v[300]);
    CHECK (env.stage == Envelope::Sustain, "stage %d", (int)env.stage);

    // curve bends: negative = fast start, positive = slow start, same end points
    for (float c : {-1.0f, -0.5f, 0.5f, 1.0f})
    {
        CHECK (std::fabs (envCurve (0.0f, c)) < 1e-5f && std::fabs (envCurve (1.0f, c) - 1.0f) < 1e-5f, "ends %f", c);
        CHECK (c < 0 ? envCurve (0.5f, c) > 0.6f : envCurve (0.5f, c) < 0.4f, "bend %f -> %f", c, envCurve (0.5f, c));
    }
    Envelope a, b;
    EnvSettings lin, bent;
    lin.attackMs = bent.attackMs = 100.0f;
    bent.curveA = 0.8f;
    a.noteOn ();
    b.noteOn ();
    float va = 0, vb = 0;
    for (int i = 0; i < 50; ++i)
    {
        va = a.process (lin, sr);
        vb = b.process (bent, sr);
    }
    CHECK (vb < va * 0.6f, "slow-start attack should lag linear at the midpoint: %f vs %f", vb, va);
    // incremental evaluation matches the closed form
    Envelope c3;
    EnvSettings sc;
    sc.attackMs = 0.1f;
    sc.decayMs = 200.0f;
    sc.sustain = 0.0f;
    sc.curveD = -0.7f;
    c3.noteOn ();
    c3.process (sc, sr);
    float worst = 0;
    for (int i = 1; i < 200; ++i)
    {
        const float got = c3.process (sc, sr);
        const float want = 1.0f - envCurve ((float)i / 200.0f, -0.7f);
        worst = std::max (worst, std::fabs (got - want));
    }
    CHECK (worst < 0.01f, "incremental vs closed form error %f", worst);
}

TEST (lfo_tremolo)
{
    auto s = sine (1000.0, 3.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kLfoOn, 1);
    e->setParam (kLfoVol, 1.0);
    e->setParam (kLfoRate, 4.0);
    e->setParam (kLfoRetrig, 1);
    e->noteOn (60, 1.0f);
    auto o = run (*e, 48000);
    double lo = 1e9, hi = 0;
    for (size_t a = 0; a + 1200 <= 48000; a += 1200)
    {
        const double r = rms (o.l, a, a + 1200);
        lo = std::min (lo, r);
        hi = std::max (hi, r);
    }
    CHECK (lo < hi * 0.3, "tremolo depth too small: %f..%f", lo, hi);
}

TEST (filter_attenuates_and_is_stable)
{
    const float sr = 48000.0f;
    for (int circuit = 0; circuit < 5; ++circuit)
        for (int type = 0; type < 5; ++type)
            for (int slope = 0; slope < 2; ++slope)
            {
                MultiFilter f;
                FilterSettings s;
                s.type = type;
                s.circuit = circuit;
                s.slope24 = slope == 1;
                s.cutoff = 500.0f;
                s.res = 1.0f;
                s.driveDb = 24.0f;
                s.morph = 0.4f;
                f.setup (s, sr);
                uint32_t seed = 3;
                float pk = 0.0f;
                bool ok = true;
                for (int i = 0; i < 96000; ++i)
                {
                    if (i % 16 == 0)
                    {
                        s.cutoff = 50.0f + 15000.0f * (0.5f + 0.5f * std::sin (i * 0.0003f));
                        f.setup (s, sr);
                    }
                    const float y = f.process (randomBipolar (seed), 0);
                    ok &= std::isfinite (y);
                    pk = std::max (pk, std::fabs (y));
                }
                CHECK (ok && pk < 40.0f, "circuit %d type %d slope %d unstable, peak %f", circuit, type, slope, pk);
            }
    // attenuation of a 5 kHz tone through a 500 Hz low-pass
    for (int circuit = 0; circuit < 5; ++circuit)
        for (int slope = 0; slope < 2; ++slope)
        {
            MultiFilter f;
            FilterSettings s;
            s.circuit = circuit;
            s.slope24 = slope == 1;
            s.cutoff = 500.0f;
            f.setup (s, sr);
            double in = 0, out = 0;
            for (int i = 0; i < 48000; ++i)
            {
                const float x = 0.1f * std::sin (2.0f * (float)M_PI * 5000.0f * i / sr);
                const float y = f.process (x, 0);
                if (i > 4800)
                {
                    in += x * x;
                    out += y * y;
                }
            }
            const double db = 10 * std::log10 (out / in);
            CHECK (db < (slope ? -55 : -30), "circuit %d slope %d only %f dB", circuit, slope, db);
            // and the display model agrees on the slope
            const float disp = filterResponseDb (s, 5000.0f);
            // Below about -70 dB the measurement hits the float noise floor (MSVC's /fp:fast makes
            // it a few dB higher), so there both only need to agree that the tone is gone.
            CHECK (std::fabs (disp - db) < 6.0 || (disp < -70.0 && db < -66.0),
                   "display %f vs measured %f (circuit %d slope %d)", disp, db, circuit, slope);
        }
}

TEST (filter_in_engine_tracks_cutoff)
{
    auto s = sine (4000.0, 1.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->noteOn (60, 1.0f);
    const double open = rms (run (*e, 9600).l, 4800);
    e->reset ();
    e->setParam (kFilterFreq, 400.0);
    e->setParam (kFilterSlope, 1);
    e->noteOn (60, 1.0f);
    const double closed = rms (run (*e, 9600).l, 4800);
    CHECK (closed < open * 0.01, "closed %f open %f", closed, open);
    // envelope amount opens it again
    e->reset ();
    e->setParam (kFilterEnvAmt, 72.0);
    e->setParam (kFiltS, 1.0);
    e->noteOn (60, 1.0f);
    const double env = rms (run (*e, 9600).l, 4800);
    CHECK (env > open * 0.5, "env amount should open the filter: %f", env);
}

TEST (slicing_transients)
{
    const std::vector<double> times = {0.0, 0.25, 0.5, 0.625, 0.75, 1.0, 1.375, 1.75};
    auto s = bursts (times, 2.0);
    ParamArray p = defaultParams ();
    SliceSettings k;
    k.sliceBy = kSliceTransient;
    k.sensitivity = 0.9;
    k.regionStart = 0;
    k.regionEnd = s->length;
    SliceList sl;
    computeSlices (*s, nullptr, k, sl);
    CHECK (sl.count == (int)times.size (), "found %d slices (want %zu)", sl.count, times.size ());
    for (int i = 0; i < std::min (sl.count, (int)times.size ()); ++i)
    {
        const double err = std::fabs (sl.pos[i] / s->sampleRate - times[(size_t)i]);
        CHECK (err < 0.004, "slice %d at %f s, want %f", i, sl.pos[i] / s->sampleRate, times[(size_t)i]);
    }
    // lower sensitivity finds fewer or equal slices
    k.sensitivity = 0.0;
    SliceList few;
    computeSlices (*s, nullptr, k, few);
    CHECK (few.count <= sl.count && few.count >= 1, "sens 0 -> %d", few.count);

    // Region / Beat
    k.sliceBy = kSliceRegion;
    k.regions = 3; // 16
    computeSlices (*s, nullptr, k, sl);
    CHECK (sl.count == 16, "regions %d", sl.count);
    k.sliceBy = kSliceBeat;
    k.warpBeats = 4;
    k.division = 2; // 1/4 -> one per beat
    computeSlices (*s, nullptr, k, sl);
    CHECK (sl.count == 4, "beat slices %d", sl.count);

    // Manual edits: add one, suppress one
    SliceEdits ed;
    ed.manual.push_back (0.1);
    ed.suppressed.push_back (0.5);
    k.sliceBy = kSliceRegion;
    k.regions = 2; // 8 regions at 0,1/8,...
    computeSlices (*s, &ed, k, sl);
    bool hasManual = false, hasSuppressed = false;
    for (int i = 0; i < sl.count; ++i)
    {
        hasManual |= sl.manual[i] && std::abs (sl.pos[i] - (int)(0.1 * s->length)) < 2;
        hasSuppressed |= std::abs (sl.pos[i] - s->length / 2) < 2;
    }
    CHECK (sl.count == 8 && hasManual && !hasSuppressed, "edits count %d manual %d suppressed %d", sl.count,
           hasManual, hasSuppressed);
    for (int i = 1; i < sl.count; ++i)
        CHECK (sl.pos[i] > sl.pos[i - 1], "slices not sorted");
}

TEST (slicing_playback)
{
    const std::vector<double> times = {0.0, 0.5, 1.0, 1.5};
    auto s = bursts (times, 2.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    e->setParam (kFilterOn, 0);
    e->setParam (kMode, kModeSlicing);
    e->setParam (kSliceBy, kSliceRegion);
    e->setParam (kRegions, 0); // 2 regions? index 0 -> 2
    e->setParam (kRegions, 1); // 4 regions
    CHECK (e->currentSlices ().count == 4, "slices %d", e->currentSlices ().count);
    e->noteOn (kSliceBaseNote + 2, 1.0f);
    auto o = run (*e, 48000);
    const double end = soundEnd (o.l, 1e-4f) / kHostSr;
    CHECK (end > 0.08 && end < 0.52, "slice should stop at next slice, ended %f", end);
    CHECK (e->activeVoices () == 0, "voices %d", e->activeVoices ());
    // Thru continues to the end of the region
    e->setParam (kSlicePlayback, kSliceThru);
    e->noteOn (kSliceBaseNote + 2, 1.0f);
    o = run (*e, 48000);
    CHECK (rms (o.l, 26000, 30000) > 0.01, "thru should reach the next slice's burst");
    // notes outside the slice range are ignored
    e->reset ();
    e->noteOn (kSliceBaseNote + 10, 1.0f);
    e->noteOn (kSliceBaseNote - 1, 1.0f);
    CHECK (e->activeVoices () == 0, "out-of-range notes %d", e->activeVoices ());
    // Poly allows overlapping slices
    e->setParam (kSlicePlayback, kSlicePoly);
    e->noteOn (kSliceBaseNote, 1.0f);
    e->noteOn (kSliceBaseNote + 1, 1.0f);
    run (*e, 64);
    CHECK (e->activeVoices () == 2, "poly slices %d", e->activeVoices ());
}

// Warp: a 2 s sample declared as 4 beats (120 BPM) must follow the host tempo.
TEST (warp_tempo_sync_all_modes)
{
    auto s = sine (440.0, 2.0, 44100.0, true);
    const char* names[] = {"Beats", "Tones", "Texture", "Re-Pitch", "Complex", "Complex Pro"};
    for (int mode = 0; mode < 6; ++mode)
        for (double bpm : {60.0, 240.0})
        {
            std::unique_ptr<Engine> e (makeEngine (s));
            e->setParam (kFilterOn, 0);
            e->setParam (kMode, kModeOneShot);
            e->setParam (kFadeOut, 0.0);
            e->setParam (kWarp, 1);
            e->setParam (kWarpMode, mode);
            e->setParam (kWarpBeats, 4);
            HostInfo h;
            h.bpm = bpm;
            e->noteOn (60, 1.0f);
            auto o = run (*e, (int)(kHostSr * 5), h);
            const double want = 4.0 * 60.0 / bpm;
            const double end = (soundEnd (o.l, 0.01f) + 1) / kHostSr;
            CHECK (std::fabs (end - want) < 0.03 * want + 0.03, "%s @%g BPM: %f s (want %f)", names[mode], bpm, end,
                   want);
            CHECK (finite (o.l) && peak (o.l) < 1.5, "%s peak %f", names[mode], peak (o.l));
            const double f = freqOf (o.l, (size_t)(0.1 * kHostSr), (size_t)(0.8 * want * kHostSr));
            const double wantF = mode == kWarpRePitch ? 440.0 * bpm / 120.0 : 440.0;
            CHECK (std::fabs (f / wantF - 1.0) < 0.02, "%s @%g BPM pitch %f (want %f)", names[mode], bpm, f, wantF);
            if (mode != kWarpRePitch && mode != kWarpBeatsMode && mode != kWarpTexture)
            {
                // steady level (no big grain/frame amplitude modulation)
                const double a = rms (o.l, (size_t)(0.3 * want * kHostSr), (size_t)(0.5 * want * kHostSr));
                const double b = rms (o.l, (size_t)(0.5 * want * kHostSr), (size_t)(0.7 * want * kHostSr));
                CHECK (std::fabs (a / b - 1.0) < 0.1 && a > 0.25, "%s level %f / %f", names[mode], a, b);
            }
        }
}

TEST (warp_transpose_keeps_tempo)
{
    auto s = sine (440.0, 2.0, 44100.0, false);
    for (int mode : {kWarpTones, kWarpTexture, kWarpComplex, kWarpComplexPro, kWarpBeatsMode})
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kMode, kModeOneShot);
        e->setParam (kWarp, 1);
        e->setParam (kWarpMode, mode);
        e->setParam (kWarpBeats, 4);
        e->setParam (kTextureFlux, 0.0);
        HostInfo h;
        h.bpm = 90.0;
        e->noteOn (67, 1.0f); // +7 semitones
        auto o = run (*e, (int)(kHostSr * 4), h);
        const double want = 4.0 * 60.0 / 90.0;
        const double end = (soundEnd (o.l, 0.01f) + 1) / kHostSr;
        CHECK (std::fabs (end - want) < 0.03 * want + 0.03, "mode %d length %f want %f", mode, end, want);
        if (mode != kWarpBeatsMode)
        {
            const double f = freqOf (o.l, (size_t)(0.2 * kHostSr), (size_t)(2.0 * kHostSr));
            const double wantF = 440.0 * std::pow (2.0, 7.0 / 12.0);
            CHECK (std::fabs (f / wantF - 1.0) < 0.02, "mode %d pitch %f want %f", mode, f, wantF);
        }
    }
}

TEST (warp_beats_preserves_transients)
{
    // 4 bursts over 2 s = 1 per beat at 120 BPM. At 60 BPM each burst must start at its beat.
    auto s = bursts ({0.0, 0.5, 1.0, 1.5}, 2.0);
    for (int loopMode : {0, 1, 2})
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kMode, kModeOneShot);
        e->setParam (kWarp, 1);
        e->setParam (kWarpMode, kWarpBeatsMode);
        e->setParam (kBeatsLoop, loopMode);
        e->setParam (kWarpBeats, 4);
        HostInfo h;
        h.bpm = 60.0;
        e->noteOn (60, 1.0f);
        auto o = run (*e, (int)(kHostSr * 4.2), h);
        for (int b = 1; b < 4; ++b)
        {
            // energy right after the beat should be far above the energy just before it (for loop off)
            const size_t at = (size_t)(b * kHostSr);
            const double after = rms (o.l, at, at + 2400);
            CHECK (after > 0.1, "loop %d beat %d missing transient (%f)", loopMode, b, after);
            if (loopMode == 0)
            {
                const double before = rms (o.l, at - 4800, at - 100);
                CHECK (before < after * 0.2, "loop off: gap expected before beat %d (%f vs %f)", b, before, after);
            }
        }
    }
}

TEST (warp_classic_loop)
{
    auto s = sine (440.0, 2.0);
    for (int mode : {kWarpBeatsMode, kWarpComplex, kWarpTones})
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 0);
        e->setParam (kWarp, 1);
        e->setParam (kWarpMode, mode);
        e->setParam (kWarpBeats, 4);
        e->setParam (kLoopOn, 1);
        e->setParam (kLength, 0.5);
        HostInfo h;
        h.bpm = 120.0;
        e->noteOn (60, 1.0f);
        auto o = run (*e, (int)(kHostSr * 6), h);
        CHECK (rms (o.l, (size_t)(5.0 * kHostSr), (size_t)(6.0 * kHostSr)) > 0.2, "mode %d loop should sustain",
               mode);
        CHECK (finite (o.l), "mode %d non-finite", mode);
    }
}

TEST (file_decode_and_ops)
{
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path () / ("smemplr_test_" + std::to_string (std::rand ()));
    fs::create_directories (tmp);
    const std::string dir = tmp.string ();
    const std::string wav = std::string (dir) + "/ramp.wav";
    drwav_data_format fmt {};
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = 2;
    fmt.sampleRate = 22050;
    fmt.bitsPerSample = 32;
    drwav w;
    CHECK (drwav_init_file_write (&w, wav.c_str (), &fmt, nullptr), "open for write");
    std::vector<float> inter (2 * 1000);
    for (int i = 0; i < 1000; ++i)
    {
        inter[(size_t)i * 2] = 0.25f * i / 1000.0f;
        inter[(size_t)i * 2 + 1] = -0.25f * i / 1000.0f;
    }
    drwav_write_pcm_frames (&w, 1000, inter.data ());
    drwav_uninit (&w);

    std::string err;
    auto a = SampleData::load (wav, {}, err);
    CHECK (a && a->length == 1000 && a->numChannels == 2 && a->sampleRate == 22050, "load: %s", err.c_str ());
    if (a)
        CHECK (std::fabs (a->ch[1][500] + 0.125f) < 1e-4, "right channel %f", a->ch[1][500]);

    SampleOps ops;
    ops.cropStart = 0.5;
    ops.cropEnd = 1.0;
    ops.reverse = true;
    ops.normalize = true;
    auto b = SampleData::load (wav, ops, err);
    CHECK (b && b->length == 500, "crop length %d", b ? b->length : -1);
    if (b)
    {
        CHECK (std::fabs (b->ch[0][0] - 1.0f) < 0.01f, "reverse+normalize first %f", b->ch[0][0]);
        CHECK (b->ch[0][0] > b->ch[0][499], "reversed ramp should fall");
    }

    // AIFF via afconvert when available (macOS)
    const std::string aif = std::string (dir) + "/ramp.aif";
    const std::string cmd = "afconvert -f AIFF -d BEI16 '" + wav + "' '" + aif + "' 2>/dev/null";
#if __APPLE__
    if (std::system (cmd.c_str ()) == 0)
#else
    if (false)
#endif
    {
        auto c = SampleData::load (aif, {}, err);
        CHECK (c && c->length == 1000 && c->numChannels == 2, "aiff load: %s", err.c_str ());
    }
    // non-ASCII file names (UTF-8 paths must also work on Windows)
    const std::string uni = dir + "/r\xC3\xBC" "ckw\xC3\xA4" "rts \xE2\x99\xAA.wav";
    std::error_code cec;
    fs::copy_file (pathFromUtf8 (wav), pathFromUtf8 (uni), cec);
    auto u = SampleData::load (uni, {}, err);
    CHECK (!cec && u && u->length == 1000, "unicode path load: %s", err.c_str ());
    // a host's temporary file with no (or an odd) extension: the content says what it is
    for (const char* name : {"/clip.tmp", "/clip"})
    {
        const std::string odd = dir + name;
        std::error_code oec;
        fs::copy_file (pathFromUtf8 (wav), pathFromUtf8 (odd), oec);
        CHECK (!oec && isSupportedAudioFile (odd) && sniffAudioFormat (odd) == "wav", "sniff %s", name);
        auto o = SampleData::load (odd, {}, err);
        CHECK (o && o->length == 1000 && o->numChannels == 2, "load %s: %s", name, err.c_str ());
    }
    {
        const std::string txt = dir + "/notes.txt";
        std::FILE* f = std::fopen (txt.c_str (), "wb");
        if (f)
        {
            std::fputs ("not audio at all", f);
            std::fclose (f);
        }
        CHECK (!isSupportedAudioFile (txt), "text file is not audio");
    }
    // a temporary file is kept (copied once, then reused); a lasting file is left where it is
    {
        CHECK (pk::isTemporaryFile (wav), "the temp folder counts as temporary");
        const fs::path keep = tmp / "kept";
        const std::string k1 = pk::keepIfTemporary (wav, keep), k2 = pk::keepIfTemporary (wav, keep);
        CHECK (k1 != wav && fs::exists (pathFromUtf8 (k1)) && k1 == k2, "kept copy %s", k1.c_str ());
        const std::string lasting = (pk::samplesFolder () / "x.wav").string ();
        CHECK (!pk::isTemporaryFile (lasting), "documents is not temporary: %s", lasting.c_str ());
    }
    auto bad = SampleData::load (std::string (dir) + "/missing.wav", {}, err);
    CHECK (!bad && !err.empty (), "missing file should fail cleanly");
    std::error_code ec;
    fs::remove_all (tmp, ec);
}

#if defined(_WIN32)
// A crash in our code leaves a dump; the crash itself is still passed on (here to the __except).
static int crashHere ()
{
    __try
    {
        *(volatile int*)nullptr = 1;
    }
    __except (1)
    {
        return 1;
    }
    return 0;
}

TEST (crash_dump)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path () / ("smemplr_dumps_" + std::to_string (std::rand ()));
    pk::installCrashDump (dir);
    pk::installCrashDump (dir); // twice is harmless
    try
    {
        throw std::runtime_error ("caught"); // not a crash: no dump
    }
    catch (const std::exception&)
    {
    }
    CHECK (pk::lastCrashDump ().empty (), "a C++ exception is not a crash");
    CHECK (crashHere () == 1, "the crash reaches its handler");
    const std::string dump = pk::lastCrashDump ();
    std::error_code ec;
    const auto size = dump.empty () ? 0 : fs::file_size (pathFromUtf8 (dump), ec);
    CHECK (!dump.empty () && size > 10000, "dump %s (%d bytes)", dump.c_str (), (int)size);
    CHECK (dump.find ("smemplr_tests") != std::string::npos, "named after the module: %s", dump.c_str ());
    fs::remove_all (dir, ec);
}
#endif

TEST (snap_to_zero)
{
    auto s = sine (100.0, 0.1);
    const int z = s->snapToZero (230);
    CHECK (std::fabs (s->ch[0][(size_t)z]) < 0.02f, "snap value %f at %d", s->ch[0][(size_t)z], z);
}

TEST (fuzz_random_params_with_sample)
{
    // Random parameter sets across every mode/warp/filter combination with notes, pitch bend
    // and sustain flying around: the output must stay finite and bounded, and nothing may crash.
    auto drums = bursts ({0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75}, 2.0);
    auto tone = sine (330.0, 1.5, 96000.0, true);
    uint32_t seed = 12345;
    auto r01 = [&] { return 0.5f + 0.5f * randomBipolar (seed); };
    int worst = 0;
    double worstPeak = 0.0;
    for (int iter = 0; iter < 400; ++iter)
    {
        std::unique_ptr<Engine> e (makeEngine (iter % 2 ? drums : tone));
        for (uint32_t id = 0; id < kNumParams; ++id)
            if (r01 () < 0.6f)
                e->setParam (id, toPlain (id, r01 ()));
        e->setParam (kVolume, 0.0);
        e->setParam (kTailBase + pk::kTailOn, 0.0);
        for (int slot = 0; slot < kRackSlots; ++slot)
            e->setParam (slotParam (slot, kSlotType), (double)kFxEmpty);
        HostInfo h;
        h.bpm = 40.0 + 200.0 * r01 ();
        h.playing = r01 () < 0.5f;
        h.ppqValid = true;
        Out total;
        for (int step = 0; step < 12; ++step)
        {
            const int note = 24 + (int)(r01 () * 80);
            if (r01 () < 0.6f)
                e->noteOn (note, r01 ());
            if (r01 () < 0.4f)
                e->noteOff (24 + (int)(r01 () * 80));
            if (r01 () < 0.2f)
                e->setPitchBend (randomBipolar (seed));
            if (r01 () < 0.1f)
                e->setSustain (r01 () < 0.5f);
            if (r01 () < 0.15f) // automate something mid-note
            {
                const uint32_t id = (uint32_t)(r01 () * (kFxParaOn - 1)); // the sampler (the effects have their own fuzz)
                e->setParam (id, toPlain (id, r01 ()));
            }
            auto o = run (*e, 1024 + (int)(r01 () * 3000), h, 1 + (int)(r01 () * 700));
            total.l.insert (total.l.end (), o.l.begin (), o.l.end ());
        }
        const double pk = peak (total.l);
        if (std::getenv ("SIMPLR_FUZZ_DEBUG") && pk > 8.0)
            std::printf ("    iter %d peak %.1f gain %.0f dB res %.2f drive %.0f circuit %d type %d voices %d spread %.2f mode %d warp %d\n",
                         iter, pk, e->param (kGain), e->param (kFilterRes), e->param (kFilterDrive),
                         (int)e->param (kFilterCircuit), (int)e->param (kFilterType), voicesFromIndex ((int)e->param (kVoices)),
                         e->param (kSpread), (int)e->param (kMode), (int)e->param (kWarp));
        if (pk > worstPeak)
        {
            worstPeak = pk;
            worst = iter;
        }
        CHECK (finite (total.l), "iteration %d produced non-finite output", iter);
    }
    std::printf ("    worst peak %.2f (iteration %d)\n", worstPeak, worst);
    CHECK (worstPeak < 64.0, "runaway level %f at iteration %d", worstPeak, worst);
}

// ---------------------------------------------------------------------------
// Far transposition: band-limited reads at any speed, and the high-pass that follows Transpose

// A band-limited sawtooth (every harmonic below Nyquist): a low, bright sound. The period is a
// whole number of samples, so one period is built and repeated.
static std::shared_ptr<SampleData> brightSaw (int period, double secs, double sr = 44100.0)
{
    std::vector<float> one ((size_t)period, 0.0f);
    const double f0 = sr / period;
    for (int k = 1; k * f0 < 0.5 * sr; ++k)
        for (int i = 0; i < period; ++i)
            one[(size_t)i] += (float)(0.3 * std::sin (2.0 * M_PI * k * i / period) / k);
    std::vector<float> l ((size_t)(secs * sr));
    for (size_t i = 0; i < l.size (); ++i)
        l[i] = one[i % (size_t)period];
    return SampleData::fromBuffers (l, {}, sr, "saw");
}

// Power spectrum (Blackman-Harris window) of x[a, a + n), n a power of two.
static std::vector<double> powerSpectrum (const std::vector<float>& x, size_t a, int n)
{
    Fft fft (n);
    std::vector<float> buf ((size_t)n);
    for (int i = 0; i < n; ++i)
    {
        const double t = 2.0 * M_PI * i / n;
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        buf[(size_t)i] = a + i < x.size () ? (float)(x[a + (size_t)i] * w) : 0.0f;
    }
    std::vector<Fft::cf> spec ((size_t)fft.bins ());
    fft.forward (buf.data (), spec.data ());
    std::vector<double> p ((size_t)fft.bins ());
    for (size_t k = 0; k < p.size (); ++k)
        p[k] = (double)std::norm (spec[k]);
    return p;
}

// dB of the power in [fLo, fHi) relative to the whole spectrum.
static double bandDb (const std::vector<double>& p, int n, double fLo, double fHi, double sr = kHostSr)
{
    double band = 1e-30, all = 1e-30;
    for (size_t k = 1; k < p.size (); ++k)
    {
        const double f = (double)k * sr / n;
        all += p[k];
        if (f >= fLo && f < fHi)
            band += p[k];
    }
    return 10.0 * std::log10 (band / all);
}

static void emptyRack (Engine& e)
{
    for (int slot = 0; slot < kRackSlots; ++slot)
        e.setParam (slotParam (slot, kSlotType), (double)kFxEmpty);
    e.setParam (kFilterOn, 0.0);
}

// The sample read the way every reader did before the levels: readSinc alone, its band narrowed with
// the speed down to a quarter (kMinCutoff).
static std::vector<float> readTheOldWay (const SampleData& s, double start, double rate, int n)
{
    std::vector<float> out ((size_t)n);
    const float cutoff = (float)std::min (1.0, 1.0 / rate);
    float r;
    for (int i = 0; i < n; ++i)
        readSinc (s.data (0), nullptr, s.length, start + rate * i, cutoff, out[(size_t)i], r);
    return out;
}
static std::vector<float> readWithLevels (const SampleData& s, double start, double rate, int n)
{
    std::vector<float> out ((size_t)n);
    const SampleReader rd (s, rate);
    float r;
    for (int i = 0; i < n; ++i)
        rd.read (start + rate * i, out[(size_t)i], r);
    return out;
}

TEST (transpose_up_48_has_no_low_junk)
{
    // A 55 Hz saw transposed up 48 semitones has its fundamental at 882 Hz: nothing may sound below
    // it. Reading 16 samples per output sample needs a 1/16 band; readSinc narrowed it only to 1/4, so
    // what was between folded down across the spectrum (aliasing), a lot of it into the low end.
    auto s = brightSaw (800, 8.0); // 55.125 Hz
    const int n = 16384;
    const double rate = 16.0 * 44100.0 / kHostSr;
    {
        const double before = bandDb (powerSpectrum (readTheOldWay (*s, 1000.0, rate, n), 0, n), n, 20.0, 800.0);
        const double after = bandDb (powerSpectrum (readWithLevels (*s, 1000.0, rate, n), 0, n), n, 20.0, 800.0);
        std::printf ("    reader +48 st: power below the fundamental (20..800 Hz) %.1f dB before, %.1f dB with the levels\n",
                     before, after);
        CHECK (before > -40.0, "the old way was cleaner than expected: %.1f dB", before);
        CHECK (after < before - 50.0 && after < -90.0, "low junk %.1f dB (before %.1f)", after, before);
    }
    // through the engine, in the modes that resample (Beats loops segment tails, which modulates the
    // sound on its own: not measured here). The phase vocoder (Complex, Complex Pro) gets a 220 Hz saw:
    // its 2048-sample frame does not resolve a 55 Hz saw's partials (2.6 bins apart), and stretched it
    // adds its own sub-harmonic (f0 / 2, about -53 dB) at every transposition from +7 up, levels or not.
    // The old way folded -22 dB (Complex) and -29 dB (Complex Pro) of the 220 Hz saw below its fundamental.
    auto s220 = brightSaw (200, 8.0); // 220.5 Hz: 3528 Hz at +48
    auto warped = [&] (std::shared_ptr<SampleData> smp, int mode) {
        std::unique_ptr<Engine> e (makeEngine (smp));
        emptyRack (*e);
        e->setParam (kTranspose, 48.0);
        if (mode >= 0)
        {
            e->setParam (kWarp, 1.0);
            e->setParam (kWarpBeats, 16.0); // the sample is 8 s: 120 BPM
            e->setParam (kWarpMode, mode);
        }
        e->noteOn (60, 1.0f);
        return run (*e, 8000 + n);
    };
    const int modes[] = {-1, kWarpTones, kWarpComplex, kWarpComplexPro};
    const char* names[] = {"classic", "tones", "complex", "complex pro"};
    for (int m = 0; m < 4; ++m)
    {
        const double below = m >= 2 ? 3000.0 : 800.0;
        const double junk = bandDb (powerSpectrum (warped (m >= 2 ? s220 : s, modes[m]).l, 8000, n), n, 20.0, below);
        std::printf ("    %-11s +48 st: power below the fundamental (20..%.0f Hz) %.1f dB\n", names[m], below, junk);
        // Complex Pro's formant correction has an artifact of its own a little below the fundamental
        // (about -40 dB from +24 up the old way, -51 dB here: on a level its envelope comes from the
        // sample, once per quarter frame); its fold-down is measured below, on its own
        const double limit[] = {-90.0, -60.0, -80.0, -45.0};
        CHECK (junk < limit[m], "%s: low junk %.1f dB", names[m], junk);
    }
    // The fold-down itself in the vocoder's modes: a 3100 Hz tone over the 220 Hz one lands at 49.6 kHz
    // at +48, past the output's Nyquist; the old way folded it down to 1600 Hz, now it is gone.
    {
        std::vector<float> x ((size_t)(8 * 44100));
        for (size_t i = 0; i < x.size (); ++i)
            x[i] = (float)(0.4 * std::sin (2.0 * M_PI * 220.5 * (double)i / 44100.0) +
                           0.2 * std::sin (2.0 * M_PI * 3100.0 * (double)i / 44100.0));
        auto two = SampleData::fromBuffers (x, {}, 44100.0, "two tones");
        for (int m = 2; m < 4; ++m)
        {
            const double folded = bandDb (powerSpectrum (warped (two, modes[m]).l, 8000, n), n, 1560.0, 1640.0);
            std::printf ("    %-11s +48 st: a tone past Nyquist, folded down to 1600 Hz: %.1f dB\n", names[m], folded);
            CHECK (folded < -90.0, "%s: folded down %.1f dB", names[m], folded);
        }
    }
}

TEST (reads_are_band_limited_at_any_speed)
{
    // Sines read at 1x to 64x: a tone that lands above the output's Nyquist must be gone (not folded
    // down), one below it must pass at its level. Rates in the crossfades between levels too.
    const double rates[] = {1.0, 1.5, 1.8, 2.0, 3.0, 3.7, 4.0, 6.0, 7.5, 8.0, 12.0, 15.0, 16.0, 24.0, 30.0, 32.0, 48.0, 60.0, 64.0};
    const int n = 8192;
    double worstStop = -300.0, worstStopOld = -300.0, worstPass = 0.0, worstSpur = -300.0;
    for (double rate : rates)
    {
        // output frequencies (cycles per output sample): in the band, and past Nyquist (0.5)
        for (double fo : {0.02, 0.1, 0.25, 0.33, 0.66, 0.9, 1.7, 3.3})
        {
            const double f = fo / rate; // in the sample
            if (f >= 0.45)
                continue;
            std::vector<float> x ((size_t)(rate * n + 4096));
            for (size_t i = 0; i < x.size (); ++i)
                x[i] = (float)std::sin (2.0 * M_PI * f * (double)i);
            auto s = SampleData::fromBuffers (x, {}, 48000.0);
            auto y = readWithLevels (*s, 1024.0, rate, n);
            auto p = powerSpectrum (y, 0, n);
            double all = 0.0;
            for (double v : p)
                all += v;
            // a full-scale sine's power through this window and FFT
            double ref = 0.0;
            {
                std::vector<float> t ((size_t)n);
                for (int i = 0; i < n; ++i)
                    t[(size_t)i] = (float)std::sin (2.0 * M_PI * 0.1 * i);
                for (double v : powerSpectrum (t, 0, n))
                    ref += v;
            }
            const double levelDb = 10.0 * std::log10 (all / ref + 1e-30);
            if (fo > 0.5)
            {
                // above 1.3x Nyquist readSinc's own kernel is past its transition band
                if (fo > 0.65)
                {
                    worstStop = std::max (worstStop, levelDb);
                    auto yo = readTheOldWay (*s, 1024.0, rate, n);
                    double allOld = 0.0;
                    for (double v : powerSpectrum (yo, 0, n))
                        allOld += v;
                    worstStopOld = std::max (worstStopOld, 10.0 * std::log10 (allOld / ref + 1e-30));
                    CHECK (levelDb < -70.0, "rate %.1f: a tone at %.2f of the output rate reads at %.1f dB", rate, fo, levelDb);
                }
            }
            else
            {
                if (fo <= 0.25)
                {
                    worstPass = std::max (worstPass, std::fabs (levelDb));
                    CHECK (std::fabs (levelDb) < 0.1, "rate %.1f: a tone at %.2f reads at %.2f dB", rate, fo, levelDb);
                }
                // everything but the tone, in the band below 0.35 of the output rate (above it readSinc's
                // own transition band lets some through: the very top of the spectrum)
                const int k0 = (int)std::lround (fo * n);
                double spur = 1e-30;
                for (int k = 1; k < (int)(0.35 * n); ++k)
                    if (std::abs (k - k0) > 8)
                        spur += p[(size_t)k];
                const double spurDb = 10.0 * std::log10 (spur / all);
                worstSpur = std::max (worstSpur, spurDb);
                CHECK (spurDb < -80.0, "rate %.1f: tone at %.2f: %.1f dB of other things below 0.35", rate, fo, spurDb);
            }
        }
    }
    std::printf ("    past Nyquist: at most %.1f dB (the old way: %.1f dB); in the band: level within %.3f dB, "
                 "anything else at most %.1f dB\n",
                 worstStop, worstStopOld, worstPass, worstSpur);
}

TEST (reads_at_up_to_9_semitones_are_as_before)
{
    // up to 2^0.75 times real time the sample itself is read, exactly as readSinc always did
    auto s = brightSaw (800, 1.0);
    for (double rate : {0.1, 0.5, 0.91875, 1.0, 1.2, 1.5, 1.68})
    {
        auto a = readTheOldWay (*s, 100.25, rate, 4000);
        auto b = readWithLevels (*s, 100.25, rate, 4000);
        CHECK (a == b, "rate %.3f reads differently", rate);
    }
}

TEST (bend_across_levels_is_smooth)
{
    // A slow bend over two octaves crosses three level boundaries: no step anywhere. A step would show
    // in the second difference, which for a sine of amplitude A and w radians per sample is at most w^2 A.
    // Long enough that the read never reaches the end (about 7.7 s of it are read): a loop's wrap is a
    // step of its own when the loop does not crossfade (Loop Fade 0), at any speed, and is not measured.
    auto s = sine (100.0, 10.0, 48000.0);
    std::unique_ptr<Engine> e (makeEngine (s));
    emptyRack (*e);
    e->setParam (kTranspose, 8.0);
    e->setParam (kPbRange, 26.0);
    e->noteOn (60, 1.0f);
    const int total = 96000, block = 32;
    Out o;
    o.l.resize ((size_t)total);
    o.r.resize ((size_t)total);
    std::vector<double> semis ((size_t)total);
    for (int pos = 0; pos < total; pos += block)
    {
        const float bend = std::min (1.0f, (float)pos / (float)(total - 4800));
        e->setPitchBend (bend);
        e->render (o.l.data () + pos, o.r.data () + pos, block, {});
        for (int i = 0; i < block; ++i)
            semis[(size_t)(pos + i)] = 8.0 + 26.0 * bend;
    }
    const double amp = 0.5;
    double worst = 0.0;
    int at = 0;
    for (int i = 4800; i < total; ++i)
    {
        const double w = 2.0 * M_PI * 100.0 * std::exp2 (std::max (semis[(size_t)i], semis[(size_t)i - 2]) / 12.0) / kHostSr;
        const double d2 = std::fabs ((double)o.l[(size_t)i] - 2.0 * o.l[(size_t)i - 1] + o.l[(size_t)i - 2]);
        const double ratio = d2 / (w * w * amp);
        if (ratio > worst)
        {
            worst = ratio;
            at = i;
        }
    }
    // and the level stays put (the levels are flat where this tone is)
    double lo = 1e9, hi = 0.0;
    for (int w0 = 4800; w0 + 2400 <= total; w0 += 2400)
    {
        const double a = peak (std::vector<float> (o.l.begin () + w0, o.l.begin () + w0 + 2400));
        lo = std::min (lo, a);
        hi = std::max (hi, a);
    }
    std::printf ("    +8 .. +34 st: largest second difference %.3f of a clean sine's (at %.1f st); level %.4f .. %.4f\n",
                 worst, semis[(size_t)at], lo, hi);
    CHECK (worst < 1.05, "a step while bending: %.3f at %.1f st", worst, semis[(size_t)at]);
    CHECK (hi / lo < 1.003, "the level moves while bending: %.4f .. %.4f", lo, hi);
}

TEST (transpose_hp_follows_the_pitch)
{
    // A tone at the high-pass's base frequency stays at its cutoff whatever the transposition and bend:
    // -6 dB for the Linkwitz-Riley slopes (Para's 12, 24, 36, 48), -3 dB for 6 and 18 dB.
    auto s = sine (100.0, 1.0, 48000.0);
    const double wantDb[] = {-3.01, -6.02, -3.01, -6.02, -6.02, -6.02};
    for (int slope = 0; slope < 6; ++slope)
        for (int t : {-12, 0, 24, 48})
            for (float bend : {0.0f, 1.0f})
            {
                std::unique_ptr<Engine> e (makeEngine (s));
                emptyRack (*e);
                e->setParam (kLoopOn, 1.0);
                e->setParam (kTranspose, t);
                e->setParam (kPbRange, 7.0);
                e->setParam (kTransHpOn, 1.0);
                e->setParam (kTransHpFreq, 100.0);
                e->setParam (kTransHpSlope, slope);
                e->setPitchBend (bend);
                e->noteOn (60, 1.0f);
                auto o = run (*e, 24000);
                const double f = 100.0 * std::exp2 ((t + 7.0 * bend) / 12.0);
                const double db = 20.0 * std::log10 (toneAmp (o.l, f, 12000, 24000) / 0.5);
                CHECK (std::fabs (db - wantDb[slope]) < 0.25, "slope %d, %+d st, bend %.0f: %.2f dB at the cutoff", slope, t, bend, db);
                if (slope == 3 && bend == 0.0f)
                    std::printf ("    24 dB, base 100 Hz, %+d st: the tone at %.0f Hz %.2f dB\n", t, f, db);
            }
    // an octave below the cutoff: 24 dB down (and more) at 24 dB, whatever the transposition
    {
        auto low = sine (50.0, 1.0, 48000.0);
        for (int t : {0, 36})
        {
            std::unique_ptr<Engine> e (makeEngine (low));
            emptyRack (*e);
            e->setParam (kLoopOn, 1.0);
            e->setParam (kTranspose, t);
            e->setParam (kTransHpOn, 1.0);
            e->setParam (kTransHpFreq, 100.0);
            e->setParam (kTransHpSlope, kTransHp24);
            e->noteOn (60, 1.0f);
            auto o = run (*e, 24000);
            const double db = 20.0 * std::log10 (toneAmp (o.l, 50.0 * std::exp2 (t / 12.0), 12000, 24000) / 0.5);
            std::printf ("    24 dB, an octave below the cutoff, %+d st: %.1f dB\n", t, db);
            CHECK (db < -24.0 && db > -30.0, "%+d st: %.1f dB an octave below", t, db);
        }
    }
    // while bending the cutoff glides with the pitch: the tone at the cutoff stays at -6 dB
    {
        std::unique_ptr<Engine> e (makeEngine (s));
        emptyRack (*e);
        e->setParam (kLoopOn, 1.0);
        e->setParam (kTranspose, 24.0);
        e->setParam (kPbRange, 12.0);
        e->setParam (kTransHpOn, 1.0);
        e->setParam (kTransHpFreq, 100.0);
        e->setParam (kTransHpSlope, kTransHp24);
        e->noteOn (60, 1.0f);
        Out o;
        o.l.resize (96000);
        o.r.resize (96000);
        for (int pos = 0; pos < 96000; pos += 64)
        {
            e->setPitchBend ((float)std::sin (2.0 * M_PI * pos / 96000.0)); // up an octave, down one, back
            e->render (o.l.data () + pos, o.r.data () + pos, 64, {});
        }
        // the level through the bend, by 20 ms windows
        double lo = 1e9, hi = 0.0;
        for (int w0 = 4800; w0 + 960 <= 96000; w0 += 960)
        {
            const double a = peak (std::vector<float> (o.l.begin () + w0, o.l.begin () + w0 + 960));
            lo = std::min (lo, a);
            hi = std::max (hi, a);
        }
        std::printf ("    bending +-12 st at +24: level %.3f .. %.3f (-6 dB is %.3f)\n", lo, hi, 0.5 * 0.5012);
        CHECK (lo > 0.23 && hi < 0.27, "the level through the bend: %.3f .. %.3f", lo, hi);
    }
}

TEST (transpose_hp_off_changes_nothing)
{
    // Off (the default), the output is what it always was: the whole default chain (the rack's
    // Smacheratr, the filter) sounds the same whatever the high-pass's frequency and slope, in every mode.
    auto s = brightSaw (800, 2.0);
    CHECK (paramInfo (kTransHpOn).def == 0.0, "the high-pass is on by default");
    for (int mode : {-1, (int)kWarpBeatsMode, (int)kWarpTones, (int)kWarpComplex})
    {
        auto render = [&] (bool touch) {
            std::unique_ptr<Engine> e (makeEngine (s));
            e->setParam (kFilterOn, 1.0);
            e->setParam (kTranspose, 19.0);
            e->setParam (kPitchEnvAmt, 5.0);
            if (mode >= 0)
            {
                e->setParam (kWarp, 1.0);
                e->setParam (kWarpMode, mode);
            }
            if (touch)
            {
                e->setParam (kTransHpFreq, 200.0);
                e->setParam (kTransHpSlope, kTransHp6);
            }
            e->noteOn (60, 1.0f);
            e->setPitchBend (0.3f);
            return run (*e, 12000);
        };
        const Out a = render (false), b = render (true);
        CHECK (a.l == b.l && a.r == b.r, "mode %d: off, the high-pass's settings change the sound", mode);
    }
    // Switched off mid-note it stops at once: from there on the voice is what it would have been. (Measured
    // with nothing stateful after the voice's source, as the rack and the filter keep what they heard.)
    auto render = [&] (bool onAWhile) {
        std::unique_ptr<Engine> e (makeEngine (s));
        emptyRack (*e);
        e->setParam (kTranspose, 19.0);
        e->setParam (kTransHpFreq, 200.0); // 600 Hz at +19: the saw's lowest partials go
        e->noteOn (60, 1.0f);
        if (onAWhile)
            e->setParam (kTransHpOn, 1.0);
        Out a = run (*e, 12000);
        e->setParam (kTransHpOn, 0.0);
        Out b = run (*e, 12000);
        return std::make_pair (a, b);
    };
    auto plain = render (false), toggled = render (true);
    CHECK (plain.second.l == toggled.second.l && plain.second.r == toggled.second.r, "off is not what it was");
    CHECK (rms (plain.first.l, 2000) > 1.2 * rms (toggled.first.l, 2000), "on does nothing: %f vs %f",
           rms (plain.first.l, 2000), rms (toggled.first.l, 2000));
}

TEST (far_transpose_fuzz)
{
    // Every mode at extreme transpositions, bends, pitch envelopes and LFOs, the high-pass on at random
    // slopes: finite and bounded.
    auto drums = bursts ({0.0, 0.25, 0.5, 0.75}, 1.0);
    auto saw = brightSaw (300, 1.5, 96000.0);
    uint32_t seed = 777;
    auto r01 = [&] { return 0.5f + 0.5f * randomBipolar (seed); };
    double worstPeak = 0.0;
    for (int iter = 0; iter < 120; ++iter)
    {
        std::unique_ptr<Engine> e (makeEngine (iter % 2 ? drums : saw));
        emptyRack (*e);
        e->setParam (kMode, (double)(iter % 3));
        e->setParam (kWarp, r01 () < 0.5f ? 1.0 : 0.0);
        e->setParam (kWarpMode, (double)std::min (5, (int)(r01 () * 6)));
        e->setParam (kTranspose, std::round (-48.0 + 96.0 * r01 ()));
        e->setParam (kPbRange, std::round (48.0 * r01 ()));
        e->setParam (kPitchEnvAmt, -48.0 + 96.0 * r01 ());
        e->setParam (kLfoOn, r01 () < 0.5f ? 1.0 : 0.0);
        e->setParam (kLfoPitch, r01 ());
        e->setParam (kLfoRate, 0.1 + 20.0 * r01 ());
        e->setParam (kTransHpOn, r01 () < 0.7f ? 1.0 : 0.0);
        e->setParam (kTransHpFreq, 10.0 + 190.0 * r01 ());
        e->setParam (kTransHpSlope, (double)std::min (5, (int)(r01 () * 6)));
        e->setParam (kLoopOn, r01 () < 0.5f ? 1.0 : 0.0);
        HostInfo h;
        h.playing = true;
        h.ppqValid = true;
        double pk = 0.0;
        bool ok = true;
        for (int step = 0; step < 16; ++step)
        {
            if (r01 () < 0.5f)
                e->noteOn (36 + (int)(r01 () * 60), r01 ());
            if (r01 () < 0.3f)
                e->noteOff (36 + (int)(r01 () * 60));
            e->setPitchBend (randomBipolar (seed));
            if (r01 () < 0.2f)
                e->setParam (kTranspose, std::round (-48.0 + 96.0 * r01 ()));
            auto o = run (*e, 512 + (int)(r01 () * 2000), h, 1 + (int)(r01 () * 300));
            ok = ok && finite (o.l) && finite (o.r);
            pk = std::max (pk, peak (o.l));
        }
        CHECK (ok, "iteration %d produced non-finite output", iter);
        worstPeak = std::max (worstPeak, pk);
    }
    std::printf ("    worst peak %.2f\n", worstPeak);
    CHECK (worstPeak < 32.0, "runaway level %f", worstPeak);
}

TEST (far_transpose_cpu_and_memory)
{
    // building the levels of a minute of stereo, and what a voice costs read far up (+48) with and
    // without the high-pass, next to the old way of reading
    {
        std::vector<float> l ((size_t)(60 * 44100)), r (l.size ());
        uint32_t seed = 5;
        for (size_t i = 0; i < l.size (); ++i)
        {
            l[i] = 0.5f * randomBipolar (seed);
            r[i] = 0.5f * randomBipolar (seed);
        }
        const auto t0 = std::chrono::steady_clock::now ();
        auto s = SampleData::fromBuffers (l, r, 44100.0);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
        size_t extra = 0;
        for (const auto& m : s->mips)
            extra += (size_t)m.length;
        std::printf ("    a minute of stereo: loaded (levels and analysis) in %.2f s; the levels add %.2fx its memory\n",
                     secs, (double)extra / s->length);
        CHECK (s->levels () == 1 + SampleData::kMipLevels, "levels %d", s->levels ());
    }
    auto s = sine (110.0, 2.0, 44100.0, true);
    auto timeIt = [&] (double transpose, bool hp, int warpMode, int voices) {
        std::unique_ptr<Engine> e (makeEngine (s));
        emptyRack (*e);
        e->setParam (kVoices, 14); // 32
        e->setParam (kLoopOn, 1);
        e->setParam (kTranspose, transpose);
        e->setParam (kTransHpOn, hp ? 1.0 : 0.0);
        e->setParam (kTransHpSlope, kTransHp48);
        e->setParam (kWarp, warpMode >= 0 ? 1.0 : 0.0);
        e->setParam (kWarpMode, std::max (0, warpMode));
        for (int i = 0; i < voices; ++i)
            e->noteOn (48 + i % 12, 0.8f);
        // CPU time (other programs running do not count), the best of five renders (one shared CI machine slowed a single render to 2.5 times its usual time)
        double best = 1e9;
        for (int k = 0; k < 5; ++k)
        {
            const pk::testing::CpuClock t0 = pk::testing::cpuClock ();
            run (*e, (int)kHostSr * 2);
            best = std::min (best, (double)(pk::testing::cpuClock () - t0) / pk::testing::kCpuClocksPerSec);
        }
        return 100.0 * best / 2.0; // % of one core in real time
    };
    const double at0 = timeIt (0.0, false, -1, 32), at48 = timeIt (48.0, false, -1, 32), hp48 = timeIt (48.0, true, -1, 32);
    const double pv0 = timeIt (0.0, false, kWarpComplex, 8), pv48 = timeIt (48.0, false, kWarpComplex, 8);
    const double pro0 = timeIt (0.0, false, kWarpComplexPro, 8), pro48 = timeIt (48.0, false, kWarpComplexPro, 8);
    // the old way at +48: readSinc at a quarter band (64 taps a channel), for 32 voices
    double old48;
    {
        const double rate = 16.0 * 44100.0 / kHostSr;
        const pk::testing::CpuClock t0 = pk::testing::cpuClock ();
        float sink = 0.0f;
        for (int v = 0; v < 32; ++v)
        {
            float l, r;
            double pos = 0.0;
            for (int i = 0; i < (int)kHostSr * 2; ++i)
            {
                readSinc (s->data (0), s->data (1), s->length, pos, (float)(1.0 / rate), l, r);
                sink += l + r;
                pos += rate;
                if (pos >= s->length)
                    pos -= s->length;
            }
        }
        old48 = 100.0 * (double)(pk::testing::cpuClock () - t0) / pk::testing::kCpuClocksPerSec / 2.0;
        if (sink == 12345.0f)
            std::printf ("-");
    }
    std::printf ("    CPU, 32 classic voices: %.1f%% at 0 st, %.1f%% at +48 (reading alone the old way: %.1f%%), %.1f%% at +48 "
                 "with the 48 dB high-pass\n",
                 at0, at48, old48, hp48);
    std::printf ("    CPU, 8 complex voices: %.1f%% at 0 st, %.1f%% at +48; 8 complex pro voices: %.1f%% at 0 st, %.1f%% at +48\n",
                 pv0, pv48, pro0, pro48);
    CHECK (at48 < 25.0 && hp48 < 30.0, "too slow: %.1f%% / %.1f%%", at48, hp48);
    // (read the old way, at the sample's rate, 8 complex voices at +48 took about 290%, complex pro 360%;
    // read from the levels with the old FFT about 70 % and 85 %; now about 25 % and 28 % here)
    CHECK (pv48 < 50.0 && pro48 < 55.0, "the vocoder too slow at +48: %.1f%% / %.1f%%", pv48, pro48);
}

// --- the modulation LFOs (Modulation.h) -------------------------------------------------------------

TEST (mod_lfo_shapes)
{
    // each shape at a few phases: Sine, Triangle (0 at the start, up first), the saws, Square (high first),
    // S&H holding its value, Smooth Random gliding from the last value to this cycle's
    auto near = [] (float a, float b) { return std::fabs (a - b) < 1e-5f; };
    CHECK (near (modShape (kModSine, 0.0, 0, 0), 0.0f) && near (modShape (kModSine, 0.25, 0, 0), 1.0f) &&
               near (modShape (kModSine, 0.75, 0, 0), -1.0f),
           "sine");
    CHECK (near (modShape (kModTriangle, 0.0, 0, 0), 0.0f) && near (modShape (kModTriangle, 0.25, 0, 0), 1.0f) &&
               near (modShape (kModTriangle, 0.5, 0, 0), 0.0f) && near (modShape (kModTriangle, 0.75, 0, 0), -1.0f),
           "triangle");
    CHECK (near (modShape (kModSawUp, 0.0, 0, 0), -1.0f) && near (modShape (kModSawUp, 0.5, 0, 0), 0.0f) &&
               near (modShape (kModSawDown, 0.0, 0, 0), 1.0f) && near (modShape (kModSawDown, 0.75, 0, 0), -0.5f),
           "saws");
    CHECK (near (modShape (kModSquare, 0.1, 0, 0), 1.0f) && near (modShape (kModSquare, 0.6, 0, 0), -1.0f), "square");
    CHECK (near (modShape (kModRandom, 0.1, 0.3f, -0.4f), -0.4f) && near (modShape (kModRandom, 0.9, 0.3f, -0.4f), -0.4f), "S&H");
    CHECK (near (modShape (kModSmoothRandom, 0.0, 0.3f, -0.4f), 0.3f) && near (modShape (kModSmoothRandom, 0.5, 0.3f, -0.4f), -0.05f) &&
               std::fabs (modShape (kModSmoothRandom, 0.999999, 0.3f, -0.4f) + 0.4f) < 1e-4f,
           "smooth random");
    // every shape stays in -1 .. 1, the random ones too (over many cycles)
    Modulator m;
    m.prepare (kHostSr);
    m.reset ();
    for (int shape = 0; shape < kNumModShapes; ++shape)
    {
        ParamArray p = defaultParams ();
        p[modLfoParam (0, kModShape)] = shape;
        p[modLfoParam (0, kModRate)] = 37.0;
        float lo = 2.0f, hi = -2.0f;
        for (int i = 0; i < 48000 / 32; ++i)
        {
            m.advance (p.data (), 32, 120.0, 0.0, false);
            lo = std::min (lo, m.value (0));
            hi = std::max (hi, m.value (0));
        }
        CHECK (lo >= -1.0f && hi <= 1.0f && hi - lo > 1.0f, "shape %d: %f .. %f", shape, lo, hi);
    }
}

TEST (mod_lfo_rates_and_sync)
{
    // rising zero crossings of a sine LFO over a second (and a step: the last one is a second in), in
    // steps of 32 samples
    auto cycles = [] (ParamArray p, double bpm, bool playing, int lfo = 0) {
        Modulator m;
        m.prepare (kHostSr);
        m.reset ();
        int n = 0;
        float last = 0.0f;
        double ppq = 0.0;
        for (int i = 0; i <= 48000 / 32; ++i)
        {
            m.advance (p.data (), 32, bpm, ppq, playing);
            ppq += 32 * (bpm > 0.0 ? bpm : 120.0) / 60.0 / kHostSr;
            const float v = m.value (lfo);
            n += i > 0 && last < 0.0f && v >= 0.0f ? 1 : 0;
            last = v;
        }
        return n;
    };
    ParamArray p = defaultParams ();
    CHECK (paramInfo (modLfoParam (0, kModRate)).def == 1.0 && std::lround (paramInfo (modLfoParam (0, kModSync)).def) == 0,
           "an LFO starts free at 1 Hz");
    p[modLfoParam (0, kModRate)] = 3.0;
    CHECK (cycles (p, 120.0, false) == 3, "3 Hz: %d", cycles (p, 120.0, false));
    p[modLfoParam (2, kModRate)] = 7.0;
    CHECK (cycles (p, 120.0, false, 2) == 7, "LFO 3 at 7 Hz: %d", cycles (p, 120.0, false, 2));
    // synced: Sync is Off, then the note lengths (1/4 is index 9 + 1), the Rate does not count
    p[modLfoParam (0, kModSync)] = 10.0;
    CHECK (paramTable ().toText (modLfoParam (0, kModSync), 10.0) == "1/4", "1/4: %s",
           paramTable ().toText (modLfoParam (0, kModSync), 10.0).c_str ());
    CHECK (cycles (p, 120.0, false) == 2, "1/4 at 120 BPM: %d", cycles (p, 120.0, false));
    CHECK (cycles (p, 60.0, false) == 1, "1/4 at 60 BPM: %d", cycles (p, 60.0, false));
    CHECK (cycles (p, 180.0, true) == 3, "1/4 at 180 BPM, playing: %d", cycles (p, 180.0, true));
    CHECK (cycles (p, 0.0, false) == 2, "no tempo: 120 BPM: %d", cycles (p, 0.0, false));
    p[modLfoParam (0, kModSync)] = 14.0; // 1/2
    CHECK (cycles (p, 120.0, false) == 1, "1/2 at 120 BPM: %d", cycles (p, 120.0, false));
    // while the host plays, a synced LFO follows the song position: at beat 17.25 a 1/4 LFO is a quarter
    // of the way through its cycle (a sine at its top), however it got there
    {
        ParamArray q = defaultParams ();
        q[modLfoParam (1, kModSync)] = 10.0;
        Modulator m;
        m.prepare (kHostSr);
        m.reset ();
        m.advance (q.data (), 32, 120.0, 17.25, true);
        CHECK (std::fabs (m.value (1) - 1.0f) < 1e-4f && std::fabs (m.phase (1) - 0.25) < 1e-9, "song position: %f (phase %f)",
               m.value (1), m.phase (1));
        // with Phase at 90 degrees the same place reads half way
        q[modLfoParam (1, kModPhase)] = 90.0;
        m.advance (q.data (), 32, 120.0, 17.25, true);
        CHECK (std::fabs (m.phase (1) - 0.5) < 1e-9, "phase offset: %f", m.phase (1));
    }
    // Retrig: a note starts the LFO again at its Phase (only the LFOs with Retrig on)
    {
        ParamArray q = defaultParams ();
        q[modLfoParam (0, kModRetrig)] = 1.0;
        q[modLfoParam (0, kModPhase)] = 90.0;
        q[modLfoParam (0, kModRate)] = 0.37;
        q[modLfoParam (1, kModRate)] = 0.37;
        Modulator m;
        m.prepare (kHostSr);
        m.reset ();
        for (int i = 0; i < 1000; ++i)
            m.advance (q.data (), 32, 120.0, 0.0, false);
        const double before1 = m.phase (1);
        m.noteOn ();
        m.advance (q.data (), 32, 120.0, 0.0, false);
        CHECK (std::fabs (m.value (0) - 1.0f) < 1e-5f && std::fabs (m.phase (0) - 0.25) < 1e-9, "retriggered at 90 degrees: %f", m.value (0));
        CHECK (std::fabs (m.phase (1) - before1 - 0.37 * 32 / kHostSr) < 1e-9, "LFO 2 (no Retrig) kept running: %f -> %f", before1, m.phase (1));
    }
    // S&H: one value per cycle
    {
        ParamArray q = defaultParams ();
        q[modLfoParam (0, kModShape)] = kModRandom;
        q[modLfoParam (0, kModRate)] = 10.0;
        Modulator m;
        m.prepare (kHostSr);
        m.reset ();
        int changes = 0;
        float last = 0.0f;
        for (int i = 0; i < 48000 / 32; ++i)
        {
            m.advance (q.data (), 32, 120.0, 0.0, false);
            changes += i > 0 && m.value (0) != last ? 1 : 0;
            last = m.value (0);
        }
        CHECK (changes >= 9 && changes <= 10, "S&H at 10 Hz changed %d times in a second", changes);
    }
}

TEST (mod_mapping_edits)
{
    ModMap m;
    CHECK (addMapping (m, 0, kFilterFreq, 0.5, -1) && addMapping (m, 1, kFilterFreq, -0.25, -1), "two LFOs on one target");
    CHECK (!addMapping (m, 0, kFilterFreq, 0.9, -1) && m.list.size () == 2 && m.list[0].depth == 0.5, "the same LFO twice: refused");
    CHECK (!addMapping (m, kModLfos, kVolume, 0.5, -1) && !addMapping (m, 0, kNumParams, 0.5, -1), "no such LFO / parameter");
    CHECK (addMapping (m, 2, kVolume, 3.0, -1) && m.list.back ().depth == 1.0, "depth held to -1 .. 1");
    CHECK (removeMapping (m, 0) && m.list.size () == 2 && m.list[0].lfo == 1 && !removeMapping (m, 5), "remove");
    ModMap full;
    for (int i = 0; i < kMaxModMappings; ++i)
        CHECK (addMapping (full, i % kModLfos, (uint32_t)(kAmpA + i / kModLfos), 0.1, -1), "mapping %d", i);
    CHECK (!addMapping (full, 0, kVolume, 0.1, -1) && (int)full.list.size () == kMaxModMappings, "at most %d", kMaxModMappings);

    // what may be modulated: numbers, not switches, menus, a slot's Type / On, the LFOs themselves
    CHECK (canModulate (kFilterFreq, kFxEmpty) && canModulate (kTranspose, kFxEmpty) && canModulate (kVolume, kFxEmpty), "numbers");
    CHECK (!canModulate (kFilterOn, kFxEmpty) && !canModulate (kMode, kFxEmpty) && !canModulate (kVoices, kFxEmpty), "switches and menus");
    CHECK (!canModulate (modLfoParam (0, kModRate), kFxEmpty) && !canModulate (kTailBase + pk::kTailDrive, kFxEmpty) &&
               !canModulate (kFxParaBase + para::kHpFreq, kFxEmpty) && !canModulate (kNumParams, kFxEmpty),
           "the LFOs, the old parameters");
    const uint32_t hp = slotBlockParam (2, (uint32_t)fxBlockOf (kFxPara, para::kHpFreq));
    CHECK (canModulate (hp, kFxPara) && !canModulate (hp, kFxEmpty) && !canModulate (slotParam (2, kSlotType), kFxPara) &&
               !canModulate (slotParam (2, kSlotOn), kFxPara),
           "a slot's values (while it holds an effect), not its Type or On");
    CHECK (!canModulate (slotBlockParam (2, (uint32_t)fxBlockOf (kFxPara, para::kMovement)), kFxPara), "a slot's menu");
    CHECK (modFxTypeFor (hp, kFxPara) == kFxPara && modFxTypeFor (kVolume, kFxPara) == -1, "fxType");

    // the rack's effects move: their mappings follow, a removed effect's go
    ModMap r;
    addMapping (r, 0, slotBlockParam (1, 3), 0.5, kFxPara);
    addMapping (r, 1, slotBlockParam (2, kSlotBlock + 2), 0.5, kFxWubr); // (in the slot's extension)
    addMapping (r, 2, kVolume, 0.5, -1);
    std::array<int, kRackSlots> moved;
    for (int s = 0; s < kRackSlots; ++s)
        moved[(size_t)s] = s;
    moved[1] = -1; // removed: the ones after it move up
    for (int s = 2; s < kRackSlots; ++s)
        moved[(size_t)s] = s - 1;
    remapSlots (r, moved);
    CHECK (r.list.size () == 2 && r.list[0].target == slotBlockParam (1, kSlotBlock + 2) && r.list[1].target == kVolume,
           "after removing slot 2: %zu, %u", r.list.size (), r.list.empty () ? 0u : r.list[0].target);
}

TEST (mod_mappings_as_bytes)
{
    ModMap m;
    addMapping (m, 0, kFilterFreq, 0.375, -1);
    addMapping (m, 3, slotBlockParam (7, kSlotBlock + 5), -0.125, kFxWubr);
    const auto b = encodeModMap (m);
    ModMap back;
    CHECK (decodeModMap (b.data (), b.size (), back) && back.list.size () == 2 && back.list[0] == m.list[0] && back.list[1] == m.list[1],
           "round trip");
    CHECK (!decodeModMap (b.data (), b.size () - 3, back) && back.list.empty (), "cut short in an entry");
    CHECK (!decodeModMap (b.data (), 8, back) && back.list.empty (), "cut short in the header");
    ModMap none;
    const auto e = encodeModMap (none);
    CHECK (decodeModMap (e.data (), e.size (), back) && back.list.empty (), "none");
    // a later format with longer entries: the fields known are read, the rest skipped
    std::vector<uint8_t> longer;
    auto put32 = [&] (uint32_t v) {
        for (int i = 0; i < 4; ++i)
            longer.push_back ((uint8_t)(v >> (8 * i)));
    };
    put32 (2);
    put32 (2);
    put32 (24);
    for (int i = 0; i < 2; ++i)
    {
        longer.insert (longer.end (), b.begin () + 12 + i * 20, b.begin () + 12 + (i + 1) * 20);
        put32 (0xDEADBEEF);
    }
    CHECK (decodeModMap (longer.data (), longer.size (), back) && back.list.size () == 2 && back.list[1] == m.list[1], "longer entries");
    // a parameter this version does not know is dropped
    ModMap future;
    future.list.push_back ({1, kNumParams + 40, 0.5, -1});
    future.list.push_back ({1, kVolume, 0.5, -1});
    const auto f = encodeModMap (future);
    CHECK (decodeModMap (f.data (), f.size (), back) && back.list.size () == 1 && back.list[0].target == kVolume, "unknown target dropped");
}

TEST (mod_applied_and_clamped)
{
    std::unique_ptr<Engine> e (makeEngine (sine (220.0, 1.0)));
    auto norm = [&] (uint32_t id) { return toNormalized (id, e->param (id)); };
    e->setParam (kFilterFreq, toPlain (kFilterFreq, 0.5));
    e->setParam (modLfoParam (0, kModShape), kModSquare);
    e->setParam (modLfoParam (0, kModRate), 1.0);
    ModMapping m {0, kFilterFreq, 0.25, -1};
    e->setModMappings (&m, 1);
    e->noteOn (60, 1.0f);
    run (*e, Engine::kModStep);
    // the square is high in its first half: the cutoff a quarter of its range up, the base value where it was
    CHECK (std::fabs (norm (kFilterFreq) - 0.75) < 1e-9 && std::fabs (toNormalized (kFilterFreq, e->baseParam (kFilterFreq)) - 0.5) < 1e-9,
           "modulated %f, base %f", norm (kFilterFreq), toNormalized (kFilterFreq, e->baseParam (kFilterFreq)));
    CHECK (e->modWorking (0) && std::fabs (e->modOffset (0) - 0.25f) < 1e-6f, "offset %f", e->modOffset (0));
    // half a second on, low: it glides there (a step of half the range at once would click)
    run (*e, 24000 - 2 * Engine::kModStep);
    const double justBefore = norm (kFilterFreq);
    run (*e, 2 * Engine::kModStep);
    const double justAfter = norm (kFilterFreq);
    CHECK (std::fabs (justBefore - 0.75) < 1e-6 && justAfter > 0.26 && justAfter < 0.74, "the edge glides: %f -> %f", justBefore, justAfter);
    run (*e, 4800);
    CHECK (std::fabs (norm (kFilterFreq) - 0.25) < 1e-6, "low half: %f", norm (kFilterFreq));
    // the host moves the parameter: the modulation goes on around the new value
    e->setParam (kFilterFreq, toPlain (kFilterFreq, 0.4));
    run (*e, Engine::kModStep);
    CHECK (std::fabs (norm (kFilterFreq) - 0.15) < 1e-6, "around the new value: %f", norm (kFilterFreq));
    // held to the range: the square low and a depth of -1 from 0.9 stops at the top (the mapping of the
    // same LFO and target before glides there: changing a depth does not jump); LFO 2's square and a full
    // depth take the volume to its top or bottom
    e->setParam (kFilterFreq, toPlain (kFilterFreq, 0.9));
    e->setParam (modLfoParam (1, kModShape), kModSquare);
    ModMapping deep[2] = {{0, kFilterFreq, -1.0, -1}, {1, kVolume, 1.0, -1}};
    e->setModMappings (deep, 2);
    run (*e, Engine::kModStep);
    CHECK (norm (kFilterFreq) < 0.9, "a new depth glides: %f", norm (kFilterFreq));
    run (*e, 960);
    CHECK (norm (kFilterFreq) == 1.0 && e->param (kVolume) == (e->modLfoValue (1) > 0.0f ? paramInfo (kVolume).max : paramInfo (kVolume).min),
           "held to the range: %f, volume %f", norm (kFilterFreq), e->param (kVolume));
    // two LFOs on one parameter add up
    ModMapping two[2] = {{0, kFilterFreq, 0.1, -1}, {1, kFilterFreq, -0.3, -1}};
    e->setParam (kFilterFreq, toPlain (kFilterFreq, 0.5));
    e->setParam (modLfoParam (1, kModShape), kModSine);
    e->setParam (modLfoParam (1, kModRate), 0.1);
    e->setModMappings (two, 2);
    run (*e, 4800);
    const double both = norm (kFilterFreq);
    const double want = 0.5 + e->modOffset (0) + e->modOffset (1);
    CHECK (std::fabs (both - want) < 1e-6 && std::fabs (e->modOffset (1) + 0.3 * e->modLfoValue (1)) < 1e-3, "two mappings: %f (want %f)",
           both, want);
    // the mappings gone, the parameters are their own values again
    e->setModMappings (nullptr, 0);
    CHECK (std::fabs (norm (kFilterFreq) - 0.5) < 1e-12 && e->param (kVolume) == e->baseParam (kVolume), "unmapped: %f, volume %f",
           norm (kFilterFreq), e->param (kVolume));
    run (*e, 4800);
    CHECK (std::fabs (norm (kFilterFreq) - 0.5) < 1e-12, "stays unmapped: %f", norm (kFilterFreq));
}

TEST (mod_rack_mappings)
{
    // a mapping onto a rack slot's value modulates the effect there; another effect in the slot pauses it
    std::unique_ptr<Engine> e (makeEngine (sine (220.0, 1.0)));
    loadFx (*e, 1, kFxPara);
    const uint32_t hp = slotBlockParam (1, (uint32_t)fxBlockOf (kFxPara, para::kHpFreq));
    e->setParam (hp, 0.3);
    e->setParam (modLfoParam (2, kModShape), kModSquare);
    ModMapping m {2, hp, 0.2, kFxPara};
    e->setModMappings (&m, 1);
    run (*e, Engine::kModStep);
    CHECK (std::fabs (e->param (hp) - 0.5) < 1e-9 && e->modWorking (0), "para's high-pass: %f", e->param (hp));
    loadFx (*e, 1, kFxLevlr);
    e->setParam (hp, 0.3);
    run (*e, Engine::kModStep);
    CHECK (!e->modWorking (0) && e->param (hp) == 0.3, "another effect: paused, %f", e->param (hp));
    loadFx (*e, 1, kFxPara);
    e->setParam (hp, 0.3);
    run (*e, Engine::kModStep);
    CHECK (e->modWorking (0) && std::fabs (e->param (hp) - 0.5) < 1e-9, "para back: resumed, %f", e->param (hp));
    // a mapping onto a slot's menu does nothing (the effect never sees it change)
    const uint32_t movement = slotBlockParam (1, (uint32_t)fxBlockOf (kFxPara, para::kMovement));
    const double before = e->param (movement);
    ModMapping menu {2, movement, 1.0, kFxPara};
    e->setModMappings (&menu, 1);
    run (*e, 4800);
    CHECK (!e->modWorking (0) && e->param (movement) == before, "a menu: %f", e->param (movement));
}

TEST (mod_without_mappings_changes_nothing)
{
    // with no mapping the LFOs' settings (and the LFOs running) change nothing: what an old project plays
    auto s = brightSaw (300, 1.0);
    auto render = [&] (bool touch) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kFilterOn, 1.0);
        if (touch)
            for (int l = 0; l < kModLfos; ++l)
            {
                e->setParam (modLfoParam (l, kModShape), kModSmoothRandom);
                e->setParam (modLfoParam (l, kModRate), 13.0);
                e->setParam (modLfoParam (l, kModRetrig), 1.0);
            }
        e->noteOn (60, 1.0f);
        Out a = run (*e, 6000, {}, 333);
        // mappings that were there and are gone leave nothing behind either
        if (touch)
        {
            ModMapping m {0, kFilterFreq, 0.8, -1};
            e->setModMappings (&m, 1);
            e->setModMappings (nullptr, 0);
        }
        Out b = run (*e, 6000, {}, 333);
        a.l.insert (a.l.end (), b.l.begin (), b.l.end ());
        return a.l;
    };
    CHECK (render (false) == render (true), "the LFOs change the sound with nothing mapped");
}

// ---------------------------------------------------------------------------
// the loop's Sync (a pass lasts the Grid Size at the host's tempo whatever the pitch) and Beat (the loop
// starts again on the host's beats)

// A long sine with the sampler's extras out of the way (no filter, no effects), looping from Start.
static Engine* loopEngine (double secs = 10.0, double fileSr = 44100.0)
{
    Engine* e = makeEngine (sine (220.0, secs, fileSr));
    e->setParam (slotParam (0, kSlotType), kFxEmpty);
    e->setParam (kFilterOn, 0);
    e->setParam (kLoopOn, 1);
    e->setParam (kStart, 0.0);
    e->setParam (kLength, 0.05);
    return e;
}

// Renders one sample at a time, the host's song position moving on, and gives where the main playhead
// (or playhead `head`) jumped back: the output samples at which its position fell. `each` runs before
// each sample (bends, tempo changes).
static std::vector<long> loopRestarts (Engine& e, long frames, HostInfo& host, int head = 0,
                                       const std::function<void (long)>& each = {}, double* furthest = nullptr)
{
    std::vector<long> at;
    float l, r;
    double last = -1.0;
    const double len = e.sample ()->length;
    for (long i = 0; i < frames; ++i)
    {
        if (each)
            each (i);
        e.render (&l, &r, 1, host);
        host.ppq += host.bpm / 60.0 / kHostSr;
        float pos[16];
        const int n = e.playPositions (pos, 16);
        if (n <= head)
            break;
        const double p = (double)pos[head] * len;
        if (last >= 0.0 && p < last - 1.0)
            at.push_back (i);
        if (furthest)
            *furthest = std::max (*furthest, p);
        last = p;
    }
    return at;
}

static bool periodsNear (const std::vector<long>& at, size_t from, double period, double tol, double* worst = nullptr)
{
    double w = 0.0;
    for (size_t k = from + 1; k < at.size (); ++k)
        w = std::max (w, std::fabs ((double)(at[k] - at[k - 1]) - period));
    if (worst)
        *worst = w;
    return at.size () >= from + 2 && w <= tol;
}

TEST (loop_sync_pass_lasts_the_grid)
{
    // 120 BPM, 1 Bar: 2 s a pass (96000 samples at 48 kHz) at the root, the loop 2 s of the 44.1 kHz file
    const double pass = 2.0 * kHostSr;
    CHECK (paramTable ().info (kLoopSync).def == 0.0 && paramTable ().info (kLoopBeat).def == 0.0, "Sync and Beat are off by default");
    for (int note : {60, 72, 53})
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLoopSync, 1);
        e->setParam (kGridSize, 4); // 1 Bar
        e->noteOn (note, 1.0f);
        HostInfo host;
        double furthest = 0.0;
        const auto at = loopRestarts (*e, (long)(pass * 3.5), host, 0, {}, &furthest);
        double worst = 0.0;
        CHECK (at.size () == 3 && std::fabs ((double)at[0] - pass) <= 1.0 && periodsNear (at, 0, pass, 1.0, &worst),
               "note %d: a pass every %.0f samples (%zu passes, the first at %ld, off by %.1f)", note, pass, at.size (),
               at.empty () ? -1L : at[0], worst);
        // the loop reads 2 s of the file at the root, twice that an octave up, less a fifth down
        const double frames = 2.0 * 44100.0 * std::exp2 ((note - 60) / 12.0);
        CHECK (std::fabs (furthest - frames) < 2.0 * 44100.0 / kHostSr * std::exp2 ((note - 60) / 12.0) + 1.0,
               "note %d: the loop is %.0f frames long (%.0f expected)", note, furthest, frames);
    }
    // the root-note length the editor shows (Transpose and Detune count)
    {
        auto s = sine (220.0, 10.0, 44100.0);
        ParamArray p = defaultParams ();
        p[kLoopSync] = 1;
        p[kGridSize] = 4;
        CHECK (std::fabs (loopSyncFrames (*s, p, 120.0) - 88200.0) < 1e-6, "1 Bar at 120 BPM: %.1f frames", loopSyncFrames (*s, p, 120.0));
        p[kTranspose] = 12;
        CHECK (std::fabs (loopSyncFrames (*s, p, 120.0) - 176400.0) < 1e-6, "an octave up: twice the frames");
        p[kTranspose] = 0;
        p[kGridSize] = 2; // 1/4
        CHECK (std::fabs (loopSyncFrames (*s, p, 60.0) - 44100.0) < 1e-6, "1/4 at 60 BPM: one second");
        p[kLoopSync] = 0;
        CHECK (loopSyncFrames (*s, p, 120.0) == 0.0, "Sync off: nothing");
    }
}

TEST (loop_sync_holds_through_bends_and_follows_tempo)
{
    const double pass = 2.0 * kHostSr;
    // a bend up in the middle of the second pass, and down again in the fourth: every pass still 2 s
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLoopSync, 1);
        e->setParam (kGridSize, 4);
        e->setParam (kPbRange, 7);
        e->noteOn (60, 1.0f);
        HostInfo host;
        const auto at = loopRestarts (*e, (long)(pass * 5.5), host, 0, [&] (long i) {
            if (i == (long)(pass * 1.4))
                e->setPitchBend (1.0f);
            if (i == (long)(pass * 3.7))
                e->setPitchBend (-0.6f);
        });
        double worst = 0.0;
        CHECK (at.size () == 5 && periodsNear (at, 0, pass, 1.0, &worst), "bent: %zu passes, off by %.1f samples", at.size (), worst);
    }
    // the pitch envelope moves the speed all the time: still 2 s
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLoopSync, 1);
        e->setParam (kGridSize, 4);
        e->setParam (kPitchEnvAmt, 12.0);
        e->setParam (kPitchD, 3000.0);
        e->noteOn (60, 1.0f);
        HostInfo host;
        const auto at = loopRestarts (*e, (long)(pass * 3.5), host);
        double worst = 0.0;
        CHECK (at.size () == 3 && periodsNear (at, 0, pass, 1.0, &worst), "pitch envelope: %zu passes, off by %.1f", at.size (), worst);
    }
    // the tempo changes to 140 BPM in the second pass: from that pass on, a bar at 140
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLoopSync, 1);
        e->setParam (kGridSize, 4);
        e->noteOn (60, 1.0f);
        HostInfo host;
        const auto at = loopRestarts (*e, (long)(pass * 4.0), host, 0, [&] (long i) {
            if (i == (long)(pass * 1.5))
                host.bpm = 140.0;
        });
        const double fast = 4.0 * 60.0 / 140.0 * kHostSr;
        double worst = 0.0;
        CHECK (at.size () >= 4 && std::fabs ((double)at[0] - pass) <= 1.0 && std::fabs ((double)(at[1] - at[0]) - fast) <= 1.0 &&
                   periodsNear (at, 1, fast, 1.0, &worst),
               "140 BPM: %zu passes, the second %ld samples (%.0f), off by %.1f", at.size (), at.size () > 1 ? at[1] - at[0] : -1L, fast, worst);
        // a host that stops giving its tempo: the last one stays
        host.tempoValid = false;
        host.bpm = 120.0;
        const auto later = loopRestarts (*e, (long)(fast * 2.5), host);
        CHECK (periodsNear (later, 0, fast, 1.0, &worst), "no tempo: the last one (off by %.1f)", worst);
    }
    // Grid Size 1/4 at 120 BPM: half a second
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLoopSync, 1);
        e->setParam (kGridSize, 2);
        e->noteOn (67, 1.0f);
        HostInfo host;
        const auto at = loopRestarts (*e, (long)(kHostSr * 2.2), host);
        double worst = 0.0;
        CHECK (at.size () == 4 && periodsNear (at, 0, 0.5 * kHostSr, 1.0, &worst), "1/4: %zu passes, off by %.1f", at.size (), worst);
    }
}

TEST (loop_sync_end_flag_fade_and_playheads)
{
    const double pass = 2.0 * kHostSr;
    // the end flag at 1.5 s of the file: the loop stops there (a shorter pass: 1.5 s of the file at the root)
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLoopSync, 1);
        e->setParam (kGridSize, 4);
        e->setParam (kSampleEnd, 0.15);
        e->noteOn (60, 1.0f);
        HostInfo host;
        double furthest = 0.0;
        const auto at = loopRestarts (*e, (long)(pass * 2.5), host, 0, {}, &furthest);
        const double capped = 1.5 * kHostSr;
        double worst = 0.0;
        CHECK (periodsNear (at, 0, capped, 1.0, &worst) && furthest <= 1.5 * 44100.0 + 1.0,
               "capped at the end flag: %zu passes, off by %.1f, read up to %.0f", at.size (), worst, furthest);
    }
    // Loop Fade: the loop is the crossfade longer, a pass still 2 s (after the first, which fades in)
    for (double fade : {0.25, 0.8})
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLoopSync, 1);
        e->setParam (kGridSize, 4);
        e->setParam (kLoopFade, fade);
        e->noteOn (65, 1.0f);
        HostInfo host;
        const auto at = loopRestarts (*e, (long)(pass * 4.8), host);
        double worst = 0.0;
        CHECK (at.size () >= 3 && periodsNear (at, 0, pass, 1.0, &worst), "Fade %.2f: %zu passes, off by %.1f", fade, at.size (), worst);
    }
    // an extra playhead loops its own region a pass in the same time
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLoopSync, 1);
        e->setParam (kGridSize, 3); // 1/2: 1 s
        e->setParam (kPlayheads, 1);
        e->setParam (headParam (1, kHeadStart), 0.5);
        e->setParam (headParam (1, kHeadLength), 0.02);
        e->noteOn (64, 1.0f);
        HostInfo host;
        const auto at = loopRestarts (*e, (long)(kHostSr * 3.5), host, 1);
        double worst = 0.0;
        CHECK (at.size () == 3 && periodsNear (at, 0, kHostSr, 1.0, &worst), "the second playhead: %zu passes, off by %.1f", at.size (), worst);
    }
    // Fit squeezes the locked envelope into a pass of Sync's time
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLoopSync, 1);
        e->setParam (kGridSize, 2); // 0.5 s
        e->setParam (kPitchLoopLock, kLoopLockFit);
        e->setParam (kPitchEnvAmt, 12.0);
        e->setParam (kPitchD, 20000.0);
        e->setParam (kPitchS, 0.0);
        e->noteOn (60, 1.0f);
        HostInfo host;
        const auto at = loopRestarts (*e, (long)(kHostSr * 2.2), host);
        double worst = 0.0;
        CHECK (at.size () == 4 && periodsNear (at, 0, 0.5 * kHostSr, 1.0, &worst), "Fit: %zu passes, off by %.1f", at.size (), worst);
    }
}

TEST (loop_sync_and_beat_off_change_nothing)
{
    // Sync and Beat off, the sampler is as it was (bit for bit: compared with the build before them too);
    // Sync with Loop off, and Beat with the host stopped, change nothing either
    auto render = [] (int mode, double loopOn, double sync, double beat, int grid, bool playing) {
        std::unique_ptr<Engine> e (makeEngine (sine (220.0, 1.0, 44100.0, true)));
        e->setParam (kLoopOn, loopOn);
        e->setParam (kLength, 0.3);
        e->setParam (kLoopFade, 0.2);
        e->setParam (kPlayheads, mode == 1 ? 2 : 0);
        e->setParam (kWarp, mode == 2 ? 1 : 0);
        e->setParam (kWarpMode, kWarpRePitch);
        e->setParam (kGridSize, grid);
        if (sync >= 0.0)
            e->setParam (kLoopSync, sync);
        if (beat >= 0.0)
            e->setParam (kLoopBeat, beat);
        e->noteOn (60, 1.0f);
        e->noteOn (67, 0.7f);
        HostInfo host;
        host.playing = playing;
        host.ppqValid = true;
        host.ppq = 0.3;
        Out o = run (*e, 60000, host, 333);
        o.l.insert (o.l.end (), o.r.begin (), o.r.end ());
        return o.l;
    };
    for (int mode = 0; mode < 3; ++mode)
    {
        const auto ref = render (mode, 1, -1, -1, 2, true);
        CHECK (ref == render (mode, 1, 0, 0, 4, true), "mode %d: Sync and Beat off (and the Grid Size) change the sound", mode);
        CHECK (ref != render (mode, 1, 1, 0, 2, true), "mode %d: Sync on changes it", mode);
        CHECK (render (mode, 1, -1, -1, 2, false) == render (mode, 1, 0, 1, 2, false), "mode %d: Beat with the host stopped changes the sound", mode);
        CHECK (render (mode, 0, -1, -1, 2, true) == render (mode, 0, 1, 1, 2, true), "mode %d: Sync and Beat with Loop off change the sound", mode);
    }
}

TEST (loop_beat_restarts_on_the_beat)
{
    // 120 BPM: a beat every 24000 samples. The note starts half way through a beat and at once; its
    // loop (far longer than a beat) starts again on the next beat and every one after
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLength, 0.9);
        e->setParam (kLoopBeat, 1);
        e->noteOn (60, 1.0f);
        HostInfo host;
        host.playing = host.ppqValid = true;
        host.ppq = 4.5;
        const auto at = loopRestarts (*e, 85000, host);
        bool onBeat = at.size () == 4;
        for (size_t k = 0; k < at.size (); ++k)
            onBeat = onBeat && std::labs (at[k] - (long)(12000 + 24000 * k)) <= 1;
        CHECK (onBeat, "%zu restarts, the first at %ld (12000)", at.size (), at.empty () ? -1L : at[0]);
    }
    // sample-accurate inside a block: rendered in blocks of 256, where the playhead is says when it restarted
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLength, 0.9);
        e->setParam (kLoopBeat, 1);
        e->noteOn (60, 1.0f);
        HostInfo host;
        host.playing = host.ppqValid = true;
        host.ppq = 0.37;
        run (*e, 20000, host, 256); // a beat at (1 - 0.37) * 24000 = 15120
        float pos[4];
        e->playPositions (pos, 4);
        const double since = (double)pos[0] * e->sample ()->length / (44100.0 / kHostSr);
        CHECK (std::fabs (20000.0 - since - 15120.0) <= 1.0, "restarted at %.1f (15120)", 20000.0 - since);
    }
    // a 1-bar Sync loop restarts on the bar lines (the first a bar line away from the note, then every bar)
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLoopSync, 1);
        e->setParam (kGridSize, 4);
        e->setParam (kLoopBeat, 1);
        e->noteOn (60, 1.0f);
        HostInfo host;
        host.playing = host.ppqValid = true;
        host.ppq = 1.5; // bar lines at 4 and 8: 60000 and 156000 samples on
        const auto at = loopRestarts (*e, 260000, host);
        CHECK (at.size () == 3 && std::labs (at[0] - 60000) <= 1 && std::labs (at[1] - 156000) <= 1 && std::labs (at[2] - 252000) <= 1,
               "%zu restarts, at %ld, %ld (60000, 156000, 252000)", at.size (), at.empty () ? -1L : at[0], at.size () > 1 ? at[1] : -1L);
        // the bar's start from the host, a 1/2 grid: on the half bars from it
        std::unique_ptr<Engine> h (loopEngine ());
        h->setParam (kLoopSync, 1);
        h->setParam (kGridSize, 3);
        h->setParam (kLoopBeat, 1);
        h->noteOn (60, 1.0f);
        HostInfo hb;
        hb.playing = hb.ppqValid = hb.barValid = true;
        hb.ppq = 3.25;
        hb.barPpq = 3.0; // (a bar from 3: half bars at 5, 7)
        const auto half = loopRestarts (*h, 100000, hb);
        CHECK (half.size () == 2 && std::labs (half[0] - 42000) <= 1 && std::labs (half[1] - 90000) <= 1,
               "1/2: %zu restarts, at %ld (42000, 90000)", half.size (), half.empty () ? -1L : half[0]);
    }
    // a loop shorter than a beat loops within it and starts again on each beat
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLength, 0.01); // 0.1 s of the file
        e->setParam (kLoopBeat, 1);
        e->noteOn (60, 1.0f);
        HostInfo host;
        host.playing = host.ppqValid = true;
        host.ppq = 0.5;
        int onBeat = 0;
        const auto at = loopRestarts (*e, 60000, host);
        for (long x : at)
            onBeat += std::labs (x - 12000) <= 1 || std::labs (x - 36000) <= 1 ? 1 : 0;
        CHECK (onBeat == 2 && at.size () > 4, "short loop: %zu restarts, %d on the beats", at.size (), onBeat);
    }
    // the host stopped: no restarts
    {
        std::unique_ptr<Engine> e (loopEngine ());
        e->setParam (kLength, 0.9);
        e->setParam (kLoopBeat, 1);
        e->noteOn (60, 1.0f);
        HostInfo host;
        host.ppqValid = true;
        host.ppq = 0.5;
        CHECK (loopRestarts (*e, 60000, host).empty (), "host stopped: no restart");
    }
}

TEST (performance)
{
    auto s = sine (220.0, 4.0, 44100.0, true);
    // CPU time (other programs running do not count), the best of five renders (one shared CI machine slowed a single render to 2.5 times its usual time)
    auto timeOnce = [&] (int voices, int warpMode, bool warp) {
        std::unique_ptr<Engine> e (makeEngine (s));
        e->setParam (kVoices, 14); // 32
        e->setParam (kFilterFreq, 3000.0);
        e->setParam (kFilterSlope, 1);
        e->setParam (kLoopOn, 1);
        e->setParam (kWarp, warp ? 1 : 0);
        e->setParam (kWarpMode, warpMode);
        for (int i = 0; i < voices; ++i)
            e->noteOn (40 + i, 0.8f);
        const pk::testing::CpuClock t0 = pk::testing::cpuClock ();
        run (*e, (int)kHostSr * 4);
        const double secs = (double)(pk::testing::cpuClock () - t0) / pk::testing::kCpuClocksPerSec;
        return 100.0 * secs / 4.0; // % of one core in real time
    };
    auto timeIt = [&] (int voices, int warpMode, bool warp) {
        double best = 1e9;
        for (int i = 0; i < 5; ++i)
            best = std::min (best, timeOnce (voices, warpMode, warp));
        return best;
    };
    const double classic = timeIt (32, 0, false);
    const double complex8 = timeIt (8, kWarpComplex, true);
    const double beats16 = timeIt (16, kWarpBeatsMode, true);
    std::printf ("    CPU: 32 classic voices %.1f%%, 8 complex voices %.1f%%, 16 beats voices %.1f%%\n", classic,
                 complex8, beats16);
    CHECK (classic < 25.0, "classic too slow %f%%", classic);
    CHECK (complex8 < 25.0, "complex too slow %f%%", complex8); // (about 9 % here)
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

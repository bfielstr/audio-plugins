// Headless tests for the Ciphr DSP. Run: ./ciphr_tests [filter]
// The wavetables, a voice (Timbre, Cross, the envelopes), the voices together (polyphony and stealing),
// the processor (taps, diffusion, Length, the feedback and its frequency shifter), Variant and Drift,
// the input bus and the CPU budget.
#include "Dsp.h"
#include "Engine.h"
#include "Params.h"
#include "SpaceFx.h"
#include "Variant.h"
#include "Voice.h"
#include "Wavetable.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace ciphr;

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
#define TEST(name)                       \
    static void name ();                 \
    static Reg reg_##name (#name, name); \
    static void name ()

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

double rms (const std::vector<float>& x, size_t a, size_t b)
{
    b = std::min (b, x.size ());
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double)x[i] * x[i];
    return b > a ? std::sqrt (s / (double)(b - a)) : 0.0;
}
double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-20)); }
bool finite (const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}
// the amplitude of the component at `hz` in x[a, b) (a single DFT bin, Hann window)
double toneAt (const std::vector<float>& x, double hz, size_t a, size_t b)
{
    double s = 0, c = 0, wsum = 0;
    for (size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double)(i - a) / (double)(b - a));
        s += w * x[i] * std::sin (2.0 * kPi * hz * (double)i / kSr);
        c += w * x[i] * std::cos (2.0 * kPi * hz * (double)i / kSr);
        wsum += w;
    }
    return 2.0 * std::sqrt (s * s + c * c) / wsum;
}

// power spectrum (|X|^2) of x[0, n) with a Blackman-Harris window, n a power of two
std::vector<double> spectrum (const std::vector<float>& x, size_t n)
{
    std::vector<std::complex<double>> v (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = 2.0 * kPi * (double)i / (double)(n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        v[i] = std::complex<double> (w * (i < x.size () ? x[i] : 0.0f), 0.0);
    }
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (v[i], v[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double a = -2.0 * kPi / (double)len;
        const std::complex<double> wl (std::cos (a), std::sin (a));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = v[i + k], t = v[i + k + len / 2] * w;
                v[i + k] = u + t;
                v[i + k + len / 2] = u - t;
                w *= wl;
            }
        }
    }
    std::vector<double> p (n / 2);
    for (size_t k = 0; k < n / 2; ++k)
        p[k] = std::norm (v[k]);
    return p;
}

std::unique_ptr<Engine> engine (double sr = kSr, int block = 512)
{
    auto e = std::make_unique<Engine> ();
    e->prepare (sr, block);
    return e;
}

// renders `seconds` (no input), calling `at (sample)` before each block
std::vector<float> render (Engine& e, double seconds, std::vector<float>* right = nullptr, int block = 256,
                           const std::function<void (size_t)>& at = {}, const std::vector<float>* in = nullptr)
{
    const size_t n = (size_t)(seconds * kSr);
    std::vector<float> l (n), r (n);
    for (size_t a = 0; a < n; a += (size_t)block)
    {
        if (at)
            at (a);
        const int m = (int)std::min ((size_t)block, n - a);
        const float* x = in ? in->data () + a : nullptr;
        e.process (x, x, l.data () + a, r.data () + a, m);
    }
    if (right)
        *right = r;
    return l;
}

// the voices alone: a dry engine (Blend 0) with every voice setting plain
void dry (Engine& e)
{
    e.setParam (kBlend, 0.0);
    e.setParam (kVelocity, 0.0);
    e.setParam (kEnvAmount, 0.0);
    e.setParam (kCutoff, 20000.0);
    e.setParam (kResonance, 0.0);
    e.setParam (kAttack, 1.0);
    e.setParam (kSustain, 1.0);
    e.reset ();
}

// a voice rendered straight from a VoiceBlock (no engine)
std::vector<float> voiceRender (const Patch& p, int note, VoiceBlock b, double seconds)
{
    Voice v;
    v.prepare (kSr);
    v.setEnvelopes (1.0, 100.0, 1.0, 100.0, 1.0, 100.0, 1.0, 100.0);
    v.start (note, 1.0f, p, true, 1);
    const size_t n = (size_t)(seconds * kSr);
    std::vector<float> out (n, 0.0f);
    for (size_t a = 0; a < n; a += 32)
        v.render (out.data () + a, (int)std::min<size_t> (32, n - a), b, p);
    return out;
}

VoiceBlock plainBlock (double pos)
{
    VoiceBlock b;
    b.sr = kSr;
    for (int k = 0; k < kOscs; ++k)
        b.oscPos[k] = pos;
    b.cutoff = 20000.0;
    b.resonance = 0.0;
    b.keyTrack = 0.0;
    b.envAmount = 0.0;
    b.velocity = 0.0f;
    return b;
}

} // namespace

// ---- wavetables ---------------------------------------------------------------------------

TEST (wavetable_band_limited)
{
    // one oscillator read the way a voice reads it, at notes high enough that a full table would alias
    // badly: every component off the harmonics must be far down (below -80 dB of the total)
    struct Case
    {
        int wave;
        double hz;
    };
    const Case cases[] = {{kWaveSaw, 3137.0}, {kWaveSquare, 5011.0}, {kWaveBuzz, 1234.5}, {kWavePulse, 7777.0}, {kWaveVowelA, 2222.2}};
    const size_t n = 65536;
    for (const Case& c : cases)
    {
        auto alias = [&] (bool bandLimited) {
            const double inc = c.hz / kSr;
            const int level = bandLimited ? WaveBank::levelFor (inc) : 0;
            const float* t = WaveBank::get ().table (c.wave, level);
            std::vector<float> x (n);
            double ph = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                x[i] = WaveBank::read (t, ph);
                ph += inc;
                ph -= std::floor (ph);
            }
            const auto p = spectrum (x, n);
            double total = 0.0, off = 0.0;
            for (size_t k = 1; k < p.size (); ++k)
            {
                const double hz = (double)k * kSr / (double)n;
                const double h = hz / c.hz;
                const bool harmonic = std::fabs (h - std::round (h)) * c.hz < 12.0 * kSr / (double)n && std::round (h) >= 1.0;
                total += p[k];
                if (!harmonic)
                    off += p[k];
            }
            return 10.0 * std::log10 (std::max (off, 1e-30) / total);
        };
        const double limited = alias (true), full = alias (false);
        std::printf ("    %s at %.0f Hz: off-harmonic energy %.1f dB (a full table: %.1f dB)\n", waveName (c.wave), c.hz, limited, full);
        CHECK (limited < -80.0, "%s band-limited: %.1f dB", waveName (c.wave), limited);
        CHECK (full > limited + 20.0, "the full table aliases (the test can tell): %.1f dB", full);
    }
    // the levels: a step that would put a table's top harmonic above Nyquist never picks it
    for (double inc = 1e-4; inc < 0.5; inc *= 1.07)
    {
        const int level = WaveBank::levelFor (inc);
        CHECK (level >= 0 && WaveBank::harmonicsAt (level) * inc <= 0.5 + 1e-12, "level %d for %.5f", level, inc);
        CHECK (level == 0 || WaveBank::harmonicsAt (level - 1) * inc > 0.5, "the fullest level that fits (%d at %.5f)", level, inc);
    }
    CHECK (WaveBank::levelFor (0.6) == -1, "above Nyquist: silent");
}

TEST (wavetable_shapes)
{
    // the full tables peak at 1; a saw's harmonics fall as 1/h; the sine is a sine
    const auto& bank = WaveBank::get ();
    for (int w = 0; w < kNumWaves; ++w)
    {
        const float* t = bank.table (w, 0);
        float peak = 0.0f;
        for (int i = 0; i < kTableSize; ++i)
            peak = std::max (peak, std::fabs (t[i]));
        CHECK (std::fabs (peak - 1.0f) < 1e-4f, "%s peaks at %.5f", waveName (w), peak);
        CHECK (t[kTableSize] == t[0] && t[kTableSize + 1] == t[1], "%s guards", waveName (w));
    }
    const float* s = bank.table (kWaveSine, 0);
    double err = 0.0;
    for (int i = 0; i < kTableSize; ++i)
        err = std::max (err, std::fabs (s[i] - std::sin (2.0 * kPi * i / kTableSize)));
    CHECK (err < 1e-6, "sine error %.2e", err);
}

// ---- a voice ------------------------------------------------------------------------------

TEST (timbre_crossfade)
{
    // halfway between two entries the voice is exactly the mean of the two (Cross centred, so linear)
    const Patch p = makePatch (3);
    const auto y0 = voiceRender (p, 57, plainBlock (1.0), 0.5);
    const auto y1 = voiceRender (p, 57, plainBlock (2.0), 0.5);
    const auto yh = voiceRender (p, 57, plainBlock (1.5), 0.5);
    double err = 0.0, level = 0.0;
    for (size_t i = 0; i < yh.size (); ++i)
    {
        err = std::max (err, (double)std::fabs (yh[i] - 0.5f * (y0[i] + y1[i])));
        level = std::max (level, (double)std::fabs (yh[i]));
    }
    CHECK (err < 1e-4 && level > 0.05, "the midpoint is the mean (error %.2e, level %.2f)", err, level);

    // sweeping Timbre end to end while a note holds: no step bigger than the steadiest held note makes
    auto steps = [] (bool sweep) {
        auto e = engine ();
        dry (*e);
        e->setParam (kTimbre, 0.0);
        e->reset ();
        e->noteOn (45, 1.0f);
        double biggest = 0.0;
        float lastY = 0.0f;
        bool first = true;
        auto y = render (*e, 3.0, nullptr, 64, [&] (size_t a) {
            if (sweep)
                e->setParam (kTimbre, std::min (1.0, (double)a / (2.5 * kSr)));
        });
        for (size_t i = 4800; i < y.size (); ++i)
        {
            if (!first)
                biggest = std::max (biggest, (double)std::fabs (y[i] - lastY));
            lastY = y[i];
            first = false;
        }
        return std::make_pair (biggest, y);
    };
    const auto held = steps (false), swept = steps (true);
    // the held note at Timbre 1 for comparison (its waves may be brighter: steeper steps)
    double end = 0.0;
    {
        auto e = engine ();
        dry (*e);
        e->setParam (kTimbre, 1.0);
        e->reset ();
        e->noteOn (45, 1.0f);
        auto y = render (*e, 1.0);
        for (size_t i = 4801; i < y.size (); ++i)
            end = std::max (end, (double)std::fabs (y[i] - y[i - 1]));
    }
    std::printf ("    biggest step: held %.4f, at the end %.4f, swept %.4f\n", held.first, end, swept.first);
    CHECK (swept.first <= 1.25 * std::max (held.first, end), "no clicks while Timbre sweeps (%.4f against %.4f)", swept.first,
           std::max (held.first, end));
    CHECK (finite (swept.second), "finite");
}

TEST (cross_centre_is_clean)
{
    // Cross at the centre: bit-identical to Cross taken out altogether
    const Patch p = makePatch (11);
    VoiceBlock b = plainBlock (0.7);
    b.crossFrom = b.crossTo = 0.0f;
    const auto centre = voiceRender (p, 60, b, 0.5);
    Voice::crossBypass = true;
    const auto off = voiceRender (p, 60, b, 0.5);
    Voice::crossBypass = false;
    CHECK (std::memcmp (centre.data (), off.data (), centre.size () * sizeof (float)) == 0, "Cross centred is Cross off, bit for bit");
    // and the engine's Cross lands exactly on the centre after a move
    auto e = engine ();
    dry (*e);
    e->noteOn (60, 1.0f);
    e->setParam (kCross, 0.8);
    render (*e, 0.3);
    e->setParam (kCross, 0.0);
    render (*e, 0.5);
    VoiceBlock tiny = b;
    tiny.crossFrom = tiny.crossTo = 1e-3f;
    const auto nudged = voiceRender (p, 60, tiny, 0.5);
    CHECK (std::memcmp (centre.data (), nudged.data (), centre.size () * sizeof (float)) != 0, "the smallest Cross changes the sound");
}

TEST (cross_sides_change_spectrum)
{
    // FM (left) spreads energy upwards; ring (right) moves it to sum and difference frequencies
    const Patch p = makePatch (2);
    auto spec = [&] (float cross) {
        VoiceBlock b = plainBlock (0.0);
        b.crossFrom = b.crossTo = cross;
        const auto y = voiceRender (p, 48, b, 1.5);
        std::vector<float> tail (y.begin () + 4800, y.end ());
        return spectrum (tail, 65536);
    };
    const auto clean = spec (0.0f), fm = spec (-0.8f), ring = spec (0.8f);
    auto above = [] (const std::vector<double>& s, double hz) {
        double hi = 0.0, all = 0.0;
        for (size_t k = 1; k < s.size (); ++k)
        {
            all += s[k];
            if ((double)k * kSr / 65536.0 > hz)
                hi += s[k];
        }
        return hi / all;
    };
    auto distance = [] (const std::vector<double>& a, const std::vector<double>& b) {
        double sa = 0, sb = 0;
        for (size_t k = 1; k < a.size (); ++k)
        {
            sa += a[k];
            sb += b[k];
        }
        double d = 0.0;
        for (size_t k = 1; k < a.size (); ++k)
            d += std::fabs (a[k] / sa - b[k] / sb);
        return d; // 0 .. 2
    };
    const double hiClean = above (clean, 3000.0), hiFm = above (fm, 3000.0);
    std::printf ("    energy above 3 kHz: clean %.4f, FM %.4f; spectral distance FM %.2f, ring %.2f\n", hiClean, hiFm,
                 distance (clean, fm), distance (clean, ring));
    CHECK (hiFm > 1.5 * hiClean, "FM brightens (%.4f against %.4f)", hiFm, hiClean);
    CHECK (distance (clean, fm) > 0.3, "FM changes the spectrum (%.2f)", distance (clean, fm));
    CHECK (distance (clean, ring) > 0.3, "ring changes the spectrum (%.2f)", distance (clean, ring));
}

TEST (envelope)
{
    // attack 10 ms, decay 100 ms to 50 %, release 200 ms
    auto e = engine ();
    dry (*e);
    e->setParam (kAttack, 10.0);
    e->setParam (kDecay, 100.0);
    e->setParam (kSustain, 0.5);
    e->setParam (kRelease, 200.0);
    e->reset ();
    e->noteOn (60, 1.0f);
    render (*e, 0.005, nullptr, 48);
    const Voice& v = e->voice (0);
    CHECK (v.stage () == dsp::Envelope::kAttack && v.level () > 0.4f && v.level () < 0.6f, "half way up after 5 ms: %.2f", v.level ());
    render (*e, 0.2, nullptr, 48);
    CHECK (std::fabs (v.level () - 0.5f) < 0.01f, "at the sustain after the decay: %.3f", v.level ());
    e->noteOff (60);
    render (*e, 0.2, nullptr, 48);
    CHECK (v.level () < 0.01f && v.level () > 0.0f, "nearly silent at the release time: %.4f", v.level ());
    render (*e, 0.3, nullptr, 48);
    CHECK (!v.active () && e->activeVoices () == 0, "then silent");
}

TEST (polyphony_and_stealing)
{
    auto e = engine ();
    dry (*e);
    e->setParam (kRelease, 100.0);
    e->reset ();
    for (int i = 0; i < kVoices; ++i)
    {
        e->noteOn (48 + i, 0.8f);
        render (*e, 0.01);
    }
    CHECK (e->activeVoices () == kVoices, "8 notes, 8 voices: %d", e->activeVoices ());
    // the same note again: retriggered, no new voice
    e->noteOn (50, 1.0f);
    render (*e, 0.01);
    int fifty = 0;
    for (int i = 0; i < kVoices; ++i)
        fifty += e->voice (i).gated () && e->voice (i).note () == 50 ? 1 : 0;
    CHECK (fifty == 1, "a repeated note keeps one voice (%d)", fifty);
    // a ninth note steals the oldest held one (48)
    e->noteOn (72, 0.8f);
    render (*e, 0.01);
    bool has48 = false, has72 = false;
    for (int i = 0; i < kVoices; ++i)
    {
        has48 = has48 || (e->voice (i).gated () && e->voice (i).note () == 48);
        has72 = has72 || (e->voice (i).gated () && e->voice (i).note () == 72);
    }
    CHECK (!has48 && has72 && e->activeVoices () == kVoices, "the ninth note took the oldest voice");
    // a released voice is stolen before a held one
    e->noteOff (55);
    render (*e, 0.02);
    e->noteOn (80, 0.8f);
    render (*e, 0.01);
    int held = 0;
    for (int i = 0; i < kVoices; ++i)
        held += e->voice (i).gated () ? 1 : 0;
    bool has49 = false;
    for (int i = 0; i < kVoices; ++i)
        has49 = has49 || (e->voice (i).gated () && e->voice (i).note () == 49);
    CHECK (held == kVoices && has49, "the released voice went first (%d held, 49 still there: %d)", held, has49);
    // every note off: silent after the release
    for (int nn = 0; nn < 128; ++nn)
        e->noteOff (nn);
    auto y = render (*e, 1.0);
    CHECK (e->activeVoices () == 0, "all released: %d voices left", e->activeVoices ());
    CHECK (rms (y, y.size () - 4800, y.size ()) < 1e-6, "and silent");
    CHECK (finite (y), "finite");
}

TEST (note_pitch)
{
    // a sine-only patch is impossible to pick by Variant, so check the root oscillator's pitch through the
    // engine's own tuning: A4 (69) with Tune +12 puts the root's first entry (a saw at the note) at 880 Hz
    const Patch p = makePatch (1);
    CHECK (p.osc[0][0].wave == kWaveSaw && p.osc[0][0].semis == 0.0, "the root's first entry is a saw at the note");
    VoiceBlock b = plainBlock (0.0);
    b.tune = 12.0;
    Patch one = p;
    for (int k = 1; k < kOscs; ++k)
        for (int e = 0; e < kEntries; ++e)
            one.osc[k][e] = {kWaveSine, 200.0}; // (far above Nyquist: silent)
    const auto y = voiceRender (one, 69, b, 1.0);
    const double at880 = toneAt (y, 880.0, 4800, y.size ()), at440 = toneAt (y, 440.0, 4800, y.size ());
    CHECK (at880 > 30.0 * at440 && at880 > 0.01, "880 Hz %.4f, 440 Hz %.5f", at880, at440);
}

// ---- the processor ------------------------------------------------------------------------

namespace {
SpaceFx fx (double space, double lengthMs, const Tap* taps, double regen = 0.0, double shift = 0.0, double character = 1.0,
            double movement = 0.0)
{
    SpaceFx f;
    f.prepare (kSr);
    f.setPattern (taps);
    f.setCharacter (character);
    f.setSpace (space);
    f.setLength (lengthMs);
    f.setMovement (movement);
    f.setRegen (regen);
    f.setShift (shift);
    f.reset ();
    return f;
}
std::vector<float> impulse (SpaceFx& f, double seconds, std::vector<float>* right = nullptr)
{
    const size_t n = (size_t)(seconds * kSr);
    std::vector<float> l (n, 0.0f), r (n, 0.0f);
    l[0] = r[0] = 1.0f;
    for (size_t a = 0; a < n; a += 256)
        f.process (l.data () + a, r.data () + a, (int)std::min<size_t> (256, n - a));
    if (right)
        *right = r;
    return l;
}
} // namespace

TEST (processor_discrete_taps)
{
    // Space 0, no Movement, no Regen: an impulse comes back as one echo per tap, at Length x its time
    const Patch p = makePatch (5);
    for (double length : {400.0, 200.0})
    {
        SpaceFx f = fx (0.0, length, p.taps);
        std::vector<float> r;
        const auto l = impulse (f, 0.6, &r);
        std::vector<float> sum (l.size ());
        for (size_t i = 0; i < l.size (); ++i)
            sum[i] = std::fabs (l[i]) + std::fabs (r[i]);
        std::vector<bool> near (sum.size (), false);
        int found = 0;
        for (int t = 0; t < kTaps; ++t)
        {
            const double d = p.taps[t].time * length * 0.001 * kSr;
            CHECK (std::fabs (f.tapDelay (t) - d) < 1e-6, "tap %d at %.2f samples (%.2f expected)", t, f.tapDelay (t), d);
            const size_t at = (size_t)d;
            double energy = 0.0;
            for (size_t i = at - 1; i <= at + 2; ++i)
            {
                energy += sum[i];
                near[i] = true;
            }
            found += energy > 0.05 ? 1 : 0;
        }
        double stray = 0.0;
        for (size_t i = 0; i < sum.size (); ++i)
            if (!near[i])
                stray = std::max (stray, (double)sum[i]);
        CHECK (found == kTaps, "Length %.0f ms: every tap's echo where it belongs (%d of %d)", length, found, kTaps);
        CHECK (stray < 1e-6, "and nothing else (%.2e)", stray);
    }
    // Length scales every time: the longest echo at Length itself
    SpaceFx a = fx (0.0, 300.0, p.taps), b = fx (0.0, 600.0, p.taps);
    for (int t = 0; t < kTaps; ++t)
        CHECK (std::fabs (b.tapDelay (t) - 2.0 * a.tapDelay (t)) < 1e-6, "tap %d twice as late at twice the Length", t);
    CHECK (std::fabs (a.tapDelay (0) - 0.3 * kSr) < 1e-6, "tap 0 is Length itself");
}

TEST (processor_space_diffuses)
{
    // Space 1: the same impulse comes back as a dense wash (many more echoes above -60 dB of the peak)
    const Patch p = makePatch (5);
    auto density = [&] (double space) {
        SpaceFx f = fx (space, 300.0, p.taps);
        const auto y = impulse (f, 1.5);
        float peak = 0.0f;
        for (float v : y)
            peak = std::max (peak, std::fabs (v));
        int count = 0;
        for (float v : y)
            count += std::fabs (v) > 1e-3f * peak ? 1 : 0;
        return count;
    };
    const int taps = density (0.0), wash = density (1.0);
    std::printf ("    samples above -60 dB: Space 0 %d, Space 1 %d\n", taps, wash);
    CHECK (wash > 30 * taps, "Space 1 is diffuse (%d against %d)", wash, taps);
}

TEST (frequency_shifter)
{
    // a 1 kHz sine moved by +100 Hz: 1100 Hz, the other sideband and the original far down
    for (double shift : {100.0, -100.0, 7.0})
    {
        dsp::FreqShifter s;
        s.setSampleRate (kSr);
        s.setShift (shift);
        s.reset ();
        std::vector<float> y ((size_t)kSr);
        for (size_t i = 0; i < y.size (); ++i)
        {
            double un = 0.0;
            y[i] = (float)s.tick (0.5 * std::sin (2.0 * kPi * 1000.0 * (double)i / kSr), un);
        }
        const double want = toneAt (y, 1000.0 + shift, 4800, y.size ()), mirror = toneAt (y, 1000.0 - shift, 4800, y.size ()),
                     orig = toneAt (y, 1000.0, 4800, y.size ());
        std::printf ("    shift %+.0f Hz: wanted %.1f dB, mirror %.1f dB, 1 kHz %.1f dB\n", shift, db (want), db (mirror), db (orig));
        CHECK (std::fabs (db (want) - db (0.5)) < 0.5, "%+.0f Hz: the shifted tone at its level (%.2f dB)", shift, db (want));
        CHECK (db (mirror) < db (want) - 35.0, "%+.0f Hz: single sideband (mirror %.1f dB)", shift, db (mirror));
        CHECK (db (orig) < db (want) - 35.0, "%+.0f Hz: the original gone (%.1f dB)", shift, db (orig));
    }
}

TEST (regen_directions)
{
    // one tap at Length (the feedback's own), a 1 kHz burst, Shift +50 Hz: the second echo (after one
    // trip round the loop) is shifted only with Regen positive, shifted and unshifted alike with Regen
    // negative
    Tap one[kTaps];
    for (int i = 0; i < kTaps; ++i)
        one[i] = {1.0, i == 0 ? 1.0 : 0.0, 0.0, 0.1, 0.0};
    const double length = 400.0;
    const size_t L = (size_t)(length * 0.001 * kSr), burst = (size_t)(0.25 * kSr);
    auto echo2 = [&] (double regen) {
        SpaceFx f = fx (0.0, length, one, regen, 50.0, 0.0);
        const size_t n = 3 * L;
        std::vector<float> l (n, 0.0f), r (n, 0.0f);
        for (size_t i = 0; i < burst; ++i)
            l[i] = r[i] = (float)(0.3 * std::sin (2.0 * kPi * 1000.0 * (double)i / kSr) * std::sin (kPi * (double)i / (double)burst));
        f.process (l.data (), r.data (), (int)n);
        return std::make_pair (toneAt (l, 1000.0, 2 * L, 2 * L + burst), toneAt (l, 1050.0, 2 * L, 2 * L + burst));
    };
    const auto pos = echo2 (0.8), neg = echo2 (-0.8), none = echo2 (0.0);
    std::printf ("    second echo: Regen +80 %% 1000 Hz %.1f dB, 1050 Hz %.1f dB; -80 %% %.1f / %.1f dB; 0 %.1f / %.1f dB\n",
                 db (pos.first), db (pos.second), db (neg.first), db (neg.second), db (none.first), db (none.second));
    CHECK (db (pos.second) > db (pos.first) + 25.0, "Regen +: shifted only");
    CHECK (std::fabs (db (neg.first) - db (neg.second)) < 1.5 && db (neg.first) > db (pos.first) + 20.0, "Regen -: shifted plus unshifted");
    CHECK (db (none.first) < -80.0 && db (none.second) < -80.0, "Regen 0: no second echo");
}

TEST (feedback_stable)
{
    // the loudest input for 20 s and 10 s of silence at the most feedback, each direction, a short and a
    // long loop, with and without a shift, everything else at its most: never NaN, bounded, no runaway,
    // and it dies away once the input stops
    const Patch p = makePatch (9);
    for (double regen : {1.0, -1.0})
        for (double length : {30.0, 2000.0})
            for (double shift : {0.0, 37.0})
            {
                SpaceFx f = fx (1.0, length, p.taps, regen, shift, 1.0, 1.0);
                const size_t n = (size_t)(30.0 * kSr), loud = (size_t)(20.0 * kSr);
                std::vector<float> l (n), r (n);
                uint32_t seed = 7;
                for (size_t i = 0; i < loud; ++i)
                {
                    seed = seed * 1664525u + 1013904223u;
                    l[i] = (float)((int32_t)seed / 2147483648.0);
                    seed = seed * 1664525u + 1013904223u;
                    r[i] = (float)((int32_t)seed / 2147483648.0);
                }
                for (size_t a = 0; a < n; a += 512)
                    f.process (l.data () + a, r.data () + a, (int)std::min<size_t> (512, n - a));
                float peak = 0.0f;
                bool ok = finite (l) && finite (r);
                for (size_t i = 0; i < n; ++i)
                    peak = std::max (peak, std::max (std::fabs (l[i]), std::fabs (r[i])));
                const double during = rms (l, loud - 5 * (size_t)kSr, loud), early = rms (l, 5 * (size_t)kSr, 10 * (size_t)kSr);
                const double after = rms (l, n - (size_t)kSr, n);
                CHECK (ok, "finite (Regen %+.0f, %.0f ms, %.0f Hz)", regen, length, shift);
                CHECK (peak < 4.0f, "bounded: peak %.2f (Regen %+.0f, %.0f ms, %.0f Hz)", peak, regen, length, shift);
                CHECK (during < 1.5 * early + 0.05, "no runaway: %.3f then %.3f (Regen %+.0f, %.0f ms, %.0f Hz)", early, during, regen,
                       length, shift);
                if (length < 100.0)
                    CHECK (db (after) < db (during) - 40.0, "dies away: %.1f dB after, %.1f dB during (Regen %+.0f, %.0f Hz)", db (after),
                           db (during), regen, shift);
            }
}

// ---- Variant and Drift --------------------------------------------------------------------

TEST (variant_deterministic)
{
    auto same = [] (const Patch& a, const Patch& b) {
        for (int k = 0; k < kOscs; ++k)
        {
            if (a.phase0[k] != b.phase0[k])
                return false;
            for (int e = 0; e < kEntries; ++e)
                if (a.osc[k][e].wave != b.osc[k][e].wave || a.osc[k][e].semis != b.osc[k][e].semis)
                    return false;
        }
        for (int i = 0; i < kTaps; ++i)
            if (a.taps[i].time != b.taps[i].time || a.taps[i].gain != b.taps[i].gain || a.taps[i].pan != b.taps[i].pan ||
                a.taps[i].lfoRate != b.taps[i].lfoRate || a.taps[i].lfoPhase != b.taps[i].lfoPhase)
                return false;
        return true;
    };
    int differ = 0;
    for (int v = kMinVariant; v <= kMaxVariant; ++v)
    {
        CHECK (same (makePatch (v), makePatch (v)), "Variant %d twice: the same patch", v);
        differ += same (makePatch (v), makePatch (v == kMaxVariant ? kMinVariant : v + 1)) ? 0 : 1;
    }
    CHECK (differ == kMaxVariant - kMinVariant + 1, "every Variant differs from the next (%d)", differ);
    // a fixed point: Variant 1's patch never changes between builds (its first entries, its longest taps)
    const Patch one = makePatch (1);
    std::printf ("    Variant 1: oscillator 1 [%s %+.17g, %s %+.17g, ...], tap 1 at %.17g\n", waveName (one.osc[1][0].wave), one.osc[1][0].semis,
                 waveName (one.osc[1][1].wave), one.osc[1][1].semis, one.taps[1].time);
    CHECK (one.osc[1][0].wave == kWaveOrgan && std::fabs (one.osc[1][0].semis - 7.0164172630285462) < 1e-12 &&
               one.osc[1][1].wave == kWavePulse && std::fabs (one.taps[1].time - 0.96411666043844912) < 1e-12,
           "Variant 1 is the patch it always was");

    // the same Variant, the same notes: the same sound, bit for bit (Drift on too); another Variant differs
    auto play = [] (int variant, int detour) {
        auto e = engine ();
        e->setParam (kDrift, 0.6);
        e->setParam (kMovement, 0.7);
        if (detour)
        {
            e->setParam (kVariant, detour);
            render (*e, 0.1);
        }
        e->setParam (kVariant, variant);
        e->reset ();
        return render (*e, 2.0, nullptr, 256, [&] (size_t a) {
            if (a == 0)
            {
                e->noteOn (60, 0.9f);
                e->noteOn (67, 0.7f);
            }
            if (a == 256 * 200)
                e->noteOff (60);
        });
    };
    const auto a = play (7, 0), b = play (7, 0), c = play (7, 99), d = play (8, 0);
    CHECK (a == b, "Variant 7 twice: the same output");
    CHECK (a == c, "Variant 7 after a visit to 99: the same output");
    CHECK (a != d && rms (a, 0, a.size ()) > 1e-3, "Variant 8 sounds different");
}

TEST (drift)
{
    // Drift 0: nothing moves, however long it plays; Drift 1: the positions and taps glide
    for (double amount : {0.0, 1.0})
    {
        auto e = engine ();
        e->setParam (kDrift, amount);
        e->setParam (kMovement, 0.0); // (Movement moves the taps by itself)
        e->setParam (kTimbre, 0.4);
        e->reset ();
        e->noteOn (60, 1.0f);
        render (*e, 0.1);
        double pos0[kOscs], tap0[kTaps];
        for (int k = 0; k < kOscs; ++k)
            pos0[k] = e->oscPosition (k);
        for (int i = 0; i < kTaps; ++i)
            tap0[i] = e->processorStage ().tapDelay (i);
        double moved = 0.0, tapMoved = 0.0, step = 0.0, last = e->oscPosition (0);
        for (int s = 0; s < 100; ++s)
        {
            render (*e, 0.1);
            for (int k = 0; k < kOscs; ++k)
                moved = std::max (moved, std::fabs (e->oscPosition (k) - pos0[k]));
            for (int i = 0; i < kTaps; ++i)
                tapMoved = std::max (tapMoved, std::fabs (e->processorStage ().tapDelay (i) - tap0[i]));
            step = std::max (step, std::fabs (e->oscPosition (0) - last));
            last = e->oscPosition (0);
        }
        if (amount == 0.0)
        {
            CHECK (moved == 0.0 && tapMoved == 0.0, "Drift 0 is static (moved %.2e, taps %.2e)", moved, tapMoved);
            CHECK (std::fabs (pos0[0] - 0.4 * (kEntries - 1)) < 1e-9, "at Timbre's place: %.4f", pos0[0]);
        }
        else
        {
            CHECK (moved > 0.2 && tapMoved > 10.0, "Drift 1 glides (moved %.3f, taps %.1f samples)", moved, tapMoved);
            CHECK (step < 0.1, "smoothly (largest step in 0.1 s: %.3f)", step);
        }
    }
}

// ---- the input bus ------------------------------------------------------------------------

TEST (input_bus)
{
    const size_t n = (size_t)(1.0 * kSr);
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = (float)(0.4 * std::sin (2.0 * kPi * 330.0 * (double)i / kSr) + 0.1 * std::sin (2.0 * kPi * 4100.0 * (double)i / kSr));
    // Direct, Input 100 %, Blend 0: the input passes untouched (through the end saturator's delay)
    {
        auto e = engine ();
        e->setParam (kInput, 1.0);
        e->setParam (kBlend, 0.0);
        e->reset ();
        const auto y = render (*e, 1.0, nullptr, 256, {}, &x);
        const size_t lat = (size_t)e->latency ();
        double err = 0.0;
        for (size_t i = 4800; i + lat < n; ++i)
            err = std::max (err, (double)std::fabs (y[i + lat] - x[i]));
        CHECK (err < 1e-4, "Input 100 %%, Blend 0: the input passes (error %.2e, latency %zu)", err, lat);
    }
    // Input 0: none of it
    {
        auto e = engine ();
        e->setParam (kBlend, 0.0);
        e->reset ();
        const auto y = render (*e, 1.0, nullptr, 256, {}, &x);
        CHECK (rms (y, 0, n) == 0.0, "Input 0: silence");
    }
    // Blend 100 %: the input reaches the processor (wet), and Input 50 % is half as loud dry
    {
        auto e = engine ();
        e->setParam (kInput, 0.5);
        e->setParam (kBlend, 0.0);
        e->reset ();
        const auto y = render (*e, 1.0, nullptr, 256, {}, &x);
        const double ratio = rms (y, 9600, n) / rms (x, 9600 - e->latency (), n - e->latency ());
        CHECK (std::fabs (ratio - 0.5) < 0.01, "Input 50 %%: half as loud (%.3f)", ratio);
        auto w = engine ();
        w->setParam (kInput, 1.0);
        w->setParam (kBlend, 1.0);
        w->setParam (kRegen, 0.0);
        w->reset ();
        const auto yw = render (*w, 1.0, nullptr, 256, {}, &x);
        const double wet = rms (yw, n / 2, n) / rms (x, n / 2, n);
        CHECK (wet > 0.1, "Blend 100 %%: the input through the processor (%.3f of it)", wet);
    }
    // Voices: only while a note plays, through its filter
    {
        auto e = engine ();
        e->setParam (kInput, 1.0);
        e->setParam (kInputPath, kPathVoices);
        e->setParam (kBlend, 0.0);
        e->setParam (kCutoff, 400.0);
        e->setParam (kEnvAmount, 0.0);
        e->setParam (kKeyTrack, 0.0);
        e->reset ();
        const auto quiet = render (*e, 0.5, nullptr, 256, {}, &x);
        CHECK (rms (quiet, 0, quiet.size ()) == 0.0, "Voices: silent without a note");
    }
}

// ---- the whole engine ---------------------------------------------------------------------

TEST (defaults_play)
{
    auto e = engine ();
    std::vector<float> r;
    const auto l = render (*e, 3.0, &r, 333, [&] (size_t a) {
        if (a == 0)
            for (int nn : {48, 55, 60, 64})
                e->noteOn (nn, 0.8f);
        if (a >= (size_t)(2.0 * kSr) && a < (size_t)(2.0 * kSr) + 333)
            e->allNotesOff ();
    });
    const double level = db (rms (l, (size_t)(0.5 * kSr), (size_t)(1.5 * kSr)));
    double diff = 0.0;
    for (size_t i = 0; i < l.size (); ++i)
        diff = std::max (diff, (double)std::fabs (l[i] - r[i]));
    std::printf ("    a chord at the defaults: %.1f dBFS\n", level);
    CHECK (finite (l) && finite (r), "finite");
    CHECK (level > -30.0 && level < -3.0, "a sensible level: %.1f dBFS", level);
    CHECK (diff > 0.01, "stereo (the processor's taps are panned): %.3f", diff);
}

TEST (cpu_budget)
{
    // 8 voices held at the heaviest settings (FM, Drift, every tap, full diffusion, the end saturator on)
    // for 10 s: CPU time (other programs running do not count), the best of three renders
    auto make = [] (bool heavy) {
        auto e = engine (kSr, 512);
        if (heavy)
        {
            e->setParam (kCross, -0.6);
            e->setParam (kDrift, 1.0);
            e->setParam (kCharacter, 1.0);
            e->setParam (kSpace, 1.0);
            e->setParam (kMovement, 1.0);
            e->setParam (kRegen, 0.8);
            e->setParam (kShift, 3.0);
            e->setParam (kTimbre, 0.5);
            e->setParam (kSustain, 1.0);
            e->setParam (kTailBase + pk::kTailOn, 1.0);
            e->setParam (kInput, 0.5);
            e->setParam (kInputPath, kPathVoices);
        }
        e->reset ();
        for (int i = 0; i < kVoices; ++i)
            e->noteOn (40 + 5 * i, 0.8f);
        return e;
    };
    const size_t n = (size_t)(10.0 * kSr);
    std::vector<float> in (n);
    uint32_t seed = 3;
    for (auto& v : in)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (float)((int32_t)seed / 2147483648.0) * 0.5f;
    }
    for (bool heavy : {false, true})
    {
        double secs = 1e9;
        std::vector<float> l, r;
        for (int i = 0; i < 3; ++i)
        {
            auto e = make (heavy);
            const std::clock_t t0 = std::clock ();
            l = render (*e, 10.0, &r, 512, {}, &in);
            secs = std::min (secs, (double)(std::clock () - t0) / CLOCKS_PER_SEC);
            CHECK (e->activeVoices () == kVoices, "8 voices held");
        }
        bool ok = finite (l) && finite (r);
        for (size_t i = 0; ok && i < n; ++i)
            ok = std::fabs (l[i]) < 8.0f && std::fabs (r[i]) < 8.0f;
        CHECK (ok, "finite and bounded");
        std::printf ("    CPU: %.2f%% of one core (8 voices x %d oscillators held, %s)\n", 100.0 * secs / 10.0, kOscs,
                     heavy ? "FM, Drift, every tap, Space 100 %, the end saturator on" : "the defaults");
        CHECK (secs / 10.0 < (heavy ? 0.20 : 0.12), "too slow");
    }
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

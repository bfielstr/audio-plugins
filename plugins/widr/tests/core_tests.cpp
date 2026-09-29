// Headless tests for the Widr DSP. Run: ./widr_tests [filter]
#include "Engine.h"
#include "Mix.h"
#include "Params.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace widr;

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

struct Sig
{
    std::vector<float> l, r;
};

static uint32_t gSeed = 12345;
static float white ()
{
    gSeed = gSeed * 1664525u + 1013904223u;
    return (float)((gSeed >> 8) & 0xFFFFFF) / 8388608.0f - 1.0f;
}

// Pink noise (Paul Kellet's filter), mono (L = R) or two independent channels.
static Sig pink (double secs, bool stereo, float level = 0.1f)
{
    Sig s;
    const size_t n = (size_t)(secs * kSr);
    s.l.resize (n);
    s.r.resize (n);
    for (int c = 0; c < (stereo ? 2 : 1); ++c)
    {
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        auto& x = c == 0 ? s.l : s.r;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = white ();
            b0 = 0.99886 * b0 + w * 0.0555179;
            b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522;
            b5 = -0.7616 * b5 - w * 0.0168980;
            x[i] = (float)((b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11 * level * 10.0);
            b6 = w * 0.115926;
        }
    }
    if (!stereo)
        s.r = s.l;
    return s;
}

static Sig run (Engine& e, const Sig& in, int block = 512)
{
    Sig out;
    out.l.resize (in.l.size ());
    out.r.resize (in.r.size ());
    for (size_t pos = 0; pos < in.l.size (); pos += (size_t)block)
    {
        const int n = (int)std::min<size_t> ((size_t)block, in.l.size () - pos);
        e.process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, n);
    }
    return out;
}

static std::unique_ptr<Engine> engine ()
{
    auto e = std::make_unique<Engine> ();
    e->prepare (kSr, 512);
    return e;
}

static std::vector<float> sum (const Sig& s, float sign)
{
    std::vector<float> x (s.l.size ());
    for (size_t i = 0; i < x.size (); ++i)
        x[i] = s.l[i] + sign * s.r[i];
    return x;
}

// Level of a single frequency in [a, b) (fit of sin/cos under a Hann window, so energy at nearby
// frequencies does not leak in).
static double toneDb (const std::vector<float>& x, double f, size_t a, size_t b)
{
    double s = 0, c = 0, wsum = 0;
    for (size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double)(i - a) / (double)(b - a));
        s += w * x[i] * std::sin (2.0 * M_PI * f * (double)i / kSr);
        c += w * x[i] * std::cos (2.0 * M_PI * f * (double)i / kSr);
        wsum += w;
    }
    const double amp = 2.0 * std::sqrt (s * s + c * c) / wsum;
    return 20.0 * std::log10 (std::max (1e-15, amp));
}

// Energy per band (dB) of x[a, b), averaged over Hann frames.
static std::array<double, kBands> bandDb (const std::vector<float>& x, size_t a, size_t b)
{
    constexpr int n = 8192;
    locus::Fft fft (n);
    std::vector<float> frame (n);
    std::vector<locus::Fft::cf> spec (n / 2 + 1);
    std::array<double, kBands> e {};
    int frames = 0;
    for (size_t pos = a; pos + n <= b; pos += n / 2, ++frames)
    {
        for (int i = 0; i < n; ++i)
            frame[(size_t)i] = x[pos + (size_t)i] * (0.5f - 0.5f * (float)std::cos (2.0 * M_PI * i / n));
        fft.forward (frame.data (), spec.data ());
        for (int k = 0; k < kBands; ++k)
        {
            const int b0 = (int)std::ceil (bandLowHz (k) / (kSr / n)), b1 = (int)std::floor (bandHighHz (k) / (kSr / n));
            for (int bin = b0; bin <= b1 && bin <= n / 2; ++bin)
                e[(size_t)k] += std::norm (spec[(size_t)bin]);
        }
    }
    std::array<double, kBands> db {};
    for (int k = 0; k < kBands; ++k)
        db[(size_t)k] = 10.0 * std::log10 (std::max (1e-20, e[(size_t)k] / std::max (1, frames)));
    return db;
}

static double correlation (const Sig& s, size_t a, size_t b)
{
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = a; i < b; ++i)
    {
        lr += (double)s.l[i] * s.r[i];
        ll += (double)s.l[i] * s.l[i];
        rr += (double)s.r[i] * s.r[i];
    }
    return lr / std::sqrt (std::max (1e-30, ll * rr));
}

static double energy (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0;
    for (size_t i = a; i < b; ++i)
        e += (double)x[i] * x[i];
    return e / (double)(b - a);
}

// ---------------------------------------------------------------------------
TEST (params)
{
    const auto& t = paramTable ();
    CHECK (t.size () == kNumParams, "size");
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        const auto& info = t.info (id);
        const double back = t.toPlain (id, t.toNormalized (id, info.def));
        CHECK (std::fabs (back - info.def) < 1e-9 * std::max (1.0, std::fabs (info.max - info.min)), "%s default %f -> %f",
               info.name, info.def, back);
        double parsed = 0.0;
        CHECK (t.fromText (id, t.toText (id, info.def), parsed) || info.disp == pk::Disp::Choice ||
                   info.disp == pk::Disp::OnOff,
               "%s parses its own text", info.name);
    }
    CHECK (t.toText (kWidth, 1.0) == "100 %" && t.toText (kDecay, 1200.0) == "1.20 s", "%s / %s",
           t.toText (kWidth, 1.0).c_str (), t.toText (kDecay, 1200.0).c_str ());
    CHECK (t.info (kCharacter).def == (double)kWide && t.info (kRole).def == (double)kSupport, "Wide, Support");
    CHECK (t.info ((uint32_t)kTailBase + pk::kTailOn).def == 0.0 && t.info ((uint32_t)kTailBase + pk::kTailDrive).def == 0.0, "saturator off, 0 dB");
}

TEST (width_zero_is_bit_exact)
{
    // everything that would act is turned up; Width 0 must still pass the input untouched (delayed
    // by the tail's constant latency)
    auto in = pink (1.0, true, 0.3f);
    auto e = engine ();
    e->setParam (kWidth, 0.0);
    e->setParam (kAir, 6.0);
    e->setParam (kBeyond, 1.0);
    e->setParam (kSpace, 1.0);
    e->setParam (kGuard, 0.0);
    e->reset ();
    auto out = run (*e, in);
    const size_t lat = (size_t)e->latency ();
    CHECK (lat > 0 && lat < 200, "latency %zu", lat);
    size_t diff = 0;
    for (size_t i = lat; i < in.l.size (); ++i)
        diff += (out.l[i] != in.l[i - lat]) + (out.r[i] != in.r[i - lat]);
    CHECK (diff == 0, "%zu samples differ", diff);
}

TEST (widens_a_mono_source)
{
    auto in = pink (3.0, false);
    auto widthCorr = [&] (double w) {
        auto e = engine ();
        e->setParam (kWidth, w);
        e->setParam (kGuard, 0.0);
        e->reset ();
        return correlation (run (*e, in), 48000, in.l.size ());
    };
    const double c1 = widthCorr (1.0), c2 = widthCorr (2.0);
    CHECK (c1 < 0.85, "Width 100 %%: correlation %f", c1);
    CHECK (c2 < c1 - 0.1, "Width 200 %% is wider: %f vs %f", c2, c1);
    for (int c = 0; c < kNumCharacters; ++c)
    {
        auto e = engine ();
        e->setParam (kCharacter, c);
        e->reset ();
        auto out = run (*e, in);
        const double side = energy (sum (out, -1.0f), 48000, in.l.size ()), mid = energy (sum (out, 1.0f), 48000, in.l.size ());
        CHECK (side > mid * 0.05 && side < mid * 1.5, "Character %d: side / mid %f", c, side / mid);
    }
}

TEST (mono_below_is_mono)
{
    // a stereo low tone a quarter of the crossover down, plus stereo noise above; every Character,
    // everything up: L - R at the low tone stays 80 dB under L + R
    for (double xover : {100.0, 150.0, 400.0})
        for (int c = 0; c < kNumCharacters; ++c)
        {
            const double f = xover / 4.0;
            auto in = pink (2.0, true, 0.05f);
            for (size_t i = 0; i < in.l.size (); ++i)
            {
                const double t = (double)i / kSr;
                in.l[i] += (float)(0.5 * std::sin (2.0 * M_PI * f * t));
                in.r[i] += (float)(0.2 * std::sin (2.0 * M_PI * f * t + 1.0));
            }
            auto e = engine ();
            e->setParam (kMonoBelow, xover);
            e->setParam (kCharacter, c);
            e->setParam (kWidth, 2.0);
            e->setParam (kSpace, 1.0);
            e->setParam (kBeyond, 1.0);
            e->setParam (kAir, 6.0);
            e->setParam (kGuard, 0.0);
            e->reset ();
            auto out = run (*e, in);
            const size_t a = 48000, b = in.l.size ();
            const double rel = toneDb (sum (out, -1.0f), f, a, b) - toneDb (sum (out, 1.0f), f, a, b);
            CHECK (rel < -80.0, "Mono Below %.0f Hz, Character %d: L-R at %.1f Hz is %.1f dB", xover, c, f, rel);
        }
}

TEST (mono_fold_keeps_the_spectrum)
{
    // Width 200 %, Guard 100 %, every Character, mono and stereo pink noise: L + R per band stays
    // within 1.5 dB of the input's
    for (bool stereo : {false, true})
    {
        auto in = pink (6.0, stereo);
        for (int c = 0; c < kNumCharacters; ++c)
        {
            auto e = engine ();
            e->setParam (kCharacter, c);
            e->setParam (kWidth, 2.0);
            e->setParam (kGuard, 1.0);
            e->reset ();
            auto out = run (*e, in);
            const size_t lat = (size_t)e->latency ();
            std::vector<float> mono = sum (out, 1.0f);
            mono.erase (mono.begin (), mono.begin () + (long)lat);
            mono.resize (in.l.size (), 0.0f);
            const auto got = bandDb (mono, 0, in.l.size () - 8192);
            const auto want = bandDb (sum (in, 1.0f), 0, in.l.size () - 8192);
            double worst = 0.0;
            int worstBand = 0;
            for (int k = 0; k < kBands; ++k)
                if (bandHz (k) >= 200.0 && bandHz (k) <= 16000.0 && std::fabs (got[(size_t)k] - want[(size_t)k]) > worst)
                {
                    worst = std::fabs (got[(size_t)k] - want[(size_t)k]);
                    worstBand = k;
                }
            CHECK (worst <= 1.5, "%s, Character %d: %.2f dB off at %.0f Hz", stereo ? "stereo" : "mono", c, worst, bandHz (worstBand));
        }
    }
}

TEST (guard_keeps_the_correlation)
{
    auto in = pink (4.0, false);
    auto corrWith = [&] (double guard) {
        auto e = engine ();
        e->setParam (kCharacter, kEpic);
        e->setParam (kWidth, 2.0);
        e->setParam (kSpace, 0.6);
        e->setParam (kGuard, guard);
        e->reset ();
        return correlation (run (*e, in), 96000, in.l.size ());
    };
    const double free = corrWith (0.0), guarded = corrWith (1.0);
    CHECK (free < 0.0, "unguarded 200 %% goes past decorrelated: %f", free);
    CHECK (guarded > 0.1 && guarded > free + 0.2, "Guard 100 %% keeps the correlation up: %f (free %f)", guarded, free);
}

TEST (space_rings_and_mono_check)
{
    auto in = pink (1.0, false, 0.3f);
    in.l.resize (3 * 48000, 0.0f);
    in.r.resize (3 * 48000, 0.0f);
    auto tailEnergy = [&] (double space) {
        auto e = engine ();
        e->setParam (kSpace, space);
        e->setParam (kDecay, 2000.0);
        e->reset ();
        auto out = run (*e, in);
        return energy (sum (out, -1.0f), 48000 + 24000, 48000 + 48000); // 0.5 to 1 s after the input stops
    };
    CHECK (tailEnergy (1.0) > 1e3 * std::max (1e-12, tailEnergy (0.0)), "Space rings on: %g vs %g", tailEnergy (1.0),
           tailEnergy (0.0));
    auto e = engine ();
    e->setParam (kMonoCheck, 1.0);
    e->reset ();
    auto out = run (*e, pink (0.5, true));
    bool same = true;
    for (size_t i = 0; i < out.l.size (); ++i)
        same &= out.l[i] == out.r[i];
    CHECK (same, "Mono Check: L == R");
    e->setParam (kMonoCheck, 0.0);
    e->setParam (kWidth, 0.0);
    e->setParam (kOutput, 6.0);
    e->reset ();
    auto loud = run (*e, in);
    const size_t lat = (size_t)e->latency ();
    CHECK (std::fabs (loud.l[20000 + lat] / in.l[20000] - dbToGain (6.0)) < 1e-4, "Output +6 dB");
}

TEST (silence_after_a_burst)
{
    // a loud burst, then a minute of silence: no NaN, no denormals, and it dies away
    for (int c = 0; c < kNumCharacters; ++c)
    {
        auto e = engine ();
        e->setParam (kCharacter, c);
        e->setParam (kWidth, 2.0);
        e->setParam (kSpace, 1.0);
        e->setParam (kDecay, 3000.0);
        e->setParam (kDamping, 20000.0);
        e->reset ();
        auto burst = pink (1.0, true, 1.0f);
        run (*e, burst);
        Sig silence;
        silence.l.assign (48000, 0.0f);
        silence.r.assign (48000, 0.0f);
        bool finite = true, subnormal = false;
        float last = 0.0f;
        for (int s = 0; s < 60; ++s)
        {
            auto out = run (*e, silence);
            for (size_t i = 0; i < out.l.size (); ++i)
                for (float v : {out.l[i], out.r[i]})
                {
                    finite &= std::isfinite (v);
                    subnormal |= std::fpclassify (v) == FP_SUBNORMAL;
                    if (s == 59)
                        last = std::max (last, std::fabs (v));
                }
        }
        CHECK (finite && !subnormal, "Character %d: finite %d, subnormal %d", c, finite, subnormal);
        CHECK (last < 1e-6f, "Character %d: dies away (%g after a minute)", c, last);
    }
}

// --- mix awareness -----------------------------------------------------------------
TEST (registry_claim_release_and_limit)
{
    auto reg = std::make_unique<Registry> ();
    std::vector<int> slots;
    for (int i = 0; i < Registry::kSlots; ++i)
        slots.push_back (reg->claim (reg->newId ()));
    CHECK (std::all_of (slots.begin (), slots.end (), [] (int s) { return s >= 0; }) && reg->used () == 64, "64 slots claimed");
    CHECK (reg->claim (reg->newId ()) == -1, "the 65th finds no slot");
    reg->release (slots[10]);
    CHECK (reg->used () == 63 && !reg->inUse (slots[10]), "released");
    const uint64_t id = reg->newId ();
    CHECK (reg->claim (id) == slots[10] && reg->slot (slots[10]).id.load () == id, "a freed slot is claimed again");
    CHECK (reg->slot (slots[10]).data.side[3].load () == 0.0f && reg->slot (slots[10]).data.group.load () == 1u, "cleared");
}

TEST (registry_heartbeat_expiry)
{
    auto reg = std::make_unique<Registry> ();
    Liveness live;
    const int a = reg->claim (reg->newId ());
    live.update (*reg, 480, kSr);
    CHECK (live.alive (a), "a new instance is alive at once");
    for (int i = 0; i < 90; ++i) // 0.9 s of the reader's audio, beating
    {
        reg->beat (a);
        live.update (*reg, 480, kSr);
    }
    CHECK (live.alive (a), "alive while beating");
    for (int i = 0; i < 90; ++i) // 0.9 s without a beat: still within the second
        live.update (*reg, 480, kSr);
    CHECK (live.alive (a), "still alive after 0.9 s of silence");
    for (int i = 0; i < 20; ++i)
        live.update (*reg, 480, kSr);
    CHECK (!live.alive (a), "gone after 1.1 s without a heartbeat");
    reg->beat (a);
    live.update (*reg, 480, kSr);
    CHECK (live.alive (a), "back with the next beat");
    reg->release (a);
    live.update (*reg, 480, kSr);
    CHECK (!live.alive (a), "released: gone");
}

TEST (negotiation_is_order_independent)
{
    std::vector<Peer> peers (7);
    for (size_t i = 0; i < peers.size (); ++i)
    {
        peers[i].id = 100 + (i * 37) % 11;
        peers[i].role = (int)(i % kNumRoles);
        for (int k = 0; k < kBands; ++k)
            peers[i].side[(size_t)k] = (float)(1e-3 * (double)(1 + (i * 7 + (size_t)k * 3) % 13) * (1.0 + 0.1 * white ()));
    }
    Peer self;
    self.id = 105;
    self.role = kWideRole;
    for (int k = 0; k < kBands; ++k)
        self.side[(size_t)k] = 2e-3f;
    const MixOutcome ref = negotiate (self, peers.data (), (int)peers.size (), 0.7);
    bool same = true;
    for (int perm = 0; perm < 20; ++perm)
    {
        std::rotate (peers.begin (), peers.begin () + 1 + perm % 3, peers.end ());
        if (perm % 2)
            std::reverse (peers.begin (), peers.end ());
        const MixOutcome o = negotiate (self, peers.data (), (int)peers.size (), 0.7);
        same &= o.yield == ref.yield && o.roleScale == ref.roleScale && o.mirror == ref.mirror && o.peers == ref.peers;
    }
    CHECK (same, "the same outcome in any order");
    // alone: neutral; Mix Aware 0: nobody yields
    const MixOutcome alone = negotiate (self, nullptr, 0, 1.0);
    CHECK (alone.roleScale == 1.0f && alone.mirror == 1.0f && alone.yield[5] == 1.0f, "alone");
    const MixOutcome deaf = negotiate (self, peers.data (), (int)peers.size (), 0.0);
    CHECK (std::all_of (deaf.yield.begin (), deaf.yield.end (), [] (float y) { return y == 1.0f; }) && deaf.roleScale == 1.0f,
           "Mix Aware 0 ignores the others");
    // the Anchor yields to nobody; twins of one role widen in opposite directions
    Peer anchor = self;
    anchor.role = kAnchor;
    const MixOutcome a = negotiate (anchor, peers.data (), (int)peers.size (), 1.0);
    CHECK (std::all_of (a.yield.begin (), a.yield.end (), [] (float y) { return y == 1.0f; }) && a.roleScale < 0.5f,
           "Anchor: no yielding, narrower (%f)", a.roleScale);
    Peer twinA = self, twinB = self;
    twinA.id = 1;
    twinB.id = 2;
    const MixOutcome ma = negotiate (twinA, &twinB, 1, 1.0), mb = negotiate (twinB, &twinA, 1, 1.0);
    CHECK (ma.mirror == 1.0f && mb.mirror == -1.0f, "twins mirror: %f / %f", ma.mirror, mb.mirror);
}

// Two engines in one registry and group: an Anchor with side only between ~500 Hz and 2 kHz, a Wide
// with full-band material. The Wide gives way in the Anchor's bands only.
static void anchorAndWide (bool anchorFirst, std::array<float, kBands>& wideGains, int& peersSeen)
{
    auto reg = std::make_unique<Registry> ();
    Engine anchor, wide;
    anchor.prepare (kSr, 480);
    wide.prepare (kSr, 480);
    anchor.setParam (kRole, kAnchor);
    anchor.setParam (kGuard, 0.0);
    wide.setParam (kRole, kWideRole);
    wide.setParam (kGuard, 0.0);
    wide.setParam (kAware, 1.0);
    anchor.reset ();
    wide.reset ();
    MixMember ma (*reg), mw (*reg);
    ma.join ();
    mw.join ();
    // the Anchor's source: pink noise band-passed to 500 Hz .. 2 kHz
    gSeed = 99;
    auto band = pink (6.0, false, 0.3f);
    {
        const auto hp = BiquadCoeffs::highPass (600.0, 0.7, kSr), lp = BiquadCoeffs::lowPass (1600.0, 0.7, kSr);
        Biquad a1, a2, b1, b2;
        for (auto& v : band.l)
            v = (float)b2.tick (lp, b1.tick (lp, a2.tick (hp, a1.tick (hp, v))));
        band.r = band.l;
    }
    auto full = pink (6.0, false);
    std::vector<float> ol (480), outR (480);
    for (size_t pos = 0; pos + 480 <= band.l.size (); pos += 480)
    {
        auto step = [&] (Engine& e, MixMember& m, Sig& in) {
            e.process (in.l.data () + pos, in.r.data () + pos, ol.data (), outR.data (), 480);
            m.update (e, 480);
        };
        if (anchorFirst)
        {
            step (anchor, ma, band);
            step (wide, mw, full);
        }
        else
        {
            step (wide, mw, full);
            step (anchor, ma, band);
        }
    }
    for (int k = 0; k < kBands; ++k)
        wideGains[(size_t)k] = wide.bandGain (k);
    peersSeen = ma.peers () + mw.peers ();
}

TEST (anchor_and_wide_share_the_bands)
{
    std::array<float, kBands> g1 {}, g2 {};
    int seen = 0;
    anchorAndWide (true, g1, seen);
    CHECK (seen == 2, "each sees the other (%d)", seen);
    for (int k = 0; k < kBands; ++k)
    {
        const double f = bandHz (k);
        if (f >= 700.0 && f <= 1300.0)
            CHECK (g1[(size_t)k] < 0.6f, "the Wide yields at %.0f Hz: %f", f, g1[(size_t)k]);
        if (f >= 5000.0 && f <= 12000.0)
            CHECK (g1[(size_t)k] > 0.85f, "and keeps its width at %.0f Hz: %f", f, g1[(size_t)k]);
    }
    int s2 = 0;
    anchorAndWide (false, g2, s2);
    float diff = 0.0f;
    for (int k = 0; k < kBands; ++k)
        diff = std::max (diff, std::fabs (g1[(size_t)k] - g2[(size_t)k]));
    CHECK (diff < 0.02f, "processing order does not matter: %f", diff);
}

TEST (groups_are_separate)
{
    auto reg = std::make_unique<Registry> ();
    Engine a, b;
    a.prepare (kSr, 480);
    b.prepare (kSr, 480);
    b.setParam (kGroup, 2.0);
    MixMember ma (*reg), mb (*reg);
    CHECK (ma.join () && mb.join (), "joined");
    auto in = pink (0.1, true);
    std::vector<float> ol (480), outR (480);
    for (size_t pos = 0; pos + 480 <= in.l.size (); pos += 480)
    {
        a.process (in.l.data () + pos, in.r.data () + pos, ol.data (), outR.data (), 480);
        ma.update (a, 480);
        b.process (in.l.data () + pos, in.r.data () + pos, ol.data (), outR.data (), 480);
        mb.update (b, 480);
    }
    CHECK (ma.peers () == 0 && mb.peers () == 0, "different groups: alone (%d, %d)", ma.peers (), mb.peers ());
    b.setParam (kGroup, 1.0);
    for (int i = 0; i < 3; ++i)
    {
        a.process (in.l.data (), in.r.data (), ol.data (), outR.data (), 480);
        ma.update (a, 480);
        b.process (in.l.data (), in.r.data (), ol.data (), outR.data (), 480);
        mb.update (b, 480);
    }
    CHECK (ma.peers () == 1 && mb.peers () == 1, "same group: they meet");
    mb.leave ();
    ma.update (a, 480);
    CHECK (ma.peers () == 0, "left: alone again");
}

TEST (fuzz_and_automation)
{
    uint32_t seed = 7;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFFF) / 16777216.0;
    };
    for (int iter = 0; iter < 40; ++iter)
    {
        Engine e;
        e.prepare (iter % 3 == 0 ? 96000.0 : (iter % 3 == 1 ? 44100.0 : kSr), 512);
        Sig in;
        in.l.resize (30000);
        in.r.resize (30000);
        for (size_t i = 0; i < in.l.size (); ++i)
        {
            in.l[i] = (float)((rnd () * 2 - 1) * ((i / 3000) % 2 ? 1.0 : 1e-4));
            in.r[i] = (float)(rnd () * 2 - 1) * 0.3f;
        }
        Sig out;
        out.l.resize (in.l.size ());
        out.r.resize (in.r.size ());
        for (size_t pos = 0; pos < in.l.size (); pos += 700)
        {
            for (uint32_t id = 0; id < kNumParams; ++id) // automate everything
                if (rnd () < 0.3)
                    e.setParam (id, paramTable ().toPlain (id, rnd ()));
            const int n = (int)std::min<size_t> (700, in.l.size () - pos);
            e.process (in.l.data () + pos, in.r.data () + pos, out.l.data () + pos, out.r.data () + pos, n);
        }
        bool finite = true;
        double pk = 0;
        for (size_t i = 0; i < out.l.size (); ++i)
        {
            finite &= std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
            pk = std::max ({pk, (double)std::fabs (out.l[i]), (double)std::fabs (out.r[i])});
        }
        CHECK (finite && pk < 50.0, "iteration %d: finite %d peak %f", iter, finite, pk);
    }
}

TEST (performance)
{
    auto e = engine ();
    e->setParam (kCharacter, kEpic);
    e->setParam (kWidth, 1.5);
    e->setParam (kSpace, 0.5);
    e->setParam (kBeyond, 0.5);
    e->reset ();
    auto in = pink (10.0, true);
    const auto t0 = std::chrono::steady_clock::now ();
    run (*e, in);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    std::printf ("    CPU: %.2f%% of one core (stereo)\n", 100.0 * secs / 10.0);
    CHECK (secs / 10.0 < 0.05, "too slow");
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

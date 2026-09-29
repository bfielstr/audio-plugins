// Tests for the Transient stage of Detonatr. Run: ./detonatr_transient_tests [filter]
//
// Most tests read the gain straight off the output: the hit goes in the left channel and the right
// channel carries a tiny constant (-100 dBFS, far below anything the detector reacts to). The gain is
// stereo-linked, so right out / right in is the gain applied at every sample.
#include "Transient.h"

#include "Harness.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

using namespace detonatr;

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kProbe = 1e-5f;

using Buf = std::vector<float>;

// A synthetic impact: 82 Hz plus a few (slightly stretched) partials, a 1 ms attack and an exponential
// decay (tau seconds), added into x from sample `start`.
void addHit (Buf& x, double sr, int start, double amp, double tau)
{
    static const double f[] = {82.0, 165.0, 249.0, 334.0, 507.0};
    static const double a[] = {0.55, 0.25, 0.14, 0.09, 0.05};
    for (int i = start; i < (int)x.size (); ++i)
    {
        const double t = (i - start) / sr;
        const double env = amp * std::min (1.0, t / 0.001) * std::exp (-t / tau);
        if (env < 1e-7 && t > 0.01)
            break;
        double s = 0.0;
        for (int k = 0; k < 5; ++k)
            s += a[k] * std::sin (2.0 * kPi * f[k] * t);
        x[(size_t)i] += (float)(env * s);
    }
}

struct Out
{
    Buf l, r;
};

Out run (Transient& t, const Buf& inL, const Buf& inR, int block)
{
    Out o {inL, inR};
    for (int i = 0; i < (int)inL.size (); i += block)
    {
        const int n = std::min (block, (int)inL.size () - i);
        t.process (o.l.data () + i, o.r.data () + i, n);
    }
    return o;
}

// hit in the left channel, the gain probe in the right
Out runProbe (Transient& t, const Buf& inL, int block = 256)
{
    return run (t, inL, Buf (inL.size (), kProbe), block);
}

Buf gainOf (const Out& o)
{
    Buf g (o.r.size ());
    for (size_t i = 0; i < g.size (); ++i)
        g[i] = o.r[i] / kProbe;
    return g;
}

// where each spike starts: the gain reaching 0 dB after having been below -6 dB
std::vector<int> spikeStarts (const Buf& g)
{
    std::vector<int> s;
    bool low = false;
    for (int i = 0; i < (int)g.size (); ++i)
    {
        if (g[(size_t)i] < 0.5f)
            low = true;
        else if (low && g[(size_t)i] >= 0.9999f)
        {
            s.push_back (i);
            low = false;
        }
    }
    return s;
}

// level of the output against the input it came from (delayed by lat), in dB, over [a, b) of the input
double levelDb (const Buf& in, const Buf& out, int lat, int a, int b)
{
    double ei = 0.0, eo = 0.0;
    for (int i = a; i < b; ++i)
    {
        ei += (double)in[(size_t)i] * in[(size_t)i];
        eo += (double)out[(size_t)(i + lat)] * out[(size_t)(i + lat)];
    }
    return 10.0 * std::log10 ((eo + 1e-30) / (ei + 1e-30));
}

double maxStep (const Buf& x, int a, int b)
{
    double m = 0.0;
    for (int i = std::max (1, a); i < std::min (b, (int)x.size ()); ++i)
        m = std::max (m, (double)std::fabs (x[(size_t)i] - x[(size_t)i - 1]));
    return m;
}

void setUp (Transient& t, double sr, double spike, double fall, double drop, double sens)
{
    t.setSpike (spike);
    t.setFall (fall);
    t.setDrop (drop);
    t.setSensitivity (sens);
    t.prepare (sr, 512);
}

int ms (double sr, double m) { return (int)std::lround (m * 0.001 * sr); }
} // namespace

TEST (latency_is_the_look_ahead)
{
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        Transient t;
        t.prepare (sr, 512);
        const int lat = t.latency ();
        std::printf ("    %6.0f Hz: latency %d samples (%.2f ms)\n", sr, lat, 1000.0 * lat / sr);
        CHECK (lat > 0 && std::fabs (1000.0 * lat / sr - 5.0) < 0.05, "latency %d at %.0f Hz", lat, sr);
        t.setDrop (40.0);
        t.setSpike (20.0);
        t.setSensitivity (3.0);
        CHECK (t.latency () == lat, "latency changes with the settings");
    }
}

TEST (drop_0_is_a_pure_delay)
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    const double sr = 48000.0;
    Transient t;
    setUp (t, sr, 3.0, 5.0, 0.0, 9.0);
    const int lat = t.latency ();
    const int n = (int)sr * 2;
    Buf l (n, 0.0f), r (n, 0.0f);
    for (int k = 0; k < 6; ++k)
        addHit (l, sr, ms (sr, 50.0 + 300.0 * k), 0.9, 0.15);
    for (int i = 0; i < n; ++i)
        r[(size_t)i] = 0.5f * l[(size_t)i] + 0.01f * u (rng);
    // odd block sizes
    Out o {l, r};
    std::uniform_int_distribution<int> bs (1, 512);
    for (int i = 0; i < n;)
    {
        const int m = std::min (bs (rng) | 1, n - i);
        t.process (o.l.data () + i, o.r.data () + i, m);
        i += m;
    }
    double err = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float el = i >= lat ? l[(size_t)(i - lat)] : 0.0f, er = i >= lat ? r[(size_t)(i - lat)] : 0.0f;
        err = std::max (err, (double)std::max (std::fabs (o.l[(size_t)i] - el), std::fabs (o.r[(size_t)i] - er)));
    }
    std::printf ("    max |out - in delayed %d| = %g\n", lat, err);
    CHECK (err == 0.0, "Drop 0 is not the input delayed (error %g)", err);
}

TEST (single_hit_spike_then_drop)
{
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        Transient t;
        setUp (t, sr, 3.0, 5.0, 24.0, 9.0);
        const int lat = t.latency ();
        const int n = (int)(sr * 1.5), on = ms (sr, 200.0);
        Buf l (n, 0.0f);
        addHit (l, sr, on, 0.9, 0.3);
        const Out o = runProbe (t, l);
        const Buf g = gainOf (o);
        const auto s = spikeStarts (g);
        CHECK (s.size () == 1, "%.0f Hz: %d spikes for one hit", sr, (int)s.size ());
        if (s.empty ())
            continue;
        const double errMs = 1000.0 * (s[0] - (on + lat)) / sr;
        // full level from the spike's start to 3 ms after the onset; the drop 20 .. 500 ms after
        float minSpike = 1.0f;
        for (int i = s[0]; i < on + lat + ms (sr, 3.0); ++i)
            minSpike = std::min (minSpike, g[(size_t)i]);
        const double spikeDb = levelDb (l, o.l, lat, on, on + ms (sr, 3.0));
        const double bodyDb = levelDb (l, o.l, lat, on + ms (sr, 20.0), on + ms (sr, 500.0));
        // the gain before the ramp up (still at the Drop) and just after the Fall
        const double preDb = 20.0 * std::log10 (g[(size_t)(on + lat - ms (sr, 1.0))]);
        const double postFallDb = 20.0 * std::log10 (g[(size_t)(on + lat + ms (sr, 8.2))]);
        std::printf ("    %6.0f Hz: spike starts %+.3f ms from the onset, gain in spike >= %.5f, level 0..3 ms %+.2f dB, "
                     "20..500 ms %+.2f dB, gain before %+.1f dB, after fall %+.2f dB\n",
                     sr, errMs, minSpike, spikeDb, bodyDb, preDb, postFallDb);
        CHECK (std::fabs (errMs) <= 0.5, "spike starts %.3f ms off the onset", errMs);
        CHECK (minSpike >= 0.9999f, "the spike isn't at full level (%.5f)", minSpike);
        CHECK (std::fabs (spikeDb) < 0.1, "first 3 ms at %.2f dB", spikeDb);
        CHECK (std::fabs (bodyDb + 24.0) < 1.0, "body at %.2f dB, not -24", bodyDb);
        CHECK (std::fabs (preDb + 24.0) < 0.1 && std::fabs (postFallDb + 24.0) < 0.1, "gain before %.2f / after fall %.2f",
               preDb, postFallDb);

        if (sr == 48000.0)
        {
            // what the stage is for: after it, a saturator pushed hard (here +24 dB into tanh) brings the
            // body back up dense while the spike still pokes out. Crest factor and where the loudest 5 ms sit.
            auto report = [&] (const Buf& x, int from, const char* what) {
                double peak = 0.0, e = 0.0, best = 0.0;
                int bestAt = 0;
                const int w = ms (sr, 5.0), len = ms (sr, 700.0);
                for (int i = from; i < from + len; ++i)
                {
                    peak = std::max (peak, (double)std::fabs (x[(size_t)i]));
                    e += (double)x[(size_t)i] * x[(size_t)i];
                }
                for (int i = from; i + w < from + len; i += w / 4)
                {
                    double ew = 0.0;
                    for (int k = 0; k < w; ++k)
                        ew += (double)x[(size_t)(i + k)] * x[(size_t)(i + k)];
                    if (ew > best)
                        best = ew, bestAt = i - from;
                }
                std::printf ("      %-32s crest %5.1f dB, loudest 5 ms starts %5.1f ms in\n", what,
                             20.0 * std::log10 (peak / std::sqrt (e / len)), 1000.0 * bestAt / sr);
            };
            Buf satIn (n), satOut (n);
            for (int i = 0; i < n; ++i)
            {
                satIn[(size_t)i] = (float)std::tanh (15.85 * l[(size_t)i]) ;
                satOut[(size_t)i] = (float)std::tanh (15.85 * o.l[(size_t)i]);
            }
            report (l, on, "input");
            report (satIn, on, "input -> sat +24 dB");
            report (satOut, on + lat, "Transient -> sat +24 dB");
        }
    }
}

TEST (two_hits_two_spikes)
{
    const double sr = 48000.0;
    const int n = (int)(sr * 1.5), on1 = ms (sr, 100.0), on2 = ms (sr, 400.0);
    // decays over about 1 s (tau 0.15 s: -58 dB at 1 s); hit 1's tail is 17 dB down when hit 2 lands
    for (int block : {256, 97, 1})
    {
        Transient t;
        setUp (t, sr, 3.0, 5.0, 24.0, 9.0);
        const int lat = t.latency ();
        Buf l (n, 0.0f);
        addHit (l, sr, on1, 0.8, 0.15);
        addHit (l, sr, on2, 0.8, 0.15);
        const auto s = spikeStarts (gainOf (runProbe (t, l, block)));
        std::printf ("    block %3d: %d spikes", block, (int)s.size ());
        for (size_t k = 0; k < s.size (); ++k)
            std::printf ("%s at %+.3f ms from onset %d", k ? "," : ":", 1000.0 * (s[k] - lat - (k ? on2 : on1)) / sr, (int)k + 1);
        std::printf ("\n");
        CHECK (s.size () == 2, "%d spikes for two hits", (int)s.size ());
        if (s.size () == 2)
        {
            CHECK (std::fabs (1000.0 * (s[0] - lat - on1) / sr) <= 0.5, "spike 1 is off its onset");
            CHECK (std::fabs (1000.0 * (s[1] - lat - on2) / sr) <= 0.5, "spike 2 is off its onset");
        }
    }
    // hit 2 landing at different points of the tail's cycle
    {
        double worst = 0.0, sum = 0.0;
        int missed = 0;
        const int steps = 16;
        for (int k = 0; k < steps; ++k)
        {
            Transient t;
            setUp (t, sr, 3.0, 5.0, 24.0, 9.0);
            const int lat = t.latency (), at = on2 + k * (int)(sr / 82.0) / steps;
            Buf l (n, 0.0f);
            addHit (l, sr, on1, 0.8, 0.15);
            addHit (l, sr, at, 0.8, 0.15);
            const auto s = spikeStarts (gainOf (runProbe (t, l)));
            if (s.size () != 2)
            {
                ++missed;
                continue;
            }
            const double e = 1000.0 * (s[1] - lat - at) / sr;
            worst = std::max (worst, std::fabs (e));
            sum += e;
        }
        std::printf ("    hit 2 at %d points across a cycle of the tail: %d missed, spike 2 off its onset by %+.3f ms on "
                     "average, %.3f ms at worst\n", steps, missed, sum / std::max (1, steps - missed), worst);
        CHECK (missed == 0 && worst <= 0.6, "missed %d, worst %.3f ms", missed, worst);
    }
    // a slower decay (tau 0.3 s): hit 1's tail is only 8.7 dB down when hit 2 lands, and hit 2's peaks rise
    // about 6 dB over it, so only a low Sensitivity sees it. Its first peak (1 ms in) rises only 2 dB over
    // the tail; the jump reaches 3 dB at its second peak, 10 ms in, too late for the 5 ms look-ahead to
    // put the spike at the onset, so the spike lands on that peak (printed, not checked)
    for (double sens : {3.0, 6.0, 9.0})
    {
        Transient t;
        setUp (t, sr, 3.0, 5.0, 24.0, sens);
        const int lat = t.latency ();
        Buf l (n, 0.0f);
        addHit (l, sr, on1, 0.8, 0.3);
        addHit (l, sr, on2, 0.8, 0.3);
        const auto s = spikeStarts (gainOf (runProbe (t, l)));
        std::printf ("    slow decay, Sensitivity %.0f dB: %d spikes", sens, (int)s.size ());
        for (size_t k = 0; k < s.size (); ++k)
            std::printf ("%s at %+.3f ms from onset %d", k ? "," : ":", 1000.0 * (s[k] - lat - (k ? on2 : on1)) / sr, (int)k + 1);
        std::printf ("\n");
        if (sens == 3.0)
            CHECK (s.size () == 2, "%d spikes for two hits at Sensitivity 3", (int)s.size ());
    }
}

TEST (no_retrigger_on_sustain_or_decay)
{
    const double sr = 48000.0;
    for (double sens : {3.0, 6.0, 9.0, 24.0})
    {
        Transient t;
        // a long decaying hit
        setUp (t, sr, 3.0, 5.0, 24.0, sens);
        const int n = (int)(sr * 3.0);
        Buf decay (n, 0.0f);
        addHit (decay, sr, ms (sr, 100.0), 0.9, 0.5);
        const int nDecay = (int)spikeStarts (gainOf (runProbe (t, decay))).size ();

        // a sustained tone (the same partials, held), 40 Hz, and a low rumble of filtered noise, each
        // coming in with a 5 ms fade
        Buf tone (n, 0.0f), low (n, 0.0f), rumble (n, 0.0f);
        std::mt19937 rng (3);
        std::normal_distribution<double> nd (0.0, 1.0);
        double lp1 = 0.0, lp2 = 0.0;
        const double c = 1.0 - std::exp (-2.0 * kPi * 150.0 / sr);
        Buf rumbleRaw (n);
        double rms = 0.0;
        for (int i = 0; i < n; ++i)
        {
            lp1 += (nd (rng) - lp1) * c;
            lp2 += (lp1 - lp2) * c;
            rumbleRaw[(size_t)i] = (float)lp2;
            rms += lp2 * lp2;
        }
        rms = std::sqrt (rms / n);
        for (int i = ms (sr, 100.0); i < n; ++i)
        {
            const double tt = (i - ms (sr, 100.0)) / sr, fade = std::min (1.0, tt / 0.005);
            tone[(size_t)i] = (float)(0.6 * fade * (0.55 * std::sin (2 * kPi * 82 * tt) + 0.25 * std::sin (2 * kPi * 165 * tt)
                                                   + 0.14 * std::sin (2 * kPi * 249 * tt)));
            low[(size_t)i] = (float)(0.6 * fade * std::sin (2 * kPi * 40 * tt));
            rumble[(size_t)i] = (float)(0.2 * fade * rumbleRaw[(size_t)i] / rms);
        }
        t.reset ();
        const int nTone = (int)spikeStarts (gainOf (runProbe (t, tone))).size ();
        t.reset ();
        const int nLow = (int)spikeStarts (gainOf (runProbe (t, low))).size ();
        t.reset ();
        const int nRumble = (int)spikeStarts (gainOf (runProbe (t, rumble))).size ();
        std::printf ("    Sensitivity %4.1f dB: onsets in a 3 s decay %d, held 82 Hz tone %d, held 40 Hz %d, "
                     "150 Hz-lowpassed noise rumble %d\n", sens, nDecay, nTone, nLow, nRumble);
        CHECK (nDecay == 1 && nTone == 1 && nLow == 1, "retriggered on a decay (%d) / held tone (%d, %d)", nDecay, nTone, nLow);
        if (sens >= 9.0) // lower, the noise's own swells (a real 3..6 dB jump now and then) count as onsets
            CHECK (nRumble == 1, "the rumble retriggered %d times at %.0f dB", nRumble, sens);
    }
}

TEST (sensitivity_ignores_a_weaker_hit)
{
    const double sr = 48000.0;
    const int n = (int)(sr * 1.0), on1 = ms (sr, 50.0), on2 = ms (sr, 350.0);
    // a loud hit, then 300 ms later (its tail now -26 dB) one 12 dB weaker: a jump of about 14 dB
    Buf l (n, 0.0f);
    addHit (l, sr, on1, 0.8, 0.1);
    addHit (l, sr, on2, 0.2, 0.1);
    int counts[2];
    const double sens[2] = {6.0, 20.0};
    for (int k = 0; k < 2; ++k)
    {
        Transient t;
        setUp (t, sr, 3.0, 5.0, 24.0, sens[k]);
        counts[k] = (int)spikeStarts (gainOf (runProbe (t, l))).size ();
    }
    std::printf ("    weak second hit: Sensitivity 6 dB -> %d onsets, 20 dB -> %d onsets\n", counts[0], counts[1]);
    CHECK (counts[0] == 2, "Sensitivity 6 dB missed the weaker hit (%d)", counts[0]);
    CHECK (counts[1] == 1, "Sensitivity 20 dB caught the weaker hit (%d)", counts[1]);
}

TEST (no_clicks)
{
    const double sr = 48000.0;
    // the hardest case for clicks: a hit landing on a loud held tone (about -12 dB under the hit), so the
    // gain ramps up and falls with lots of signal under it
    const int n = (int)(sr * 1.0), on = ms (sr, 400.0);
    Buf l (n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double tt = i / sr;
        l[(size_t)i] = (float)(0.22 * std::min (1.0, tt / 0.005) * (0.7 * std::sin (2 * kPi * 82 * tt) + 0.3 * std::sin (2 * kPi * 249 * tt)));
    }
    addHit (l, sr, on, 0.9, 0.3);
    for (double fall : {5.0, 1.0})
    {
        Transient t;
        setUp (t, sr, 3.0, fall, 24.0, 6.0);
        const int lat = t.latency ();
        const Out o = runProbe (t, l);
        const auto s = spikeStarts (gainOf (o));
        // the signal's own steepest step (as it comes out, i.e. at the gain it's at), against the output's
        const double inStep = maxStep (l, on - ms (sr, 20.0), on + ms (sr, 50.0));
        const double rampStep = maxStep (o.l, on + lat - ms (sr, 2.0), on + lat + ms (sr, 0.2));
        const double fallStep = maxStep (o.l, on + lat + ms (sr, 2.5), on + lat + ms (sr, 3.0 + fall + 2.0));
        const double allStep = maxStep (o.l, 0, n);
        std::printf ("    Fall %.0f ms: %d onsets; steepest step: input %.4f, ramp up %.4f (%.2fx), fall %.4f (%.2fx), "
                     "whole output %.4f (%.2fx)\n", fall, (int)s.size (), inStep, rampStep, rampStep / inStep, fallStep,
                     fallStep / inStep, allStep, allStep / maxStep (l, 0, n));
        CHECK (s.size () == 2, "expected onsets at the tone's start and the hit (%d)", (int)s.size ());
        CHECK (rampStep < 1.5 * inStep && fallStep < 1.5 * inStep, "a click: ramp %.4f / fall %.4f vs the signal's %.4f",
               rampStep, fallStep, inStep);
        CHECK (allStep < 1.5 * maxStep (l, 0, n), "a click somewhere");
    }
}

TEST (block_size_does_not_matter)
{
    const double sr = 44100.0;
    const int n = (int)(sr * 1.2);
    Buf l (n, 0.0f), r (n, 0.0f);
    addHit (l, sr, 1000, 0.9, 0.2);
    addHit (r, sr, 1003, 0.7, 0.2);
    addHit (l, sr, ms (sr, 330.0), 0.9, 0.2);
    addHit (r, sr, ms (sr, 700.0), 0.9, 0.2);
    Out ref;
    bool same = true;
    for (int block : {1, 7, 64, 255, 512})
    {
        Transient t;
        setUp (t, sr, 1.5, 12.0, 30.0, 8.0);
        const Out o = run (t, l, r, block);
        if (block == 1)
            ref = o;
        else
            same = same && o.l == ref.l && o.r == ref.r;
    }
    CHECK (same, "the output depends on the block size");
}

TEST (robustness_silence_and_fuzz)
{
    const double sr = 48000.0;
    {
        Transient t;
        t.prepare (sr, 512);
        Buf l (48000, 0.0f), r (48000, 0.0f);
        const Out o = run (t, l, r, 480);
        bool zero = true;
        for (int i = 0; i < 48000; ++i)
            zero = zero && o.l[(size_t)i] == 0.0f && o.r[(size_t)i] == 0.0f;
        CHECK (zero, "silence in, not silence out");
    }
    std::mt19937 rng (11);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    bool finite = true, bounded = true;
    for (double srf : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
    {
        Transient t;
        t.prepare (srf, 512);
        const int lat = t.latency (), n = (int)srf * 3;
        Buf l (n), r (n);
        // bursts of noise at wildly different levels, clicks, tiny values and silence
        double level = 0.1;
        for (int i = 0; i < n; ++i)
        {
            if (u (rng) < 0.0005)
                level = std::pow (10.0, -8.0 + 9.0 * u (rng)) * (u (rng) < 0.2 ? 0.0 : 1.0);
            l[(size_t)i] = (float)(level * (2.0 * u (rng) - 1.0));
            r[(size_t)i] = (float)(level * (2.0 * u (rng) - 1.0));
            if (u (rng) < 0.0001)
                l[(size_t)i] = 4.0f;
        }
        Out o {l, r};
        for (int i = 0; i < n;)
        {
            t.setSpike (0.1 + 19.9 * u (rng));
            t.setFall (0.1 + 49.9 * u (rng));
            t.setDrop (48.0 * u (rng));
            t.setSensitivity (3.0 + 21.0 * u (rng));
            const int m = std::min (1 + (int)(u (rng) * 512), n - i);
            t.process (o.l.data () + i, o.r.data () + i, m);
            i += m;
        }
        for (int i = 0; i < n; ++i)
        {
            finite = finite && std::isfinite (o.l[(size_t)i]) && std::isfinite (o.r[(size_t)i]);
            // the gain never goes above 0 dB
            const float el = i >= lat ? l[(size_t)(i - lat)] : 0.0f, er = i >= lat ? r[(size_t)(i - lat)] : 0.0f;
            bounded = bounded && std::fabs (o.l[(size_t)i]) <= std::fabs (el) && std::fabs (o.r[(size_t)i]) <= std::fabs (er);
        }
    }
    CHECK (finite, "NaN / inf out");
    CHECK (bounded, "the gain went above 0 dB");
}

TEST (cpu)
{
    const double sr = 48000.0;
    Transient t;
    setUp (t, sr, 3.0, 5.0, 24.0, 9.0);
    const int n = (int)sr * 60;
    Buf l (n, 0.0f), r (n, 0.0f);
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> u (-0.05f, 0.05f);
    for (int i = 0; i < n; ++i)
        l[(size_t)i] = r[(size_t)i] = u (rng);
    for (int k = 0; k < 100; ++k)
        addHit (l, sr, (int)(k * 0.6 * sr), 0.9, 0.2);
    const auto t0 = std::chrono::steady_clock::now ();
    const Out o = run (t, l, r, 256);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    std::printf ("    60 s of stereo at 48 kHz in %.1f ms: %.3f%% of real time (checksum %g)\n", 1000.0 * secs,
                 100.0 * secs / 60.0, (double)o.l[(size_t)n / 2]);
    CHECK (secs / 60.0 < 0.02, "too slow: %.2f%% of real time", 100.0 * secs / 60.0);
}

DETONATR_TEST_MAIN

// The Motion stage: the Doppler swarm.
#include "Harness.h"
#include "Motion.h"
#include "Signals.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace detonatr;

namespace {
constexpr double kSr = 48000.0;

Motion make ()
{
    Motion m;
    m.prepare (kSr, 256);
    return m;
}

// one orb on a circle, nothing else: a clean Doppler test
Motion oneOrb (double speed, double distance = 6.0, double radius = 2.0)
{
    Motion m;
    m.setOrbs (1);
    m.setPattern (Motion::kOrbit);
    m.setRandomness (0.0);
    m.setSpread (0.0);
    m.setFloor (false);
    m.setMix (1.0);
    m.setSpeed (speed);
    m.setDistance (distance);
    m.setRadius (radius);
    m.prepare (kSr, 256);
    return m;
}

// the frequency over each period (from one rising zero crossing to the next, interpolated), from `from` on
std::vector<double> periods (const std::vector<float>& y, size_t from)
{
    std::vector<double> f;
    double last = -1.0;
    for (size_t i = from + 1; i < y.size (); ++i)
        if (y[i - 1] < 0.0f && y[i] >= 0.0f)
        {
            const double t = (double)(i - 1) + y[i - 1] / (double)(y[i - 1] - y[i]);
            if (last >= 0.0)
                f.push_back (kSr / (t - last));
            last = t;
        }
    return f;
}
} // namespace

TEST (doppler_matches_theory)
{
    // a 1 kHz tone from an orb at 30 m/s on a circle (radius 2 m, its centre 6 m ahead): the listener is
    // outside the circle, so twice a turn the orb moves straight towards or away from the ears at its
    // full speed: f c / (c - v) and f c / (c + v)
    const double v = 30.0, c = Motion::kSoundSpeed;
    Motion m = oneOrb (v);
    const auto x = sig::sine (1000.0, 0.5, 3.0, kSr);
    const auto y = sig::run (m, x);
    const auto f = periods (y, 24000);
    double hi = 0.0, lo = 1e9;
    for (size_t i = 1; i + 1 < f.size (); ++i)
    {
        const double s = (f[i - 1] + f[i] + f[i + 1]) / 3.0; // over three periods
        hi = std::max (hi, s);
        lo = std::min (lo, s);
    }
    const double wantHi = 1000.0 * c / (c - v), wantLo = 1000.0 * c / (c + v);
    std::printf ("    highest %.2f Hz (theory %.2f), lowest %.2f Hz (theory %.2f)\n", hi, wantHi, lo, wantLo);
    CHECK (std::fabs (hi / wantHi - 1.0) < 0.003, "approaching: %.2f vs %.2f Hz", hi, wantHi);
    CHECK (std::fabs (lo / wantLo - 1.0) < 0.003, "receding: %.2f vs %.2f Hz", lo, wantLo);
}

TEST (doppler_follows_the_orbs_radial_speed)
{
    // over time: the pitch at each moment against the orb's radial speed from its positions
    const double v = 20.0, c = Motion::kSoundSpeed;
    Motion m = oneOrb (v, 4.0, 1.5);
    const auto x = sig::sine (1000.0, 0.5, 1.0, kSr);
    std::vector<float> y (x.size ());
    std::vector<double> dist (x.size ());
    std::vector<float> l (16), r (16);
    for (size_t a = 0; a < x.size (); a += 16)
    {
        const auto p = m.orbPosition (0);
        std::copy (x.begin () + (ptrdiff_t)a, x.begin () + (ptrdiff_t)a + 16, l.begin ());
        std::copy (l.begin (), l.end (), r.begin ());
        m.process (l.data (), r.data (), 16);
        for (int i = 0; i < 16; ++i)
        {
            y[a + (size_t)i] = l[(size_t)i];
            dist[a + (size_t)i] = std::sqrt (p.x * p.x + p.y * p.y + p.z * p.z);
        }
    }
    // the source's radial speed at the moment the sound left it (the orb's clock runs with the output's:
    // the sound heard at t left the orb at t - d / c; the fixed 10 ms only delays the audio); compare
    // the measured frequency with c / (c + v_r) there
    double worst = 0.0;
    int n = 0;
    double last = -1.0;
    for (size_t i = 24001; i < y.size (); ++i)
        if (y[i - 1] < 0.0f && y[i] >= 0.0f)
        {
            const double t = (double)(i - 1) + y[i - 1] / (double)(y[i - 1] - y[i]);
            if (last >= 0.0)
            {
                const double fm = kSr / (t - last), mid = 0.5 * (t + last);
                // the emission time of the sample at `mid`
                const double em = mid - dist[(size_t)mid] / c * kSr;
                const size_t e = (size_t)em;
                const double vr = (dist[e + 48] - dist[e - 48]) / (96.0 / kSr);
                const double want = 1000.0 * c / (c + vr);
                worst = std::max (worst, std::fabs (fm / want - 1.0));
                ++n;
            }
            last = t;
        }
    std::printf ("    %d periods: the largest error against c / (c + v_r) %.3f %%\n", n, 100.0 * worst);
    CHECK (n > 400 && worst < 0.002, "the pitch follows the radial speed: %.3f %%", 100.0 * worst);
}

TEST (standing_still_keeps_the_pitch)
{
    Motion m = oneOrb (0.0);
    const auto x = sig::sine (1000.0, 0.5, 1.0, kSr);
    const auto y = sig::run (m, x);
    const auto f = periods (y, 24000);
    double worst = 0.0;
    for (double v : f)
        worst = std::max (worst, std::fabs (v - 1000.0));
    CHECK (worst < 0.5, "1 kHz stays 1 kHz: off by %.3f Hz at most", worst);
}

TEST (latency_and_dry)
{
    Motion m = make ();
    CHECK (m.latency () == 480, "10 ms at 48 kHz: %d", m.latency ());
    m.setMix (0.0);
    const auto x = sig::noise (0.2, 0.5, kSr, 2);
    const auto y = sig::run (m, x, 173);
    double err = 0.0;
    for (size_t i = (size_t)m.latency (); i < x.size (); ++i)
        err = std::max (err, (double)std::fabs (y[i] - x[i - (size_t)m.latency ()]));
    CHECK (err < 1e-7, "Mix 0: the input delayed by the latency (%.2g)", err);
    for (double sr : {44100.0, 96000.0})
    {
        Motion k;
        k.prepare (sr, 512);
        CHECK (k.latency () == (int)std::lround (0.01 * sr), "10 ms at %.0f Hz: %d", sr, k.latency ());
    }
}

TEST (swarm_is_wide_and_level_matched)
{
    Motion m = make (); // the defaults: six orbs swarming, Liquid Debris-like
    m.setOrbs (6);
    m.setPattern (Motion::kSwarm);
    m.setSpeed (18.0);
    m.setDistance (3.0);
    m.setRadius (2.0);
    m.setSpread (0.8);
    m.setRandomness (0.6);
    m.setFloor (true);
    m.setMix (1.0);
    m.reset ();
    const auto x = sig::noise (0.1, 3.0, kSr, 8);
    std::vector<float> r;
    const auto l = sig::run (m, x, 256, &r);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = 48000; i < x.size (); ++i)
    {
        lr += (double)l[i] * r[i];
        ll += (double)l[i] * l[i];
        rr += (double)r[i] * r[i];
    }
    const double corr = lr / std::sqrt (ll * rr);
    const double d = sig::db (0.5 * (sig::rms (l, 48000, x.size ()) + sig::rms (r, 48000, x.size ())) / sig::rms (x, 48000, x.size ()));
    std::printf ("    left / right correlation %.2f, level %+.1f dB\n", corr, d);
    CHECK (corr < 0.9, "the swarm spreads across the stereo field: %.2f", corr);
    CHECK (std::fabs (d) < 4.0, "about as loud as the input: %+.1f dB", d);
    // the orbs stay in their ball round the centre
    bool inside = true;
    for (int k = 0; k < 6; ++k)
    {
        const auto p = m.orbPosition (k);
        inside = inside && std::sqrt (p.x * p.x + (p.y - 3.0) * (p.y - 3.0) + p.z * p.z) <= 2.0 + 1e-6;
    }
    CHECK (inside, "every orb within the radius");
}

TEST (finite_everywhere)
{
    std::mt19937 rng (6);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Motion m = make ();
    bool ok = true;
    for (int round = 0; round < 16; ++round)
    {
        m.setOrbs (1 + (int)(u (rng) * 16));
        m.setPattern (u (rng) < 0.5 ? 0 : 1);
        m.setSpeed (u (rng) * 80.0);
        m.setDistance (0.5 + u (rng) * 19.5);
        m.setRadius (0.1 + u (rng) * 2.9);
        m.setSpread (u (rng));
        m.setRandomness (u (rng));
        m.setFloor (u (rng) < 0.5);
        m.setMix (u (rng));
        auto x = sig::noise (u (rng) < 0.3 ? 3.0 : 0.1, 0.25, kSr, (unsigned)round);
        if (round % 4 == 0)
            std::fill (x.begin (), x.end (), 0.0f);
        ok = ok && sig::finite (sig::run (m, x, 1 + (int)(u (rng) * 600)));
    }
    CHECK (ok, "finite with random settings, loud input and silence");
}

DETONATR_TEST_MAIN

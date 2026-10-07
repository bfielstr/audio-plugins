#include "Movement.h"

#include "Params.h"

#include <algorithm>
#include <cmath>

namespace moistr {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kPi = 3.14159265358979323846;

// a point of a random curve: -1 .. 1, from its key and index alone
double point (uint64_t key, int64_t i)
{
    Rng r (key ^ ((uint64_t)i * 0xD1B54A32D192ED03ull));
    return 2.0 * r.uniform () - 1.0;
}

// a smooth random curve through a random point at every whole x (smoothstep between them)
double smoothRandom (uint64_t key, double x)
{
    const double f = std::floor (x);
    const double t = x - f, s = t * t * (3.0 - 2.0 * t);
    const int64_t i = (int64_t)f;
    const double a = point (key, i), b = point (key, i + 1);
    return a + (b - a) * s;
}

// log-uniform in lo .. hi
double logRange (Rng& r, double lo, double hi) { return lo * std::pow (hi / lo, r.uniform ()); }

} // namespace

double Channel::value (double theta) const
{
    const double x = rate * theta + phase;
    const double sines = 0.7 * std::sin (kTwoPi * x) + 0.3 * std::sin (kTwoPi * (rate2 * x + phase2));
    // (the random curve takes a new point every half cycle: it wanders about as fast as the sines)
    return (1.0 - blend) * sines + blend * smoothRandom (key, 2.0 * x);
}

bool BandMotion::risesOn (int64_t step) const
{
    const int64_t n = (int64_t)steps * loop;
    const int64_t i = ((step % n) + n) % n;
    return ((mask >> i) & 1u) != 0;
}

double BandMotion::lift (double theta, double secPerCycle, double riseSec, double fallSec) const
{
    const double spc = std::max (secPerCycle, 1e-6), r = std::max (riseSec, 1e-6), f = std::max (fallSec, 1e-6);
    const double stepSec = spc / steps;
    const double upEnd = std::max (r, hold * stepSec); // (the rise always completes)
    const double end = upEnd + f;
    // every event has the same shape: once one (going back step by step) is over, the earlier ones are too
    const double u = theta * steps - offset;
    int64_t j = (int64_t)std::floor (u);
    double best = 0.0;
    for (int guard = 0; guard < 8192; ++guard, --j)
    {
        const double dt = (u - (double)j) * stepSec; // seconds since step j's onset
        if (dt > end)
            break;
        if (!risesOn (j))
            continue;
        const double v = dt < r ? dt / r : (dt < upEnd ? 1.0 : 1.0 - (dt - upEnd) / f);
        if (v > best)
        {
            best = v;
            if (best >= 1.0)
                break;
        }
    }
    best = std::clamp (best, 0.0, 1.0);
    return 0.5 - 0.5 * std::cos (kPi * best);
}

double lowXoverForSeed (int seed)
{
    const int s = std::clamp (seed, kMinSeed, kMaxSeed);
    // frac (s x phi) in 53-bit fixed point: exact, and evenly spread over the seeds
    const double u = (double)(((uint64_t)s * 0x9E3779B97F4A7C15ull) >> 11) * (1.0 / 9007199254740992.0);
    return kLowXoverMin * std::pow (kLowXoverMax / kLowXoverMin, u);
}

Pattern makePattern (int seed, int pass)
{
    Pattern p;
    p.seed = seed;
    p.pass = pass;
    p.lowXover = lowXoverForSeed (seed); // (the same in every pass: the low end stays locked)
    // the second pass's stream is a different one, not the first's continued
    Rng rng (0x4D6F697374ull * (uint64_t)(uint32_t)seed + 0x5B1Dull * (uint64_t)(pass + 1) + 0x19ull);
    constexpr int kStepChoices[6] = {1, 2, 3, 4, 6, 8};
    constexpr int kLoopChoices[3] = {1, 2, 4};
    for (int b = kBandMid; b < kMaxBands; ++b)
    {
        BandMotion& m = p.band[b];
        m.steps = kStepChoices[rng.next () % 6];
        m.loop = kLoopChoices[rng.next () % 3];
        const int n = m.steps * m.loop; // (at most 32 bits)
        // the density: how many of the steps rise (a share of them, at least one)
        const uint64_t density = 22 + rng.next () % 45; // percent: 22 .. 66
        m.mask = 0;
        for (int i = 0; i < n; ++i)
            if (rng.next () % 100 < density)
                m.mask |= (uint64_t)1 << i;
        if (m.mask == 0)
            m.mask = (uint64_t)1 << (rng.next () % (uint64_t)n);
        m.offset = 0.25 * (double)(rng.next () % 4);
        m.hold = rng.range (0.2, 0.9);
        m.rise = logRange (rng, kRiseMin, kRiseMax);
        m.fall = logRange (rng, kFallMin, kFallMax);
    }
    for (int x = 1; x < kMaxXovers; ++x)
    {
        Channel& c = p.drift[x];
        c.rate = rng.range (0.3, 0.9);
        c.phase = rng.uniform ();
        c.rate2 = rng.range (1.7, 2.9);
        c.phase2 = rng.uniform ();
        c.blend = rng.range (0.2, 0.8);
        c.key = rng.next ();
    }
    return p;
}

} // namespace moistr

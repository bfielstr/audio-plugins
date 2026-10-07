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

bool BandMotion::risesOn (int64_t step, uint64_t bits) const
{
    const int64_t n = (int64_t)steps * loop;
    const int64_t i = ((step % n) + n) % n;
    return ((bits >> i) & 1u) != 0;
}

double BandMotion::lift (double theta, double secPerCycle, double riseSec, double fallSec, double density, uint64_t bits) const
{
    const double spc = std::max (secPerCycle, 1e-6), r = std::max (riseSec, 1e-6), f = std::max (fallSec, 1e-6);
    const double perCycle = (double)steps * density; // (x1: steps exactly)
    const double stepSec = spc / perCycle;
    const double upEnd = std::max (r, hold * stepSec); // (the rise always completes)
    const double u = theta * perCycle - offset;
    const int64_t top = (int64_t)std::floor (u); // the latest onset (dt >= 0)
    auto dtOf = [&] (int64_t j) { return (u - (double)j) * stepSec; }; // seconds since step j's onset
    // every event has the same shape in dt (seconds since its onset): rising until r, up until upEnd, then
    // falling (below 0 after upEnd + f). Over the onsets with dt <= upEnd the shape only rises with dt, so
    // the oldest of them is the highest; over the older ones it only falls, so the newest of them is. The
    // mask comes round every steps x loop steps (at least one bit set): each search ends within that.
    auto v = [&] (double dt) { return dt < r ? dt / r : (dt < upEnd ? 1.0 : 1.0 - (dt - upEnd) / f); };
    const int64_t n = (int64_t)steps * loop;
    int64_t mid = top - (int64_t)std::floor (upEnd / stepSec) - 1; // the oldest onset with dt <= upEnd
    while (mid <= top && dtOf (mid) > upEnd)
        ++mid;
    while (mid - 1 <= top && dtOf (mid - 1) <= upEnd)
        --mid;
    double best = 0.0;
    if (bits != 0)
    {
        for (int64_t j = mid; j <= top && j < mid + n; ++j)
            if (risesOn (j, bits))
            {
                best = std::max (best, v (dtOf (j)));
                break;
            }
        for (int64_t j = mid - 1; j >= mid - n; --j)
            if (risesOn (j, bits))
            {
                best = std::max (best, v (dtOf (j)));
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

Pattern makePattern (int seed, int pass, int lowXoverSeed)
{
    Pattern p;
    p.seed = seed;
    p.pass = pass;
    p.lowXover = lowXoverForSeed (lowXoverSeed > 0 ? lowXoverSeed : seed); // (the same in every pass)
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
    // the Low band's pushes and dips (0.22): a stream of their own, so the patterns above are 0.21's
    Rng low (0x4C6F7750ull * (uint64_t)(uint32_t)seed + 0x3A7Full * (uint64_t)(pass + 1) + 0x2Bull);
    {
        constexpr int kLowSteps[4] = {2, 4, 4, 8};
        constexpr int kLowLoops[2] = {1, 2};
        BandMotion& m = p.band[kBandLow];
        m.steps = kLowSteps[low.next () % 4];
        m.loop = kLowLoops[low.next () % 2];
        const int n = m.steps * m.loop; // (2 .. 16)
        const uint64_t density = 25 + low.next () % 31; // percent of the steps push: 25 .. 55
        const uint64_t dips = 20 + low.next () % 21;    // percent of the others dip: 20 .. 40
        m.mask = 0;
        p.lowDipMask = 0;
        for (int i = 0; i < n; ++i)
        {
            const uint64_t roll = low.next () % 100, roll2 = low.next () % 100;
            if (roll < density)
                m.mask |= (uint64_t)1 << i;
            else if (roll2 < dips)
                p.lowDipMask |= (uint64_t)1 << i;
        }
        if (m.mask == 0)
        {
            const int i = (int)(low.next () % (uint64_t)n);
            m.mask = (uint64_t)1 << i;
            p.lowDipMask &= ~m.mask;
        }
        if (p.lowDipMask == 0)
            for (int k = 0, i = (int)(low.next () % (uint64_t)n); k < n; ++k, i = (i + 1) % n)
                if (((m.mask >> i) & 1u) == 0)
                {
                    p.lowDipMask = (uint64_t)1 << i;
                    break;
                }
        m.offset = 0.25 * (double)(low.next () % 4);
        m.hold = low.range (0.15, 0.6);
        m.rise = logRange (low, kLowRiseMin, kLowRiseMax);
        m.fall = logRange (low, kLowFallMin, kLowFallMax);
    }
    return p;
}

} // namespace moistr

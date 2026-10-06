#include "Movement.h"

#include <cmath>

namespace moistr {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

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

} // namespace

double Channel::value (double theta) const
{
    const double x = rate * theta + phase;
    const double sines = 0.7 * std::sin (kTwoPi * x) + 0.3 * std::sin (kTwoPi * (rate2 * x + phase2));
    // (the random curve takes a new point every half cycle: it wanders about as fast as the sines)
    return (1.0 - blend) * sines + blend * smoothRandom (key, 2.0 * x);
}

Pattern makePattern (int seed, int pass)
{
    Pattern p;
    p.seed = seed;
    p.pass = pass;
    // the second pass's stream is a different one, not the first's continued
    Rng rng (0x4D6F697374ull * (uint64_t)(uint32_t)seed + 0x9E37ull * (uint64_t)(pass + 1));
    auto channel = [&rng] (double rateLo, double rateHi) {
        Channel c;
        c.rate = rng.range (rateLo, rateHi);
        c.phase = rng.uniform ();
        c.rate2 = rng.range (1.7, 2.9);
        c.phase2 = rng.uniform ();
        c.blend = rng.range (0.2, 0.8);
        c.key = rng.next ();
        return c;
    };
    for (int b = 0; b < kBands; ++b)
    {
        BandPattern& bp = p.band[b];
        bp.freq = channel (0.6, 1.5);
        bp.level = channel (0.5, 1.3);
        bp.depth = rng.range (0.75, 1.0);
        bp.offset = rng.range (-1.0, 1.0);
    }
    return p;
}

} // namespace moistr

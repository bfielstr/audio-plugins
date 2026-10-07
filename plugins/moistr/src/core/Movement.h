// Movement: what a Seed number stands for, and how the moving bands rise and fall.
//
// The bands are a crossover split (Engine.h): Low | Mid | High (3 bands) or Low | Mid | High | Air (4).
// The Low band's crossover (the Low crossover, 100 .. 500 Hz) is picked by the Seed alone and stays where
// it is (the same in both passes); its level moves only on its own events with Low Push / Low Dip (else it
// is locked). The other bands rise and fall.
//
// Each Seed feeds a deterministic random number generator (SplitMix64) that deals out a pattern for each
// pass (the second pass from a stream of its own). For every moving band:
//   a timing pattern  the movement's cycle (one cycle of Rate, or Sync Rate's beats) is cut into `steps`
//                     steps; a mask of steps x `loop` bits (the pattern repeats every `loop` cycles) says on
//                     which steps the band rises, `offset` shifts its steps a little off the grid
//   an event          on a step whose bit is set, the band rises from its floor (Level - Depth) to its Level
//                     over `rise` seconds, stays up for at least `hold` of a step, then falls back over `fall`
//                     seconds. Events overlap freely: the band follows whichever is highest.
//   rise, fall        seconds, before Rise and Fall scale them (rise 10 ms .. 1.5 s, fall 30 ms .. 3 s,
//                     both log-uniform); Speed divides them (down to kMinRampSec)
// Density (0.22) multiplies the steps per cycle (the mask then comes round Density times as often): x1 is
// the grid above exactly. Events overlap smoothly: a band follows whichever is highest, so a new rise
// during a fall takes over where it crosses it (the gain never jumps).
// The Low band (0.22) has events of its own too, from a third stream of the Seed (so the moving bands'
// patterns are those of 0.21): pushes (band[kBandLow].mask: up by Low Push) and dips (lowDipMask: down by
// Low Dip, never set on a push's step), on a grid and with times of their own. Its crossover never moves.
// and for the two upper crossovers a slow drift (a smooth curve, up to kXoverDriftOctaves each way at
// Movement 100 %). The same Seed always gives the same pattern (only integer arithmetic and exact
// conversions decide the choices; the times are worked out once from them).
//
// A band's lift (0: at its floor, 1: at its Level) is a pure function of the movement's phase theta (in
// cycles) and the cycle's length in seconds: the engine only has to say where on the timeline it is
// (Engine::setTransport) for the movement to repeat exactly, wherever playback starts.
#pragma once

#include <cstdint>

namespace moistr {

// (legacy) the 0.18 display's three bands (Low, Mid, High): the size of Meters::freq / level
constexpr int kBands = 3;
constexpr int kMaxBands = 4;  // Low, Mid, High, Air
constexpr int kMaxXovers = 3; // Low X (seeded, locked), Mid X, High X
constexpr int kMaxPasses = 2;
enum Band { kBandLow = 0, kBandMid, kBandHigh, kBandAir };

// A deterministic generator: SplitMix64.
struct Rng
{
    uint64_t s = 0;
    explicit Rng (uint64_t seed) : s (seed) {}
    uint64_t next ()
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // 0 .. 1 (53 bits: exact)
    double uniform () { return (double)(next () >> 11) * (1.0 / 9007199254740992.0); }
    double range (double lo, double hi) { return lo + (hi - lo) * uniform (); }
};

// One smoothly moving value: -1 .. 1 as a function of theta (the upper crossovers' drift).
struct Channel
{
    double rate = 1.0;    // cycles per cycle of the movement
    double phase = 0.0;   // 0 .. 1
    double rate2 = 2.0;   // the second sine's rate, relative to the first
    double phase2 = 0.0;  // 0 .. 1
    double blend = 0.5;   // 0: the sines only, 1: the smooth random curve only
    uint64_t key = 0;     // the random curve's points
    double value (double theta) const;
};

// A moving band's rises and falls.
struct BandMotion
{
    int steps = 4;      // steps per cycle
    int loop = 1;       // cycles before the pattern repeats
    uint64_t mask = 1;  // bit i: the band rises on step i of the loop (steps x loop bits, at least one set)
    double offset = 0;  // 0 .. 0.75: the steps' shift off the grid (in steps)
    double hold = 0.5;  // 0.2 .. 0.9: how long the band stays up at least (in steps, counted from its rise)
    double rise = 0.1;  // seconds (x Rise)
    double fall = 0.3;  // seconds (x Fall)
    bool risesOn (int64_t step) const { return risesOn (step, mask); }
    bool risesOn (int64_t step, uint64_t bits) const;
    // 0 (the floor) .. 1 (the Level), cosine-shaped, at phase theta (cycles) for a cycle of secPerCycle
    // seconds, with the rise and fall times given (seconds: rise x Rise / Speed, fall x Fall / Speed), the
    // steps per cycle x density, on the steps of `bits` (the mask, or the Low band's dips)
    double lift (double theta, double secPerCycle, double riseSec, double fallSec) const
    {
        return lift (theta, secPerCycle, riseSec, fallSec, 1.0, mask);
    }
    double lift (double theta, double secPerCycle, double riseSec, double fallSec, double density, uint64_t bits) const;
};

struct Pattern
{
    int seed = 1, pass = 0;
    double lowXover = 270.0;           // Hz: the Low band's crossover (the same in every pass)
    BandMotion band[kMaxBands];        // [kBandLow]: the Low band's pushes (its mask) and timing (0.22)
    Channel drift[kMaxXovers];         // [0] unused (the Low crossover does not move)
    uint64_t lowDipMask = 0;           // the Low band's dips (on band[kBandLow]'s grid; none on a push's step)
};

// The Low crossover a Seed picks (Hz, kLowXoverMin .. kLowXoverMax on a log scale): the fractional part of
// seed x the golden ratio (in 64-bit fixed point), so the 128 seeds spread evenly over the range.
double lowXoverForSeed (int seed);

// The pattern of a pass (0: the first, 1: the second, from a stream of its own) for a Seed. lowXoverSeed
// (> 0): the Low crossover from that Seed instead (Seed B's pattern keeps Seed's Low crossover).
Pattern makePattern (int seed, int pass, int lowXoverSeed = 0);

// Two patterns' lifts overlapping (Seed Blend b): Seed's alone at 0, Seed B's alone at 1, and in between a
// soft maximum, mix + 4 b (1 - b) (max - mix) with mix = (1 - b) a + b c: at 0.5 a band is up whenever
// either pattern has it up.
inline double blendLifts (double a, double c, double b)
{
    if (b <= 0.0)
        return a;
    if (b >= 1.0)
        return c;
    const double mix = (1.0 - b) * a + b * c, hi = a > c ? a : c;
    return mix + 4.0 * b * (1.0 - b) * (hi - mix);
}

// At Movement 100 %: how far the upper crossovers drift (octaves, each way).
constexpr double kXoverDriftOctaves = 1.0 / 3.0;
// the seeded times' ranges (seconds, before Rise / Fall)
constexpr double kRiseMin = 0.010, kRiseMax = 1.5, kFallMin = 0.030, kFallMax = 3.0;
// the Low band's seeded times (seconds, before Rise / Fall): quicker, punchier pushes
constexpr double kLowRiseMin = 0.005, kLowRiseMax = 0.25, kLowFallMin = 0.04, kLowFallMax = 1.2;
// the quickest a rise or fall can be (seconds, with Speed): still a smooth raised-cosine ramp
constexpr double kMinRampSec = 0.001;

} // namespace moistr

// Movement: what a Seed number stands for, and the slow drift of each band's frequency and level.
//
// Each Seed feeds a deterministic random number generator (SplitMix64) that deals out a pattern for each
// pass: for every band, a frequency channel and a level channel, each with its own rate (a multiple of
// Rate), start phases, a blend between two sines and a smooth random curve, a depth (0.75 .. 1 of the
// band's share) and a small offset of the band's frequency (so the bands' ratios shift a little as they
// move). The same Seed always gives the same pattern, on every machine (only integer arithmetic and
// exact conversions decide it).
//
// A channel's value is a pure function of the movement's phase theta (in cycles of Rate): the engine
// only has to say where on the timeline it is (Engine::setTransport) for the movement to repeat exactly.
#pragma once

#include <cstdint>

namespace moistr {

constexpr int kBands = 3;  // Low, Mid, High
constexpr int kMaxPasses = 2;

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

// One moving value: -1 .. 1 as a function of theta.
struct Channel
{
    double rate = 1.0;    // cycles per cycle of Rate
    double phase = 0.0;   // 0 .. 1
    double rate2 = 2.0;   // the second sine's rate, relative to the first
    double phase2 = 0.0;  // 0 .. 1
    double blend = 0.5;   // 0: the sines only, 1: the smooth random curve only
    uint64_t key = 0;     // the random curve's points
    double value (double theta) const;
};

struct BandPattern
{
    Channel freq, level;
    double depth = 1.0;  // 0.75 .. 1: this band's share of its Move
    double offset = 0.0; // -1 .. 1: the frequency's offset (kOffsetOctaves at full movement)
};

struct Pattern
{
    int seed = 1, pass = 0;
    BandPattern band[kBands];
};

// The pattern of a pass (0: the first, 1: the second, from a stream of its own) for a Seed.
Pattern makePattern (int seed, int pass);

// At Movement 100 % and a band's Move 100 %: how far its frequency swings (each way) and is offset.
constexpr double kSwingOctaves = 1.0;
constexpr double kOffsetOctaves = 0.15;

} // namespace moistr

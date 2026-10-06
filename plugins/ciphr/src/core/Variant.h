// Variant: what a Variant number stands for. Each number seeds a deterministic random number generator
// (SplitMix64) that fills in the oscillators' lists of (wave, pitch offset) entries, their start phases
// and the processor's tap pattern. The same number always gives the same patch, on every machine (only
// integer arithmetic and exact conversions decide it).
#pragma once

#include <cstdint>

namespace ciphr {

// Oscillators per voice: a compile-time cap so 8 voices fit the suite's CPU budget (README, "CPU").
constexpr int kOscs = 6;
// Entries in each oscillator's list (Timbre scans them).
constexpr int kEntries = 4;
// Taps of the processor's delay line.
constexpr int kTaps = 8;
// Voices.
constexpr int kVoices = 8;

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
    int below (int n) { return (int)(next () % (uint64_t)n); }
};

struct Entry
{
    int wave = 0;      // Wave (Wavetable.h)
    double semis = 0;  // pitch offset from the note, semitones (cents included)
};

struct Tap
{
    double time = 1.0;    // a fraction of Length (tap 0 is always 1: the longest, also the feedback's)
    double gain = 1.0;    // 0 .. 1
    double pan = 0.0;     // -1 .. 1 (balance)
    double lfoRate = 0.1; // Hz at Movement 50 %
    double lfoPhase = 0;  // 0 .. 1
};

struct Patch
{
    int variant = 1;
    Entry osc[kOscs][kEntries];
    double phase0[kOscs] {}; // each oscillator's start phase (0 .. 1) for a fresh voice
    Tap taps[kTaps];
};

Patch makePatch (int variant);

} // namespace ciphr

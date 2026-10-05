// Orbitr: Detonatr's Motion stage on its own, a Doppler swarm in the spirit of Tonsturm's
// SpinTracer (not affiliated), with its "Liquid Debris"-like setting as the defaults.
//
//   input -> Motion (the orbs moving round the listener, each heard through its own delay line, playing
//   the input or, with Grains, their grains of it; Mix
//   against the input, delayed as much) -> Dry/Wet with the input (delayed as much) -> Output ->
//   Smacheratr (the optional saturator at the end of every plug-in)
//
// The latency is constant: Motion's 10 ms plus the end saturator's (always in the path).
#pragma once

#include "Dsp.h"
#include "Motion.h"
#include "Params.h"

#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>

namespace orbitr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// What the editor shows (written by the audio thread once per block).
struct Meters
{
    std::atomic<uint32_t> blocks {0}; // counts the blocks, so the display knows when there is news
    // the orbs (x right, y ahead, metres), how many, and the centre's distance, angle (degrees) and the
    // radius now
    std::atomic<int> orbs {0};
    std::array<std::atomic<float>, Motion::kMaxOrbs> orbX {}, orbY {};
    std::atomic<float> distance {3.0f}, radius {2.0f}, angle {0.0f};
    // Grains: whether the orbs play grains, and each orb's newest grain's window now (0 .. 1)
    std::atomic<bool> grains {false};
    std::array<std::atomic<float>, Motion::kMaxOrbs> orbGrain {};
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return motion.latency () + tail.latency (); }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    const Motion& motionStage () const { return motion; } // for the tests

private:
    void applyMotion (uint32_t id, double v);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    Motion motion;
    dsp::Delay dry; // the input, lined up with the effect for Dry/Wet
    float wet = 1.0f, out = 1.0f, smooth = 0.0f;
    Meters* meters = nullptr;
    smacheratr::Tail tail;
};

} // namespace orbitr

// Orbitr's effect (Detonatr's Motion stage): a Doppler swarm in the spirit of Tonsturm's SpinTracer
// (not affiliated). The input (both channels summed) is played by Orbs virtual sources (1 .. 16) that
// move around a listener; each reaches the listener's ears through its own variable delay line, so its
// pitch shifts with its radial speed (true Doppler) and its level with its distance.
//
// Geometry (metres; x right, y ahead, z up; the listener's head at the origin, its ears 8.75 cm to
// either side, 1.7 m over the floor): the orbs move around a centre Distance ahead of the listener,
//   Orbit  on circles of Radius round the centre, spread evenly, at Speed (m/s along the circle);
//          Randomness tilts each orbit, shrinks it (up to 30 %), varies its speed (up to 35 %) and
//          its place on the circle.
//   Swarm  each on its own smooth path inside a ball of Radius round the centre (on each axis a sum
//          of two sines at their own rates; flatter in height), at Speed (its RMS speed); Randomness
//          spreads the rates (0: every orb at the same rates, on its own phases).
// Each ear hears each orb delayed by the time sound takes from where the orb was when it sent it:
// the delay d solves d = |orb (t - d) - ear| / c (c = 343 m/s, three fixed-point steps, at every 16th
// sample, the delay ramped in between), so the pitch shift is the moving-source Doppler shift
// f c / (c + v_r). The delay is read with 4-point Hermite interpolation. Its level: Distance /
// distance (at most +12 dB), then equal-power panned by the orb's direction (Spread: 0 centred,
// 1 full width; Spread also scales the ears' spacing, so it sets the time difference between the
// ears too). Floor adds each orb's reflection off the floor (an image source under it, 0.4 x).
// The orbs are summed / sqrt (Orbs) and mixed with the input (Mix).
//
// Latency: the delays are taken relative to the centre (the delay of a source at the centre is
// 10 ms): a fixed 10 ms, the effect's latency; the input in the mix is delayed as much.
//
// Grains (off by default; off, the orbs play the input as above, sample for sample as before Grains):
// each orb plays a stream of grains instead of the input, and its own delay lines carry that stream,
// so its Doppler, pan, level and floor reflection apply to the grains as to the input. A grain is
// Grain Size long, Hann windowed, read from the recent input (up to 1.25 s back) at its own speed
// (Grain Pitch: 2^(st / 12)). Orb k's slice of the input starts k / Orbs x 0.5 s back (the orbs
// spread over the last half second), each grain Scatter x 0.5 s further back at random, and its
// start in time jumps by up to Scatter / 2 of the gap between grains. An orb starts a grain every
// Grain Size / Density samples (Density 2: each grain overlaps the next by half, and with no
// Scatter and Pitch the windows sum to 1: the orb plays its slice untouched); its grains are scaled
// by sqrt (2 / Density) above Density 2. Switching Grains crossfades over 30 ms.
#pragma once

#include "Dsp.h"

#include <array>
#include <vector>

namespace orbitr {

class Motion
{
public:
    static constexpr int kMaxOrbs = 16;
    static constexpr double kSoundSpeed = 343.0, kBaseMs = 10.0, kEarHalf = 0.0875, kEarHeight = 1.7, kFloorGain = 0.4;
    static constexpr double kMaxRadius = 3.0;
    static constexpr int kStep = 16; // the delays are worked out at every 16th sample
    enum Pattern { kOrbit = 0, kSwarm = 1 };

    void prepare (double sampleRate, int maxBlock);
    void reset ();
    int latency () const { return baseDelay; }

    void setOrbs (int n);
    void setPattern (int p);
    void setSpeed (double metresPerSecond);
    void setDistance (double m) { distance = std::clamp (m, 0.5, 50.0); }
    void setRadius (double m) { radius = std::clamp (m, 0.05, kMaxRadius); }
    void setSpread (double s) { spread = std::clamp (s, 0.0, 1.0); }
    void setRandomness (double r);
    void setFloor (bool on) { floor = on; }
    void setMix (double m) { mix = (float)std::clamp (m, 0.0, 1.0); }
    // Grains
    static constexpr int kMaxGrains = 12;                                  // per orb (Density 8 needs 8, Scatter's jitter a few more)
    static constexpr double kSliceSpan = 0.5, kScatterSpan = 0.5;          // s: the orbs' slices' spread, Scatter's reach
    static constexpr double kMinGrainMs = 10.0, kMaxGrainMs = 500.0, kMaxDensity = 8.0, kMaxPitch = 12.0;
    void setGrains (bool on) { grains = on; }
    void setGrainSize (double ms) { grainMs = std::clamp (ms, kMinGrainMs, kMaxGrainMs); }
    void setGrainDensity (double x) { density = std::clamp (x, 0.05, kMaxDensity); }
    void setGrainScatter (double s) { scatter = std::clamp (s, 0.0, 1.0); }
    void setGrainPitch (double st) { grainPitch = std::clamp (st, -kMaxPitch, kMaxPitch); }

    void process (float* l, float* r, int n);

    struct Vec
    {
        double x = 0, y = 0, z = 0;
    };
    int orbs () const { return numOrbs; }
    // where orb k is now (for the display, and the tests)
    Vec orbPosition (int k) const { return position (k, 0.0); }
    double currentDistance () const { return dSm; }
    double currentRadius () const { return rSm; }
    bool grainsOn () const { return grainPath; }
    // orb k's newest grain's window now (0 .. 1; 0 without Grains), for the display
    float grainLevel (int k) const;

private:
    struct Orb
    {
        // fixed per orb (from its seed): Orbit's tilt, size, speed and place; Swarm's rates and weights
        double tiltU = 0, sizeU = 0, speedU = 0, placeU = 0;
        double rateU[3][2] {};
        double weight[2] {1.0, 0.5};
        // the motion: Orbit's angle, Swarm's six phases, and their rates (radians per second)
        double angle = 0, phase[3][2] {};
        double angleRate = 0, phaseRate[3][2] {};
    };
    struct Tap // one ear's view of one orb (or of its reflection)
    {
        float delay = 0, delayStep = 0, gain = 0, gainStep = 0;
    };
    Vec position (int k, double ago) const; // where orb k was `ago` seconds back
    void updateRates ();
    void retarget (); // the taps' targets for the next kStep samples
    void processPlain (float* l, float* r, int n);  // the orbs play the input
    void processGrains (float* l, float* r, int n); // the orbs play their grains (and while Grains fades in or out)
    void startGrains ();                            // Grains switched on: the orbs' lines take over from the input's
    void startGrain (int k);
    float cloud (int k); // orb k's grains' next sample
    double sr = 48000.0;
    int baseDelay = 480, numOrbs = 6, pattern = kSwarm;
    double speed = 18.0, distance = 3.0, radius = 2.0, spread = 0.8, randomness = 0.6;
    double dSm = 3.0, rSm = 2.0, smooth = 0.01;
    bool floor = true, started = false;
    float mix = 0.5f;
    std::array<Orb, kMaxOrbs> orbList {};
    std::array<std::array<Tap, 4>, kMaxOrbs> taps {}; // left, right, left floor, right floor
    std::vector<float> buf; // the input, mono
    int mask = 0, writePos = 0, stepLeft = 0;
    std::vector<float> hist; // the input again, on a longer line: what the grains read
    int histMask = 0, histPos = 0;
    dsp::Delay dry;
    // Grains
    struct Grain
    {
        double back = 0.0;  // samples behind the write position
        double phase = 0.0; // 0 .. 1 through the window
        double phaseStep = 0.0;
        double drift = 0.0; // 1 - the grain's speed: back's change per sample
        float amp = 0.0f;
        bool on = false;
    };
    struct GrainOrb
    {
        std::array<Grain, kMaxGrains> g {};
        double wait = 0.0; // samples to its next grain
        uint32_t rng = 1;
        int newest = -1;
    };
    bool grains = false, grainPath = false;
    double grainMs = 80.0, density = 2.0, scatter = 0.3, grainPitch = 0.0;
    float gMix = 0.0f, gMixStep = 0.001f; // the grains against the input in the orbs' lines (the 30 ms crossfade)
    int quiet = 0;                         // samples since the crossfade reached the input (Grains off)
    double histMax = 0.0;                  // samples: the furthest back a grain may read
    std::array<GrainOrb, kMaxOrbs> grainOrbs {};
    std::vector<float> orbBuf; // each orb's line: kMaxOrbs x (mask + 1), what its taps read with Grains
};

} // namespace orbitr

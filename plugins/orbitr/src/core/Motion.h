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
    dsp::Delay dry;
};

} // namespace orbitr

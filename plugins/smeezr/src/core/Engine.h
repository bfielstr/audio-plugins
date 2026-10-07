// Smeezr: a one-knob compressor. Squeeze at 0 leaves the sound untouched; turning it up first squeezes
// the spectrum towards the balance of pink noise, and past the middle adds an OTT-style boost on top
// (upward and downward multiband compression), loudest and most squashed at 100 %.
//
//   input -> 10 bands (Linkwitz-Riley 4, one octave each, 20 Hz .. 20 kHz)
//         -> per band: pink gain (towards an equal share of the power per octave) + OTT gain (its band group)
//         -> bands summed (phase aligned) -> Mix (dry / wet) -> Output -> Smacheratr (the end saturator)
//
// Pink stage. Every band's short-term level is measured (RMS: 250 ms in the lowest band down to 60 ms in
// the highest at Fast, four times as long at Slow) on both channels together (the mean square of left and
// right), so both channels get the same gain and the stereo image stays as it is. Pink noise has the same
// power in every octave, so with one-octave bands the target for each band is an equal share of the total
// power. A band's gain is (target - level) x the pink amount, at most 15 dB of cut or boost (less boost at
// the ends: 6 dB in the lowest band, 20 .. 40 Hz, against rumble, 12 dB in the highest, 10 .. 20 kHz,
// against hiss). A band more than 30 dB below the loudest band gets less boost, and 40 dB below none at all
// (nor does it count in the share), so silence and near-silent bands are never pulled up; below -80 dBFS in
// total every gain glides back to 0 dB. All the gains are then shifted together so the total power stays
// the same (the loudness is kept), and glide (cuts faster than boosts, the lows slower than the highs).
//
// OTT stage. The bands are grouped as OTT's three (below 80 Hz, 80 Hz .. 2.5 kHz, above 2.5 kHz). Each
// group's power after the pink gains goes through Multidyn's OTT model (multidyn/src/core/Ott.h: David
// Braun's fit of Xfer Records' OTT, the Faust libraries' co.xfer_ott, MIT) at Time 100 %: envelope, upward
// and downward curves and OTT's own makeup, at the depth the knob gives.
//
// The knob: 0 .. 50 % sets the pink amount (0 .. 1), 50 .. 100 % the OTT depth (0 .. 1) while the pink
// amount stays at 1, both on an S-curve (smoothstep: half way at 25 % and 75 %). Both curves are flat at
// 50 %: no jump or kink there. The knob, Mix
// and Output glide (no zipper noise). At 0 % (once it has glided there) the input passes bit for bit: the
// processed path fades in over 20 ms as the knob leaves 0.
//
// The bands sum to an all-pass of the input (flat in level), so Mix blends with the same all-passed dry
// signal (no comb filtering). No latency of its own: the latency is the end saturator's (always in the
// path).
#pragma once

#include "Dsp.h"
#include "Params.h"

#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>

namespace smeezr {

constexpr int kBands = 10;
constexpr int kXovers = kBands - 1;
constexpr int kGroups = 3; // OTT's bands

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// What the editor shows (written by the audio thread once per block).
struct Meters
{
    std::atomic<uint32_t> blocks {0};
    std::atomic<bool> active {false}; // input heard in the last half second
    std::atomic<float> pink {0.0f}, ott {0.0f}; // the pink amount and the OTT depth now (0 .. 1)
    std::atomic<float> targetDb {-120.0f};      // the pink target: each band's share of the total (dB)
    std::array<std::atomic<float>, kBands> levelDb {}; // each band's measured level (dB)
    std::array<std::atomic<float>, kBands> pinkDb {};  // the pink stage's gain (dB)
    std::array<std::atomic<float>, kBands> ottDb {};   // the OTT stage's gain (dB, its group's)
};

class Engine
{
public:
    static constexpr int kTick = 16; // the detectors and the gains are worked out every kTick samples

    Engine ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return tail.latency (); }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // In place is fine.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // the knob's two stages: the pink amount and the OTT depth at Squeeze u (0 .. 1)
    static double pinkAmount (double u);
    static double ottDepth (double u);
    // the crossovers (Hz), log-spaced so every band is one octave of 20 Hz .. 20 kHz
    static double crossover (int i);
    // a band's OTT group (0 low, 1 mid, 2 high)
    static int group (int band) { return band <= 1 ? 0 : (band >= kBands - 2 ? 2 : 1); }
    // the largest boost the pink stage gives a band (dB, at full pink amount)
    static double maxBoostDb (int band) { return band == 0 ? 6.0 : (band == kBands - 1 ? 12.0 : 15.0); }
    static constexpr double kMaxCutDb = 15.0;

    // for the tests and the display
    double bandLevelDb (int b) const;                       // the pink detector's level
    double pinkGainDb (int b) const { return gPink[b]; }    // now
    double ottGainDb (int g) const { return gOtt[g]; }      // now
    double targetDb () const { return target; }
    double squeezeNow () const { return knob; }
    bool bypassed () const { return engage <= 0.0; }

private:
    void setTimes ();
    void tickGains (const double* ms, int m, double* gainOut);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    dsp::SvfCoefs xc[kXovers], detLoC, detHiC;
    dsp::Lr4 split[kXovers][2];
    dsp::Allpass ap[kXovers][2];
    dsp::Svf detLo[2], detHi[2]; // the lowest band's detector high-passed at 20 Hz, the highest's low-passed at 20 kHz
    // pink stage
    double msAt[kBands][kBands] {}, msSlow[kBands] {}, gPink[kBands] {};
    double cRms[kBands] {}, cAtk[kBands] {}, cRel[kBands] {}; // per full tick
    double rmsSec[kBands] {}, atkSec[kBands] {}, relSec[kBands] {};
    double target = -120.0;
    // OTT stage
    double env[kGroups] {}, gOtt[kGroups] {};
    double cOttAtk[kGroups] {}, cOttRel[kGroups] {};
    // the gains applied at the end of the last tick (linear, Mix included)
    double gNow[kBands] {};
    // smoothed controls
    double knob = 0.0, knobSmooth = 0.1, mixNow = 1.0, engage = 0.0, engageStep = 0.05;
    float out = 1.0f, smooth = 0.001f;
    int quiet = 0; // samples since the input was last heard
    Meters* meters = nullptr;
    smacheratr::Tail tail;
};

} // namespace smeezr

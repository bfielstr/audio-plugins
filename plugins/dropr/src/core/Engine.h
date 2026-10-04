// Dropr: a multiband compressor in the style of Minimal Audio's Fuse Compressor, feeding a saturator.
//
//   in -> Input gain -> 6-band Linkwitz-Riley tree -> per band: detector -> gain law (Gain.h: downward,
//   normal or negative ratio, upward) -> attack / release -> the band's gain (its point in the display,
//   + Tilt + Makeup) -> sum -> Dry/Wet (the dry signal is the input before the Input gain) -> Output
//   -> Smacheratr (the saturator at the end of every plug-in; on and driven by default)
//
// Crossovers: Linkwitz-Riley 24 dB/oct splits (multidyn/src/core/Crossover.h), split[j] separating band
// j from everything above it; each lower band goes through the all-passes of the crossovers above it,
// so the bands sum to the all-pass AP0 .. AP4 of the input: flat in level. The dry signal goes through
// the same all-passes, so any Dry/Wet lines up in phase. The tree always runs all five crossovers:
// with fewer Bands the top band is the sum of the bands above crossover Bands - 1 (one detector, one
// gain), so changing Bands only regroups the bands' gains, which are crossfaded over 20 ms. Crossover
// moves are smoothed (about 20 ms) and the filters retuned every 32 samples.
// Detector: per band and channel, the band's peak, held for one period of the band's lowest
// frequency (its lower crossover, 25 Hz for the lowest band) and then falling with a time constant of
// a quarter of that, so a steady tone in the band reads its peak level without ripple.
// Channel Link: each channel's level moves towards the louder channel's by the Link (in dB);
// 100 %: both channels get the same gain. In Mid-Side mode the two channels are mid and side (the bands
// are split in left / right and turned into mid / side after the split; a Mode change dips the output
// for 5 ms + 5 ms around the switch).
// Attack / Release: the band's gain (dB) moves towards the gain law's target with one-pole time
// constants: Attack while the gain goes down (more reduction or less upward lift), Release while it
// comes back up: it gets 63 % of the way in that time.
// Adaptive Time A: the larger the move the gain has to make, the faster both times: they are divided by
//   1 + 3 A min (1, |target - gain| / 24 dB)
// so at 100 % a move of 24 dB or more runs 4 times as fast, at 50 % 2.5 times; small moves keep the
// set times (smooth on steady material, quick on the jumps of a hit). 0 %: always the set times.
// Latency: the end saturator's only (the crossovers are minimum-phase, there is no look-ahead); it
// never changes. No allocation in process ().
#pragma once

#include "Gain.h"
#include "Params.h"

#include "multidyn/src/core/Crossover.h"
#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>

namespace dropr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

constexpr double kLowestBandHz = 25.0; // the lowest band's "lower edge" for its detector's hold
constexpr double kBandsFadeMs = 20.0, kModeDuckMs = 5.0, kXoverSmoothMs = 20.0;

// For the editor (written by the audio thread once per block).
struct Meters
{
    std::atomic<int> bands {kMaxBands};
    std::atomic<float> levelDb[kMaxBands];  // each band's detector level (after the Input gain), the block's peak
    std::atomic<float> gainDb[kMaxBands];   // the gain law's gain (attack / release applied), the block's lowest
    std::atomic<float> outDb[kMaxBands];    // the band's level after its gain and makeup, the block's peak
    Meters ()
    {
        for (int k = 0; k < kMaxBands; ++k)
        {
            levelDb[k].store (-150.0f);
            gainDb[k].store (0.0f);
            outDb[k].store (-150.0f);
        }
    }
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain)
    {
        p[id] = plain;
        if (id >= kTailExt4Base)
            tail.setParam (smacheratr::kTailExt4First + (id - kTailExt4Base), plain);
        else if (id >= kTailExt3Base)
            tail.setParam (smacheratr::kTailExt3First + (id - kTailExt3Base), plain);
        else if (id >= kTailExt2Base)
            tail.setParam (pk::kTailFields + pk::kTailExtFields + (id - kTailExt2Base), plain);
        else if (id >= kTailExtBase)
            tail.setParam (pk::kTailFields + (id - kTailExtBase), plain);
        else if (id >= kTailBase)
            tail.setParam (id - kTailBase, plain);
    }
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return tail.latency (); }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // the crossovers in use (Hz)
    double xover (int j) const { return xf[j]; }

private:
    static constexpr int kChunk = 32;
    void retune ();
    void processChunk (const float* inL, const float* inR, float* outL, float* outR, int n);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    // the tree: split[j] separates band j from the rest; ap[j][k] (k > j): band j through crossover k's all-pass
    multidyn::Lr4Split split[kNumXovers];
    multidyn::Allpass2 ap[kNumXovers][kNumXovers];
    multidyn::Allpass2 dryAp[kNumXovers];
    double xf[kNumXovers] {};
    bool xfInit = false;
    double xfCoef = 0.0;
    struct Band
    {
        float env[2] {};       // the held peak (linear)
        int hold[2] {};        // samples left to hold it
        float gr[2] {};        // the gain law's gain with attack / release (dB)
        float mk = 0.0f;       // the band's gain + Tilt + Makeup (dB), smoothed
        int holdLen = 48;      // one period of the band's lowest frequency
        float fall = 0.99f;    // the peak's fall after the hold, per sample
        float mLevel = -150.0f, mGain = 0.0f, mOut = -150.0f; // this block's meter values
    };
    Band band[kMaxBands];
    int nb = kMaxBands;           // bands in use
    float fadeFrom[kMaxBands][2] {}; // a Bands change: the linear gains each split band had (fading out)
    int fadePos = 0, fadeLen = 1;    // samples into the fade (fadeLen: done)
    float lastG[kMaxBands][2] {};    // the gain each split band got last sample
    int mode = kModeStereo, modeTarget = kModeStereo;
    float duck = 1.0f, duckStep = 0.01f;
    float gin = 1.0f, gout = 1.0f, mix = 1.0f, smooth = 0.0f, mkSmooth = 0.0f;
    float atkC = 0.1f, relC = 0.001f, atkL = 0.0f, relL = 0.0f; // one-pole coefficients and log2 (1 - c)
    Meters* meters = nullptr;
    smacheratr::Tail tail;
};

} // namespace dropr

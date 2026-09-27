// Multiband upward/downward compressor-expander in the style of Live's Multiband Dynamics.
//
// Per band, the detector level x (dB) drives two gain computers:
//   above threshold Ta, ratio Ra:  gain = (x - Ta) * (1/Ra - 1)   for x > Ta
//        Ra > 1 = downward compression, Ra < 1 = upward expansion
//   below threshold Tb, ratio Rb:  gain = (Tb - x) * (1 - 1/Rb)   for x < Tb
//        Rb > 1 = upward compression,   Rb < 1 = downward expansion
// Ratios read Live-style as "1 : R": R > 1 always compresses (reduces dynamic range).
// Each computer is driven by its own level envelope: Above uses Attack when the level rises and
// Release when it falls, Below the other way round.
#pragma once

#include "Crossover.h"
#include "Params.h"

#include <array>

namespace multidyn {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// Static gain curve (dB), shared with the editor's display.
double aboveGainDb (double levelDb, double thresh, double ratio, bool softKnee);
double belowGainDb (double levelDb, double thresh, double ratio, bool softKnee);

struct BandMeter
{
    float inputDb = -100.0f;  // band level before dynamics (after band input gain)
    float outputDb = -100.0f; // band level after dynamics and band output gain
    float gainDb = 0.0f;      // current dynamic gain change
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain) { p[id] = plain; }
    double param (uint32_t id) const { return p[id]; }

    // In-place capable. sc may be null (no side-chain connected). All buffers are n samples.
    void process (const float* inL, const float* inR, const float* scL, const float* scR, float* outL, float* outR,
                  int n);

    const BandMeter& meter (int band) const { return meters[band]; }
    bool bandUsed (int band) const { return band < bandCount (); }
    int bandCount () const;

private:
    struct BandState
    {
        float aboveDb = 0.0f, belowDb = 0.0f;       // current gain changes (dB)
        float envAbove = -120.0f, envBelow = -120.0f; // level envelopes (dB)
        float rms = 0.0f, peak = 0.0f;
        float inGain = 1.0f, outGain = 1.0f;  // smoothed linear gains
        float meterIn = 0.0f, meterOut = 0.0f;
    };
    void updateFilters (bool force);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    // Crossover tree: split[j] separates band j from everything above it. Lower bands are
    // passed through allpasses at every higher crossover so the bands sum to an allpass.
    Lr4Split split[kMaxBands - 1], scSplit[kMaxBands - 1];
    Allpass2 ap[kMaxBands - 1][kMaxBands - 1], scAp[kMaxBands - 1][kMaxBands - 1];
    float xf[kMaxBands - 1] {};
    void splitBands (float x, int c, int n, Lr4Split* sp, Allpass2 (*aps)[kMaxBands - 1], float* out);
    BandState bands[kNumBands];
    BandMeter meters[kNumBands];
    float outGain = 1.0f, scGain = 1.0f;
    float rmsCoef = 0.0f, peakCoef = 0.0f, meterFall = 0.0f, smooth = 0.0f;
};

} // namespace multidyn

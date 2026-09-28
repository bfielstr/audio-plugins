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
//
// Modes: Base is the plain device. Character detects more slowly (a 50 ms RMS window, a
// two-stage envelope), has a wider knee and a release that slows down the deeper the gain change,
// so it moves like a character compressor rather than grabbing peaks.
// Pre-Limit: a 1 ms look-ahead limiter on each band's driven input with its ceiling relative to
// the band's Above threshold, so a transient pushed hard into the thresholds is held where the
// compressor will settle anyway instead of passing through at full level until the attack
// catches up (and being squared by whatever follows). Every band
// runs through the look-ahead delay whether the limiter is on or not, so the latency (1 ms) never
// changes.
// Saturator: a built-in Smacheratr after the Output gain (the usual chain), always in the path
// with its dry/wet at zero when off, so its oversampling latency is constant too.
#pragma once

#include "Crossover.h"
#include "Params.h"

#include "smacheratr/src/core/Engine.h" // the built-in saturator

#include <array>

namespace multidyn {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

constexpr double kKneeDb = 6.0, kCharacterKneeDb = 12.0;

// Static gain curve (dB), shared with the editor's display.
double aboveGainDb (double levelDb, double thresh, double ratio, bool softKnee, double kneeDb = kKneeDb);
double belowGainDb (double levelDb, double thresh, double ratio, bool softKnee, double kneeDb = kKneeDb);

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
    int latency () const { return look + sat.latency (); } // look-ahead + saturator, constant for a sample rate

    // In-place capable. sc may be null (no side-chain connected). All buffers are n samples.
    void process (const float* inL, const float* inR, const float* scL, const float* scR, float* outL, float* outR,
                  int n);

    const BandMeter& meter (int band) const { return meters[band]; }
    bool bandUsed (int band) const { return band < bandCount (); }
    int bandCount () const;

private:
    static constexpr int kMaxLookahead = 256; // 1 ms up to 256 kHz
    struct BandState
    {
        float aboveDb = 0.0f, belowDb = 0.0f;         // current gain changes (dB)
        float envAbove = -120.0f, envBelow = -120.0f; // level envelopes (dB)
        float envAbove2 = -120.0f, envBelow2 = -120.0f; // second stage (Character)
        float rms = 0.0f, peak = 0.0f;
        float inGain = 1.0f, outGain = 1.0f; // smoothed linear gains
        float meterIn = 0.0f, meterOut = 0.0f;
        float limGain = 1.0f, limPeak = 0.0f; // pre-limiter gain and its held input peak
        float delay[2][kMaxLookahead] {};    // look-ahead delay
        int delayPos = 0;
    };
    void updateFilters (bool force);
    void syncSaturator (); // pushes the saturator parameters into its engine

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int look = 48;
    // Crossover tree: split[j] separates band j from everything above it. Lower bands are
    // passed through allpasses at every higher crossover so the bands sum to an allpass.
    Lr4Split split[kMaxBands - 1], scSplit[kMaxBands - 1];
    Allpass2 ap[kMaxBands - 1][kMaxBands - 1], scAp[kMaxBands - 1][kMaxBands - 1];
    float xf[kMaxBands - 1] {};
    void splitBands (float x, int c, int n, Lr4Split* sp, Allpass2 (*aps)[kMaxBands - 1], float* out);
    BandState bands[kNumBands];
    BandMeter meters[kNumBands];
    float outGain = 1.0f, scGain = 1.0f;
    float rmsCoef = 0.0f, rmsCoefC = 0.0f, peakCoef = 0.0f, peakCoefC = 0.0f;
    float limAtk = 0.0f, limRel = 0.0f, limPeakDecay = 0.0f, meterFall = 0.0f, smooth = 0.0f;
    smacheratr::Engine sat;
};

} // namespace multidyn

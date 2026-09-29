// Levlr: the spectrum in four bands that touch (band k's top edge is band k+1's bottom edge), each
// with its own level, then Output and the Smacheratr at the end of the chain.
// The bands come from a tree of Linkwitz-Riley splits (Crossover.h), minimum phase: split at
// crossover 1 into band 1 and the rest, the rest at crossover 2, and so on; bands 1 and 2 then go
// through the all-passes of the crossovers above them, so with every band at 0 dB the four add up to
// an all-pass of the input. Its level is flat and its phase turns around each crossover: the sound of
// the crossovers, on purpose (moving a band's level is then a shelf-like step between two edges).
#pragma once

#include "Crossover.h"
#include "Params.h"

#include "pluginkit/ScopeBuffer.h"
#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>
#include <cmath>
#include <complex>

namespace levlr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// The crossovers as the engine uses them: in order, at least kMinGapOct apart, inside 20 Hz .. 20 kHz
// and under 0.45 of the sample rate (a lower crossover pushes the ones above it up).
void effectiveCrossovers (const double set[kCrossovers], double sampleRate, double out[kCrossovers]);

// Each band's gain as the engine applies it (linear): its level, or 0 when it is muted or another
// band is soloed. A soloed band is heard even when muted.
template <typename Get>
void bandGains (Get plain, double gains[kBands])
{
    bool anySolo = false;
    for (int b = 0; b < kBands; ++b)
        anySolo |= plain (bandParam (b, kSolo)) >= 0.5;
    for (int b = 0; b < kBands; ++b)
    {
        const bool heard = anySolo ? plain (bandParam (b, kSolo)) >= 0.5 : plain (bandParam (b, kMute)) < 0.5;
        gains[b] = heard ? std::pow (10.0, plain (bandParam (b, kGain)) / 20.0) : 0.0;
    }
}

// One band's response at f (crossovers as effectiveCrossovers gives them), and the whole of them.
std::complex<double> bandResponse (int band, const double xover[kCrossovers], int slope, double f, double sampleRate);
std::complex<double> totalResponse (const double xover[kCrossovers], int slope, const double gains[kBands], double f,
                                    double sampleRate);

// For the editor (written by the audio thread).
struct Meters
{
    pk::ScopeBuffer<8192> scope;                                // mono input (a) and output (b), for the analyser
    std::atomic<float> xover[kCrossovers] = {120.0f, 1000.0f, 6000.0f}; // the crossovers now (they glide)
    std::atomic<uint32_t> blocks {0};                           // counts processed blocks
    std::atomic<float> sampleRate {48000.0f};
};

class Engine
{
public:
    // withTail: the end-of-chain Smacheratr (off where Levlr is built into another plug-in, and in tests)
    explicit Engine (bool withTail = true) : hasTail (withTail) {}
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return hasTail ? tail.latency () : 0; }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }
    int slopeInUse () const { return slopeNow; }

    // In place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    static constexpr int kChunk = 256;
    void processChunk (const float* inL, const float* inR, float* outL, float* outR, int n); // n <= kChunk
    void retune ();       // the filters to xfNow and slopeNow
    void resetFilters ();

    float inMono[kChunk] {}; // the chunk's input, for the analyser
    ParamArray p = defaultParams ();
    double sr = 48000.0;
    LrSplit split[kCrossovers];
    LrAllpass apLow[2]; // band 1 through the all-passes of crossovers 2 and 3
    LrAllpass apMid;    // band 2 through crossover 3's
    double xfNow[kCrossovers] = {120.0, 1000.0, 6000.0}; // gliding crossovers (Hz)
    int slopeNow = kSlope24;
    int coeffCountdown = 0;
    float gain[kBands] = {1.0f, 1.0f, 1.0f, 1.0f}, out = 1.0f;
    float duck = 1.0f; // fades out and back in around a change of slope
    double smoothGain = 0.0, glide = 0.0;
    float duckStep = 0.0f;
    smacheratr::Tail tail;
    bool hasTail = true;
    Meters* meters = nullptr;
};

} // namespace levlr

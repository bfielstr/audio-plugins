// Levlr: the spectrum in up to four bands that touch (band k's top edge is band k+1's bottom edge),
// each with its own level and drive, then Output and the Smacheratr at the end of the chain.
// The bands come from a tree of Linkwitz-Riley splits (Crossover.h), minimum phase: split at
// crossover 1 into band 1 and the rest, the rest at crossover 2, and so on; bands 1 and 2 then go
// through the all-passes of the crossovers above them, so with every band at 0 dB the four add up to
// an all-pass of the input. Its level is flat and its phase turns around each crossover: the sound of
// the crossovers, on purpose (moving a band's level is then a shelf-like step between two edges).
//   Bands (1 .. 4): with N bands only the first N-1 crossovers split (1 band: no split at all, the input
// as it is). The whole tree always runs: N bands are taps on it (3 bands: band 1 through crossover 2's
// all-pass only, band 2 before crossover 3's all-pass, band 3 the input of crossover 3), so a new
// count crossfades (20 ms) between the two sets of taps, which are there at once.
//   Each band then: its level, then its drive (Drive.h), then all of them add up. The drive's
// oversampling delays a band, so every band is delayed by that latency, driven or not (and the
// output's level with them): with every drive off the output is today's Levlr's, bit for bit, only
// latency () samples later (at 4x, the default: 37 at 48 kHz, 59 at 44.1, 14 at 96; at 2x the first
// half-band stage's alone, 32 at 48 kHz; Off: none). A new Oversampling takes effect at the start of the
// next block: the delays take their new length and the drives start again from silence (a short gap).
#pragma once

#include "Crossover.h"
#include "Drive.h"
#include "Params.h"

#include "pluginkit/ScopeBuffer.h"
#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

namespace levlr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// The crossovers as the engine uses them: in order, at least kMinGapOct apart, inside 20 Hz .. 20 kHz
// and under 0.45 of the sample rate (a lower crossover pushes the ones above it up).
void effectiveCrossovers (const double set[kCrossovers], double sampleRate, double out[kCrossovers]);

// Each band's gain as the engine applies it (linear): its level, or 0 when it is muted or another
// band is soloed. A soloed band is heard even when muted. Only the bands in use (count) count for
// solo (a soloed band past them doesn't silence the rest).
template <typename Get>
void bandGains (Get plain, double gains[kBands], int count = kBands)
{
    bool anySolo = false;
    for (int b = 0; b < count && b < kBands; ++b)
        anySolo |= plain (bandParam (b, kSolo)) >= 0.5;
    for (int b = 0; b < kBands; ++b)
    {
        const bool heard = anySolo ? plain (bandParam (b, kSolo)) >= 0.5 : plain (bandParam (b, kMute)) < 0.5;
        gains[b] = heard ? std::pow (10.0, plain (bandParam (b, kGain)) / 20.0) : 0.0;
    }
}

// One band's response at f (crossovers as effectiveCrossovers gives them), and the whole of them,
// with `count` bands in use (the bands past them: 0).
std::complex<double> bandResponse (int band, const double xover[kCrossovers], int slope, double f, double sampleRate,
                                   int count = kBands);
std::complex<double> totalResponse (const double xover[kCrossovers], int slope, const double gains[kBands], double f,
                                    double sampleRate, int count = kBands);

// For the editor (written by the audio thread).
struct Meters
{
    pk::ScopeBuffer<8192> scope;                                // mono input (a) and output (b), for the analyser
    std::atomic<float> xover[kCrossovers] = {120.0f, 1000.0f, 6000.0f}; // the crossovers now (they glide)
    std::atomic<uint32_t> blocks {0};                           // counts processed blocks
    std::atomic<float> sampleRate {48000.0f};
    // the band drives' latency as the engine runs it (-1 before it is prepared); a controller watching
    // it tells the host when Oversampling moves it (pk::ControllerBase::watchLatency)
    std::atomic<int> driveLatency {-1};
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
    // the drives' oversampling (always, whatever is on), and the end saturator's: at the Oversampling
    // settings set (what the next block runs at)
    int latency () const { return driveLatency () + (hasTail ? tail.latency () : 0); }
    int driveLatency () const { return drive[0].latencyAt (driveOversamplingFactor (p[kDriveOversampling])); }
    void setMeters (Meters* m)
    {
        meters = m;
        publishLatency ();
    }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }
    int slopeInUse () const { return slopeNow; }

    int bandsInUse () const { return countNow; }

    // In place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
    // Switched off where it is built into another plug-in: only the latency's delay, in place.
    void processBypassed (float* l, float* r, int n);

private:
    static constexpr int kChunk = 256;
    void processChunk (const float* inL, const float* inR, float* outL, float* outR, int n); // n <= kChunk
    void retune ();       // the filters to xfNow and slopeNow
    void resetFilters ();
    void applyOversampling (); // a new drive Oversampling: every band's delay and the drives, from silence
    void publishLatency ();
    int countTarget () const { return bandsOf (p[kBandCount]); }

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
    // the band count: taps crossfade from countFrom's to countNow's (countFade 0 .. 1)
    int countNow = kBands, countFrom = kBands;
    float countFade = 1.0f, countStep = 0.001f;
    // each band's drive, and the delay every band takes with it: per sample, the bands' taps (left,
    // right), their gains, the output level and the clean output (left, right), kRingStride floats,
    // latency () samples long
    BandDrive drive[kBands];
    static constexpr int kRingStride = 2 * kBands + kBands + 1 + 2;
    std::vector<float> ring, bypassRing; // room for 4x's latency; ringLen of it in use (0: no delay)
    int ringPos = 0, bypassPos = 0, ringLen = 0;
    // the chunk's bands for the drives: into them, the clean bands delayed, the drives' outputs
    float pre[kBands][2][kChunk] {}, dry[kBands][2][kChunk] {}, wet[kBands][2][kChunk] {}, mix[kBands][kChunk] {};
    float levelD[kChunk] {};
    smacheratr::Tail tail;
    bool hasTail = true;
    Meters* meters = nullptr;
};

} // namespace levlr

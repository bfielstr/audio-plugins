// Smoothr: a limiter that puts a smooth low end before the last dB of loudness.
//
//   input -> Input gain -> Smacheratr (the saturator; on, mild) -> Character (a dip in the low mids
//         on loud low-mid content) -> the limiter (Limiter.h: slow lows, fast highs, the ceiling)
//
// Saturation first, then limiting: the curve rounds the tops of the peaks, so the limiter has less to
// catch. The saturator is always in the path (off, it only delays the signal), so the latency never
// changes: the saturator's plus the limiter's.
#pragma once

#include "Character.h"
#include "Limiter.h"
#include "Params.h"

#include "pluginkit/ScopeBuffer.h"
#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>

namespace smoothr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// For the editor (written by the audio thread).
struct Meters
{
    static constexpr double kColumnHz = 160.0; // history columns a second
    pk::ScopeBuffer<4096> level; // per column: a = the limiter's input peak (lined up with the output), b = the output's peak (linear)
    pk::ScopeBuffer<4096> gr;    // per column: a = the lows' gain reduction, b = the highs' (dB, 0 or more)
    // the loudest sample since the editor last took it (it swaps in 0), per channel: the limiter's input and the output
    std::atomic<float> inPeak[2] {}, outPeak[2] {};
    std::atomic<float> dipDb {0.0f}; // Character's cut now (dB, 0 or less)
    std::atomic<uint32_t> blocks {0};
    std::atomic<float> sampleRate {48000.0f};
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return tail.latency () + limiter.latency (); }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }
    const Limiter& limiterStage () const { return limiter; }
    const CharacterDip& characterStage () const { return dip; }

    // In place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    static constexpr int kChunk = 256;
    void processChunk (const float* inL, const float* inR, float* outL, float* outR, int n); // n <= kChunk
    void applyLimiterParams ();

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    float bufL[kChunk] {}, bufR[kChunk] {};
    float inGain = 1.0f, smoothGain = 0.0f;
    smacheratr::Tail tail;
    CharacterDip dip;
    Limiter limiter;
    Meters* meters = nullptr;
    // the history column being gathered
    int colLen = 300, colFill = 0;
    float colIn = 0.0f, colOut = 0.0f, colLow = 1.0f, colHigh = 1.0f;
};

} // namespace smoothr

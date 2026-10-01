// Detonatr: an explosion designer that mirrors the user's explosion chain in REAPER (MVocoder ->
// Spiff -> SpinTracer -> Oxford TransMod -> Pro-L2 -> TransMod -> Pro-C 3 TTM -> Pro-C 3 TTM ->
// Saturn 2 -> Pro-L2). Ten stages, in the order the Stage parameters give:
//   Vocoder      the sound vocoding itself (Vocoder.h)
//   Spike        a spectral transient booster / cutter (Spike.h)
//   Motion       a Doppler swarm (Motion.h)
//   Transient 1  a transient modulator (Transient.h)
//   Limiter 1    a look-ahead true-peak limiter (Limiter.h)
//   Transient 2  the second transient modulator
//   Comp 1, 2    multiband upward and downward compression to a target (Ttm.h)
//   Tape         two-band tape saturation (Tape.h)
//   Limiter 2    the final limiter
// then Dry/Wet (the input delayed to line up), Output, and the Smacheratr every plug-in of the suite
// ends with (smacheratr::Tail, off by default).
// A stage that is off only delays by its latency (its input through a delay line), so the plug-in's
// latency is constant (the sum of the stages' and the tail's) whatever the order and what is on.
// Turning a stage on or off crossfades over 10 ms between it and its delay; a stage that is off and
// faded out is not run. When it comes back it starts from a clean state and runs (unheard) until its
// own delay has filled, then fades in.
#pragma once

#include "Limiter.h"
#include "Motion.h"
#include "Params.h"
#include "Spike.h"
#include "Tape.h"
#include "Transient.h"
#include "Ttm.h"
#include "Vocoder.h"

#include "smacheratr/src/core/Tail.h"

#include "pluginkit/ScopeBuffer.h"

#include <array>
#include <atomic>
#include <vector>

namespace detonatr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// For the editor (written by the audio thread once per block).
struct Meters
{
    std::atomic<float> inDb {-100.0f}, outDb {-100.0f}; // peak levels of the block
    std::atomic<uint32_t> blocks {0};
    std::atomic<float> sampleRate {48000.0f};
    static constexpr int kScopeSize = 1 << 17;
    pk::ScopeBuffer<kScopeSize> scope; // the input (lined up with the output) and the output, mono
    // Vocoder: each band's level (dB)
    std::atomic<int> vocBands {0};
    std::array<std::atomic<float>, Vocoder::kMaxBands> vocLevelDb {};
    std::array<std::atomic<float>, Vocoder::kMaxBands> vocFreq {};
    // Spike: each band's gain change (dB)
    std::array<std::atomic<float>, Spike::kBands> spikeGainDb {};
    std::array<std::atomic<float>, Spike::kBands> spikeFreq {};
    // Motion: the orbs (x right, y ahead, metres), how many, and the centre's distance and radius
    std::atomic<int> orbs {0};
    std::array<std::atomic<float>, Motion::kMaxOrbs> orbX {}, orbY {};
    std::atomic<float> motionDistance {3.0f}, motionRadius {2.0f};
    // Transient 1 and 2: the largest boost and cut of the block (dB)
    std::atomic<float> trBoostDb[2] {}, trCutDb[2] {};
    // Limiter 1 and 2: the most gain reduction of the block (dB)
    std::atomic<float> limReductionDb[2] {};
    // Comp 1 and 2: each band's level, target and gain (dB), and the make-up gain
    std::atomic<float> compLevelDb[2][Ttm::kBands] {}, compTargetDb[2][Ttm::kBands] {}, compGainDb[2][Ttm::kBands] {};
    std::atomic<float> compMakeupDb[2] {};
    smacheratr::Meters sat; // the Smacheratr at the end
    // bumps when the audio thread writes the stage meters (so a display can tell new values)
    std::atomic<uint32_t> stageBlocks {0};
};

class Engine
{
public:
    Engine ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const;
    int stageLatency (int stage) const;
    void setMeters (Meters* m);
    Order order () const { return ord; }

    // In place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // the stages, for the tests
    const Limiter& limiter (int i) const { return lim[i]; }
    const Motion& motion () const { return mot; }

private:
    void runStage (int stage, float* L, float* R, int n);
    void processStage (int stage, float* L, float* R, int n);
    void resetStage (int stage);
    void applyParam (uint32_t id, double v);
    void writeMeters ();

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int maxBlock = 512;
    Order ord {};
    Vocoder voc;
    Spike spk;
    Motion mot;
    Transient tr[2];
    Limiter lim[2];
    Ttm comp[2];
    Tape tape;
    smacheratr::Tail tail;
    // each stage's bypass: its input delayed by its latency, and the crossfade to it
    std::array<dsp::Delay, kNumStages> bypass;
    std::array<float, kNumStages> fade {}; // 1: the stage, 0: its delay
    std::array<int, kNumStages> warmup {};  // samples a stage coming back runs before it fades in
    float fadeStep = 0.01f;
    std::vector<float> bypassBuf[2];
    // dry/wet: the input delayed by the latency
    std::vector<float> dryLine[2];
    int dryPos = 0, dryLen = 1;
    float mix = 1.0f, out = 1.0f, smooth = 0.0f;
    std::vector<float> work[2];
    Meters* meters = nullptr;
};

} // namespace detonatr

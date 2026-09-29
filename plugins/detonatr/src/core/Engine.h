// Detonatr: an explosion / impact designer. Five stages, in the order the Stage parameters give:
//   Clean      a spectral denoiser and dereverber (Clean.h)
//   Tone       tuned resonators and vocoded recordings of household items, then a disperser (Tone.h)
//   Multiband  Multidyn (without its side-chain and its own saturator)
//   Transient  keeps a very short spike at each hit and drops the rest (Transient.h)
//   Saturator  Smacheratr, driven hard to raise the dropped body back up (smacheratr::Tail)
// then dry/wet (the dry signal delayed to line up) and the output gain.
// Every stage delays by its latency whether it is on or not, so the plug-in's latency is constant
// (the sum of the stages') and turning a stage off or moving it does not move the sound in time.
#pragma once

#include "Clean.h"
#include "Params.h"
#include "Tone.h"
#include "Transient.h"

#include "multidyn/src/core/Engine.h"
#include "multidyn/src/plugin/Meters.h"
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
    multidyn::Meters multiband;        // the Multiband stage's band levels
    smacheratr::Meters sat;            // the Saturator stage
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
    void setMeters (Meters* m);
    Order order () const { return ord; }

    // Audio thread, at the start of a block: the recording in a Tone slot (null: empty).
    void setCarrier (int slot, const Carrier* c) { tone.setCarrier (slot, c); }

    // In place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    void applyStageOn ();
    void runStage (int stage, float* L, float* R, int n);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int maxBlock = 512;
    Order ord {};
    Clean clean;
    Tone tone;
    multidyn::Engine multiband {false};
    Transient transient;
    smacheratr::Tail sat;
    // Tone has no latency, so it fades in and out against its own input
    float toneMix = 1.0f;
    bool toneIdle = false;
    std::vector<float> toneDry[2];
    // dry/wet: the input delayed by the latency
    std::vector<float> dryLine[2];
    int dryPos = 0, dryLen = 1;
    float mix = 1.0f, out = 1.0f, smooth = 0.0f;
    std::vector<float> work[2];
    Meters* meters = nullptr;
};

} // namespace detonatr

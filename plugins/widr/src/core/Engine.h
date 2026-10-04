// Widr: cinematic stereo width with a clear left, centre and right.
//
// Width that is only a side signal (L - R) is heard as a diffuse, phasey wall around the whole sound.
// Film-style separation comes from different material on each side of a dry centre, so Widr builds
// two voices from the mid, one for the left and one for the right, each played a little
// differently, like a double-tracked part, and later than the centre, so the centre keeps its place:
//   1. Mid / side: M = (L + R) / 2, S = (L - R) / 2. The source of the voices is the mid,
//      band-limited (above Mono Below, below a tone that follows Damping).
//   2. Each voice is its own blend of four generators, set by the Character: a delayed copy (a
//      different delay on each side, 1 to 40 ms, wandering a little like a second take: small pitch
//      and timing drifts), a decorrelator (its own all-pass chain), a micro pitch shift (down on
//      the left, up on the right) and its own early reflections. Width scales them.
//   3. Space: a short FDN reverb, fed band-limited, with separate left and right outputs.
//   4. Contrast keeps the centre and the sides apart: in time, the voices duck under the mid's
//      transients and bloom between them; across the spectrum they give way where the mid is strong
//      for its neighbourhood (a voice's presence) and fill where it is thin. The Character sets how
//      much, Contrast scales it.
//   5. The voices go through a graphic EQ of 24 third-octave bands (Bands.h) whose gains come from
//      the spectral contrast, the Mono Guard and the negotiation with the other Widrs (Mix.h). The two
//      voices are unrelated, so the mono fold only gains a little energy and nothing cancels; the
//      Mono Guard caps that gain (1.2 dB per band at 100 %) and the width of each band.
//   6. Air (a high shelf) and Beyond (a lift around 4 kHz) on the side.
//   7. Mono Below: the side is high-passed by a Linkwitz-Riley 8th-order filter and the mid goes
//      through the matching all-pass (the same crossover's low-pass plus high-pass), so the low end
//      is mono and mid and side stay in phase above it.
//   8. Output, then the Smacheratr tail.
// Width 0 bypasses everything but Output and Mono Check: bit-exact at 0 dB (the tail only delays).
#pragma once

#include "Bands.h"
#include "Dsp.h"
#include "Params.h"
#include "Reverb.h"

#include "locus/src/core/Fft.h"
#include "pluginkit/ScopeBuffer.h"
#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>
#include <vector>

namespace widr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// For the editor (written by the audio thread).
struct Meters
{
    std::atomic<float> correlation {1.0f};                  // of the output, -1 .. +1
    std::array<std::atomic<float>, kBands> bandGain {};     // gain on the voices per band
    std::array<std::atomic<float>, kBands> bandYield {};    // the part of it that yields to the group
    std::array<std::atomic<float>, kBands> bandSide {};     // generated side energy (dB) per band
    std::atomic<int> peers {0};                             // other live Widrs in this group
    std::atomic<int> slot {-1};                             // this instance's registry slot
    std::atomic<float> sampleRate {48000.0f};
    pk::ScopeBuffer<2048> scope;                            // output L / R, for the goniometer
    Meters ()
    {
        for (auto& g : bandGain)
            g.store (1.0f);
        for (auto& g : bandYield)
            g.store (1.0f);
        for (auto& g : bandSide)
            g.store (-120.0f);
    }
};

// The per-block outcome of the negotiation (Mix.h); neutral when alone.
struct MixOutcome
{
    std::array<float, kBands> yield {}; // 0 .. 1 per band
    float roleScale = 1.0f;             // the role pulls the width (Anchor narrower, ...)
    float mirror = 1.0f;                // -1: swap the voices, to widen the other way from a twin
    int peers = 0;
    MixOutcome () { yield.fill (1.0f); }
};

class Engine
{
public:
    // withTail: the end-of-chain Smacheratr (off where Widr is built into another plug-in)
    explicit Engine (bool withTail = true);
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain)
    {
        if (id >= kNumParams)
            return;
        p[id] = plain;
        if (id >= kTailBase && id < kTailBase + pk::kTailFields)
            tail.setParam (id - kTailBase, plain);
        else if (id >= kTailExt4Base)
            tail.setParam (smacheratr::kTailExt4First + (id - kTailExt4Base), plain);
        else if (id >= kTailExt3Base)
            tail.setParam (smacheratr::kTailExt3First + (id - kTailExt3Base), plain);
        else if (id >= kTailExt2Base)
            tail.setParam (pk::kTailFields + pk::kTailExtFields + (id - kTailExt2Base), plain);
        else if (id >= kTailExtBase)
            tail.setParam (pk::kTailFields + (id - kTailExtBase), plain);
    }
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return hasTail ? tail.latency () : 0; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    void setMeters (Meters* m)
    {
        meters = m;
        if (m)
            m->sampleRate.store ((float)sr);
    }

    // The negotiation's outcome for the next blocks (smoothed inside).
    void setMixOutcome (const MixOutcome& o) { mix = o; }
    // What the group sees (Mix.h): the generated side per band after this instance's own gains,
    // what it would generate before yielding, the mid energy (all smoothed), the width it plays at.
    const std::array<float, kBands>& sideEnergy () const { return pubSide; }
    const std::array<float, kBands>& desiredSide () const { return eGs; }
    const std::array<float, kBands>& midEnergy () const { return eM; }
    double effectiveWidth () const { return p[kWidth] * mix.roleScale; }
    void reportMix (int peers, int slot)
    {
        if (meters)
        {
            meters->peers.store (peers, std::memory_order_relaxed);
            meters->slot.store (slot, std::memory_order_relaxed);
        }
    }
    double sampleRate () const { return sr; }
    float correlation () const { return corr; }
    float bandGain (int k) const { return gCur[(size_t)k]; }

private:
    // one of the two voices (left or right): its own delay, decorrelator, pitch shift and reflections
    struct Voice
    {
        std::array<Allpass, 4> decor;
        MicroShift shift;
        OnePole erDamp;
        double delay = 1.0, delayT = 1.0;   // samples
        double drift = 0.0, driftT = 0.0;   // -1 .. 1, the wander of the delay
        uint32_t seed = 1;
        int driftCount = 0;
        std::array<Biquad, kBands> bank;    // this voice through the per-band gains
        void reset ();
    };

    void blockSetup ();
    void analyse ();
    void processBlock (const float* inL, const float* inR, float* outL, float* outR, int n);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int maxBlock = 512;
    Meters* meters = nullptr;
    MixOutcome mix;

    // the voices' source and the generators
    BiquadCoeffs srcHpC, srcLpC, revHpC, revLpC;
    Biquad srcHp, srcLp, revHp, revLp;
    double srcHpHz = -1.0, srcLpHz = -1.0;
    DelayLine line; // the band-limited mid, for the delays and the reflections
    std::array<Voice, 2> voice;
    float erDampA = 0.5f;
    Reverb reverb;
    double lfoPhase = 0.0;

    // shaping of the voices, psychoacoustic cues, Mono Below
    std::array<BiquadCoeffs, kBands> bankC;
    std::array<float, kBands> bankDb {};
    bool bankFlat = true;
    BiquadCoeffs airC, beyondC;
    Biquad airZ, beyondZ;
    double airDb = -1.0, beyondAmt = -1.0;
    std::array<BiquadCoeffs, 2> xLp, xHp; // Butterworth-4 sections of the LR8 crossover
    std::array<Biquad, 4> sideHp, midLp, midHp;
    double xHz = -1.0;

    // smoothed controls
    float wet = 0.0f, width = 0.0f, space = 0.0f, outGain = 1.0f, smooth = 0.0f, slow = 0.0f;
    float dryLevel = 1.0f, wetLevel = 1.0f; // the parallel Dry / Wet levels
    std::array<float, 4> gen {}, genT {};
    double erScale = 1.0, erScaleT = 1.0, driftDepth = 0.0;
    int erTaps = 16;
    std::array<float, 16> erGain {};
    float reverbSend = 1.0f, mirror = 1.0f;
    float level = 1.0f, contrast = 0.0f; // the Character's voice level; contrast amount (Character x Contrast)
    bool bypassed = false;
    // temporal contrast: fast and slow mean square of the mid, and the gain on the voices
    double envFast = 0.0, envSlow = 0.0;
    float duck = 1.0f, duckT = 1.0f, duckAtt = 0.0f, duckRel = 0.0f, envFastA = 0.0f, envSlowA = 0.0f;
    int duckCount = 0;

    // analysis (for the guard, the contrast and the negotiation), every kHop samples: the input's
    // mid and side, what the voices add to the mid (Gm) and to the side (Gs), and the mid with them
    // (Mono: where the voices are still in phase with the mid, low down, they add more than power)
    static constexpr int kFft = 2048, kHop = 1024;
    locus::Fft fft {kFft};
    std::vector<float> ringM, ringS, ringGm, ringGs, ringMono, window, frame;
    std::vector<locus::Fft::cf> spec;
    int ringPos = 0, hopCount = 0;
    std::array<float, kBands> eM {}, eS {}, eGm {}, eGs {}, eMono {}, pubSide {};
    std::array<float, kBands> gCur {}, gYieldCur {};

    // output correlation
    double sLR = 0.0, sLL = 0.0, sRR = 0.0;
    float corr = 1.0f;

    smacheratr::Tail tail;
    bool hasTail = true;
};

} // namespace widr

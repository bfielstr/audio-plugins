// Widr: cinematic stereo width that keeps the mono fold.
//
// Everything Widr adds is side signal (L - R); the mid (L + R) passes untouched apart from the
// phase of the Mono Below crossover, so folding the output to mono gives back the input's mid.
//   1. Mid / side: M = (L + R) / 2, S = (L - R) / 2.
//   2. Four generators make side from the mid: a Haas pair (the band-limited mid, delayed, added
//      to one channel and subtracted from the other: complementary combs, the mid stays put), a
//      decorrelator (a cascade of all-passes), a micro pitch spread (a few cents down on the left
//      and up on the right, slowly modulated) and early reflections (8 or 16 taps, spaced by
//      Size, low-passed by Damping). Character sets their blend (normalised to the same power);
//      Width scales it.
//   3. Space: a short FDN reverb fed mostly from the side; its output is side as well.
//   4. Contrast keeps the centre and the sides apart: in time, the generated side ducks under the
//      mid's transients and blooms between them (hits stay dry and centred, tails go wide); across
//      the spectrum, it backs off where the mid is strong for its neighbourhood (a voice's
//      presence) and fills where the mid is thin. The Character sets how much, Contrast scales it.
//   5. The generated side goes through a graphic EQ of 24 third-octave bands (Bands.h) whose gains
//      come from the spectral contrast, the Mono Guard (a ceiling on the side of every band, a floor
//      under its correlation) and the negotiation with the other Widrs of the group (Mix.h).
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
    std::array<std::atomic<float>, kBands> bandGain {};     // gain on the generated side per band
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
    float mirror = 1.0f;                // -1: widen the other way from a twin of the same role
    int peers = 0;
    MixOutcome () { yield.fill (1.0f); }
};

class Engine
{
public:
    Engine ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain)
    {
        if (id >= kNumParams)
            return;
        p[id] = plain;
        if (id >= kTailBase && id < kTailBase + pk::kTailFields)
            tail.setParam (id - kTailBase, plain);
    }
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return tail.latency (); }

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
    const std::array<float, kBands>& desiredSide () const { return eG; }
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
    void blockSetup ();
    void analyse ();
    void processBlock (const float* inL, const float* inR, float* outL, float* outR, int n);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int maxBlock = 512;
    Meters* meters = nullptr;
    MixOutcome mix;

    // generators
    BiquadCoeffs haasHpC, haasLpC;
    Biquad haasHp, haasLp;
    DelayLine haasLine, erLine;
    std::array<Allpass, 4> decor;
    MicroShift shiftDown, shiftUp;
    OnePole erDamp;
    float erDampA = 0.5f;
    Reverb reverb;
    double lfoPhase = 0.0;

    // shaping of the generated side, psychoacoustic cues, Mono Below
    std::array<BiquadCoeffs, kBands> bankC;
    std::array<Biquad, kBands> bank;
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
    std::array<float, 4> gen {}, genT {};
    double haasD = 1.0, haasDT = 1.0, erScale = 1.0, erScaleT = 1.0;
    int erTaps = 16;
    float level = 1.0f, contrast = 0.0f; // the Character's side level; contrast amount (Character x Contrast)
    // temporal contrast: fast and slow mean square of the mid, and the gain on the generated side
    double envFast = 0.0, envSlow = 0.0;
    float duck = 1.0f, duckT = 1.0f, duckAtt = 0.0f, duckRel = 0.0f, envFastA = 0.0f, envSlowA = 0.0f;
    int duckCount = 0;
    std::array<float, 16> erGain {};
    float reverbSend = 1.0f, mirror = 1.0f;
    bool bypassed = false;

    // analysis (for the guard and the negotiation), every kHop samples
    static constexpr int kFft = 2048, kHop = 1024;
    locus::Fft fft {kFft};
    std::vector<float> ringM, ringS, ringG, window, frame;
    std::vector<locus::Fft::cf> spec;
    int ringPos = 0, hopCount = 0;
    std::array<float, kBands> eM {}, eS {}, eG {}, pubSide {};
    std::array<float, kBands> gCur {}, gYieldCur {};

    // output correlation
    double sLR = 0.0, sLL = 0.0, sRR = 0.0;
    float corr = 1.0f;

    smacheratr::Tail tail;
};

} // namespace widr

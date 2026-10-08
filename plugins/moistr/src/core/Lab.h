// Moistr's LAB (0.29): effects chains on the bands of the first pass, and POST after their sum.
//
//   Low  ------------------------------------------- delay (the chains' latency + POST's) ---------+
//   Mid  -> band gain (Movement, gestures) -> chain 1 [slots 1 .. 4] -> Mono -> Level -> align --+  |
//   High -> band gain                      -> chain 2                                   -> align -+-> sum -> POST [slots 1 .. 3]
//   Air  -> band gain                      -> chain 3                                   -> align --+     -> (Liquid, Close, Wobble,
//                                                                                                           Shift) -> + Low
// A slot is a smemplr rack slot (smemplr/src/core/FxSlot.h): Empty or one of the suite's effects, its values stored
// normalized and read through the kind's own table. The band's moving gain comes first, so Movement and the
// gestures change how hard a band drives its chain (its timbre moves, not only its level). Chain 4 (parallel, on
// all three bands) is kept for later: its parameters exist, it does not run.
//
// POST sits on the sum of the chains, before Liquid, Close, Wobble and the shifter: a multiband OTT there glues the
// three chains into one dense upper sound, while the movements after it stay on top of it (OTT's upward compression
// would flatten them if they came first). The Low band never goes through a chain or POST: the sub stays clean.
//
// Latency: each chain's is the sum of its slots' kinds' (on or off, as in a smemplr rack); every chain is delayed to
// the slowest one's, and the Low band (and the dry signal, Engine) to that plus POST's, so the paths sum as they
// did. While any slot holds an effect, the chains and POST run kChunk samples at a time (the effects' work per call is
// then spread over more samples), which adds kChunk to the latency. With every slot Empty the LAB's latency is 0.
//
// Nothing a chain or POST adds goes below Low X: what an effect changes (its output against its input, lined up) is
// high-passed at Low X (24 dB/oct) before it is added back, so a distorted band cannot put a new fundamental or
// pumping under the sub (the Low band alone is heard there, as without the LAB), while a chain whose effects are
// off passes its band as it was (the bands still sum flat).
//
// Mute fades a chain out (20 ms) and then stops running it; Solo: while any chain is soloed only the soloed ones are
// heard (the Low band too is silent). Mono: the chain's output in the middle. Level: after its slots (-48 dB: off).
// Both channels are processed alike: a mono input stays mono (the kinds are stereo-linked).
#pragma once

#include "Dsp.h"
#include "Movement.h"
#include "Params.h"

#include "smemplr/src/core/FxSlot.h"

#include <array>
#include <memory>
#include <vector>

namespace moistr {

class Lab
{
public:
    static constexpr int kTick = 16;               // the most samples run () takes (the engine's tick)
    static constexpr int kChunk = 64;              // the effects' block
    static constexpr int kMaxLatency = 1 << 14;   // (the alignment's delays; far more than four slots take)

    Lab ();
    void prepare (double sampleRate);
    void reset ();
    // a LAB parameter (isLabParam): plain is its plain value (a slot's block values are normalized)
    void setParam (uint32_t id, double plain);
    void setTransport (double bpm, double ppq, bool playing);

    // Whether the LAB does anything now: a slot of chains 1 .. 3 or POST holding an effect, a chain not at 0 dB, muted,
    // soloed or mono (or still fading). Not active: the engine leaves it out (moistr as 0.28, bit for bit).
    bool active () const;
    int latency () const;                  // (with an effect in any slot) kChunk, the slowest chain's and POST's
    bool holdsEffects () const;            // a slot of chains 1 .. 3 or POST not Empty
    int chainLatency (int chain) const;    // its slots' kinds'
    int postLatency () const;
    int kind (int slot) const { return slots[(size_t)slot] ? slots[(size_t)slot]->type : smemplr::kFxEmpty; } // (smemplr::FxType)

    // One tick of m <= kTick samples. in[c]: chain c's band (both channels, its gain applied; run in place); low: the
    // Low band (delayed in place to line up); up: the chains' sum after POST (written); lowX: the Low crossover (Hz)
    void run (float (*in)[2][kTick], double (*low)[kTick], double (*up)[kTick], int m, double lowX);

    // a slot (nullptr for chain 4's: kept for later), for the gestures' targets and the tests
    smemplr::FxSlot* slot (int s) { return slots[(size_t)s].get (); }
    // a chain's first slot of a kind (-1: none), among chains 0 .. 2 (3: POST)
    int firstOf (int chain, int fxType) const;
    double chainGain (int chain) const { return gain[(size_t)chain]; } // now, with its fades (0 .. )

private:
    struct Delay
    {
        std::vector<double> buf[2];
        int w = 0;
        void reset ();
        // writes x and returns what went in `delay` samples ago
        double tick (int ch, double x, int delay);
        void advance () { w = (w + 1) & (kMaxLatency - 1); }
    };
    void runSlots (int first, int count, float* l, float* r, int m);
    void block (int m); // the chains and POST on chunkIn's first m samples, into chunkOut
    // what the effects changed (out against the input lined up, dry), high-passed at Low X, added back to dry
    struct Below
    {
        dsp::Svf hp[2][2];
        void reset ();
        double tick (int ch, double dry, double out, const dsp::SvfCoefs& c);
    };

    std::array<std::unique_ptr<smemplr::FxSlot>, kNumLabSlots> slots;
    double sr = 48000.0;
    // the chains' settings, and their gains now (Level, Mute and Solo; gliding) and the Low band's (Solo)
    std::array<double, kNumChains> levelDb {}, gain {};
    std::array<bool, kNumChains> mute {}, solo {}, mono {};
    double lowGain = 1.0, fade = 0.001;
    std::array<bool, kNumBandChains> wasRunning {};
    Delay chainDelay[kNumBandChains], lowDelay;
    Delay chainDry[kNumBandChains], postDry; // (the inputs, lined up with the effects' outputs)
    Below chainBelow[kNumBandChains], postBelow;
    float post[2][kChunk] {};
    float chunkIn[kNumBandChains][2][kChunk] {};
    double chunkOut[2][kChunk] {}, held[2][kChunk] {}; // (held: the last chunk's, coming out now)
    int fill = 0;
    dsp::SvfCoefs below;
};

} // namespace moistr

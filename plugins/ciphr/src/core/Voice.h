// One of Ciphr's voices: the oscillator cluster (kOscs oscillators, each crossfading two neighbouring
// entries of its list at the Timbre position), Cross (phase modulation or ring modulation between
// neighbouring oscillators), the input when it takes the Voices path, the morphing filter with its
// envelope, and the amp envelope (the VCA). Mono: the processor after the voices makes the stereo.
#pragma once

#include "Dsp.h"
#include "Variant.h"
#include "Wavetable.h"

namespace ciphr {

// What every voice shares for one slice of a block (the engine works it out every kSlice samples).
struct VoiceBlock
{
    double sr = 48000.0;
    double tune = 0.0;          // semitones
    double oscPos[kOscs] {};    // each oscillator's place in its list (0 .. kEntries - 1): Timbre and Drift
    double oscCents[kOscs] {};  // each oscillator's detune: Character's spread and Drift
    float crossFrom = 0.0f, crossTo = 0.0f; // Cross, ramped over the slice (-1 .. 1)
    double cutoff = 6000.0, resonance = 0.2, type = 0.0, keyTrack = 0.5, envAmount = 0.3;
    float velocity = 0.6f;       // how much the velocity sets the level
    const float* input = nullptr; // the input (mono) on the Voices path, or nullptr
    float inputGain = 0.0f;
};

class Voice
{
public:
    // FM (Cross left of centre): the most phase modulation, in cycles of the modulated oscillator
    static constexpr double kPmDepth = 0.35;
    // above this fundamental the FM index is turned down (in proportion), so it stays roughly band-limited
    static constexpr double kPmFullBelowHz = 2000.0;
    // ring modulation's make-up gain (the product of two waves is quieter than either)
    static constexpr float kRingGain = 1.4f;
    // For the tests: true takes Cross out of every voice ("Cross off"), whatever its value
    static inline bool crossBypass = false;

    void prepare (double sampleRate);
    void setEnvelopes (double a, double d, double s, double r, double fa, double fd, double fs, double fr);
    // fresh: from silence (phases from the patch, the filter cleared); else a retrigger or a steal (the
    // phases and the filter run on, the envelopes rise from where they are: no click)
    void start (int note, float velocity, const Patch& p, bool fresh, uint32_t order);
    void release ();
    void kill (); // silent at once (reset)

    // Adds n samples (n <= the engine's slice) to out.
    void render (float* out, int n, const VoiceBlock& b, const Patch& p);

    bool active () const { return amp.active (); }
    bool gated () const { return gate; }
    int note () const { return noteNum; }
    uint32_t order () const { return startOrder; }
    float level () const { return amp.value (); }
    dsp::Envelope::Stage stage () const { return amp.current (); }

private:
    double sr = 48000.0;
    int noteNum = 60;
    float velGainRaw = 1.0f;
    bool gate = false;
    uint32_t startOrder = 0;
    double phase[kOscs][kEntries] {};
    float last[kOscs] {}; // the oscillators' last outputs (oscillator 0's FM comes from the last one's)
    dsp::Envelope amp, fenv;
    dsp::Svf filter;
};

} // namespace ciphr

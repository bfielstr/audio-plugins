// Perrera: a high-pass and a low-pass filter in parallel (their outputs are summed, with the
// polarity that makes two filters meeting at one cutoff sum flat), so with the
// high-pass above the low-pass there is a notch between them and with the two meeting there is
// nothing. Split moves them apart or together around their set frequencies, an envelope
// triggered by MIDI notes adds to Split, and both cutoffs follow the played note: the root note
// leaves them where they are set, and Transpose, pitch bend and Key tracking all count.
#pragma once

#include "Params.h"
#include "Svf.h"

#include <array>
#include <atomic>

namespace perrera {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// For the editor (written by the audio thread once per block).
struct Meters
{
    std::atomic<float> hpHz {800.0f}, lpHz {200.0f}; // effective cutoffs, tracking and envelope included
    std::atomic<float> offset {0.0f};                // semitones the tracked note moves both cutoffs
    std::atomic<float> env {0.0f};                   // envelope level 0 .. 1
    std::atomic<int> note {-1};                      // the note being tracked, -1 = none
};

// Where the cutoffs sit for a note offset (semitones from the root, key tracking applied) and a
// total split (Split + envelope), shared with the editor's display.
inline double hpCutoff (double hpBase, double offsetSemis, double splitSemis)
{
    return hpBase * std::pow (2.0, (offsetSemis + 0.5 * splitSemis) / 12.0);
}
inline double lpCutoff (double lpBase, double offsetSemis, double splitSemis)
{
    return lpBase * std::pow (2.0, (offsetSemis - 0.5 * splitSemis) / 12.0);
}

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain) { p[id] = plain; }
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return 0; }

    void noteOn (int note);
    void noteOff (int note);
    void setPitchBend (float bipolar) { bend = bipolar; }
    int trackedNote () const { return lastNote; }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    void setMeters (Meters* m) { meters = m; }
    double envLevel () const { return env; }

private:
    double targetOffset () const;

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    Svf hp[2][2], lp[2][2]; // [channel][stage]
    SvfCoeffs hpC, lpC;
    int lastNote = -1;
    float bend = 0.0f;
    double env = 0.0;
    bool envRising = false;
    double offset = 0.0, split = 0.0; // smoothed semitone offsets
    float mix = 1.0f, out = 1.0f, smooth = 0.0f, semiSmooth = 0.0f;
    Meters* meters = nullptr;
};

} // namespace perrera

// Para: a high-pass and a low-pass filter in parallel (their outputs are summed, with the
// polarity that makes two filters meeting at one cutoff sum flat), so with the
// high-pass above the low-pass there is a notch between them and with the two meeting there is
// nothing. Split moves them apart or together around their set frequencies and an envelope
// triggered by MIDI notes adds to Split. The cutoffs do not follow the notes (they used to: Key,
// Transpose, Bend and Root are left unused).
// Movement: Free keeps the filters independent. Vocal couples them: the filter that moved last
// leads, and when it crosses the other (the low-pass above the high-pass), the other is pushed
// along to the leader's cutoff and fades out, from the crossing to -inf Fade semitones past it
// (an octave by default, equal-power: -3 dB half way), so one filter sweeps alone instead of the
// two summing. With Notch on, Liquid also runs a notch after the sum that follows the low-pass,
// zigzagging up to 7 semitones either side of it as the low-pass moves (still when it stops); it
// fades out towards 180 Hz, so the sub is untouched. Liquid is Vocal with Split swinging along with
// the sweep: the leader overshoots in the direction it moves (and the other filter the other way)
// by as far as it moved in the last ~150 ms, then flows back when it stops; on a Reese that is the
// liquid, techy movement.
#pragma once

#include "Params.h"
#include "Svf.h"

#include "pluginkit/ScopeBuffer.h"
#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>

namespace para {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// For the editor (written by the audio thread once per block).
struct Meters
{
    std::atomic<float> hpHz {800.0f}, lpHz {200.0f}; // effective cutoffs, tracking and envelope included
    std::atomic<float> hpShift {0.0f}, lpShift {0.0f}; // semitones the tracking, envelope and glide move the set
                                                        // cutoffs (before Vocal pushes one: the display does that)
    std::atomic<float> hpMul {1.0f}, lpMul {1.0f};   // Vocal: the fade of the pushed filter
    std::atomic<bool> leaderLp {true};               // Vocal: the low-pass leads (it moved last)
    std::atomic<uint32_t> blocks {0};                // counts processed blocks: the editor sees audio running
    std::atomic<float> notchHz {1000.0f}, notchCut {0.0f}; // Liquid's notch: where, and how deep (0 .. 1)
    std::atomic<float> offset {0.0f};                // semitones the tracked note moves both cutoffs
    std::atomic<float> env {0.0f};                   // envelope level 0 .. 1
    std::atomic<int> note {-1};                      // the note being tracked, -1 = none
    std::atomic<float> sampleRate {48000.0f};
    pk::ScopeBuffer<4096> scope;                     // mono input (a) and output (b), for the spectra
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

// Vocal movement, shared with the display: once the leader crosses the follower (the low-pass above
// the high-pass), the follower sits at the leader's cutoff and fades, from 0 dB at the crossing to
// -inf fadeSemis past it along an equal-power curve (-3 dB half way).
inline void vocalPush (double& hpHz, double& lpHz, bool leaderLp, double fadeSemis, float& hpMul, float& lpMul)
{
    hpMul = lpMul = 1.0f;
    const double over = 12.0 * std::log2 (lpHz / hpHz);
    if (over <= 0.0)
        return;
    const float fade = (float)std::cos (0.5 * M_PI * std::fmin (1.0, over / std::fmax (0.5, fadeSemis)));
    if (leaderLp)
    {
        hpHz = lpHz;
        hpMul = fade;
    }
    else
    {
        lpHz = hpHz;
        lpMul = fade;
    }
}

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    // withTail: the end-of-chain Smacheratr (off where Para is built into another plug-in)
    explicit Engine (bool withTail = true) : hasTail (withTail) {}
    void setParam (uint32_t id, double plain)
    {
        if (id >= kNumParams)
            return;
        p[id] = plain;
        if (id >= kTailBase && id < kTailBase + pk::kTailFields)
            tail.setParam (id - kTailBase, plain);
    }
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return hasTail ? tail.latency () : 0; }

    void noteOn (int note);
    void noteOff (int note);
    void setPitchBend (float bipolar) { bend = bipolar; }
    int trackedNote () const { return lastNote; }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    void setMeters (Meters* m)
    {
        meters = m;
        if (m)
            m->sampleRate.store ((float)sr);
    }
    double envLevel () const { return env; }

private:
    double targetOffset () const;
    double lpRes () const { return p[kResLink] >= 0.5 ? p[kHpRes] : p[kLpRes]; }

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    Svf hp[2][2], lp[2][2]; // [channel][stage]
    OnePole hp1[2], lp1[2];  // the first-order section of 18 dB
    double hpG1 = 0.0, lpG1 = 0.0;
    SvfCoeffs hpC, lpC;
    int lastNote = -1;
    float bend = 0.0f;
    double env = 0.0;
    bool envRising = false;
    double offset = 0.0, split = 0.0; // smoothed semitone offsets
    float mix = 1.0f, out = 1.0f, hpG = 1.0f, lpG = 1.0f, smooth = 0.0f, semiSmooth = 0.0f;
    Meters* meters = nullptr;
    bool hasTail = true;
    // Vocal movement
    bool leaderLp = true;
    // Liquid: where the leader is (semitones) and where it was ~150 ms ago
    double liquidSlow = 0.0, liquidA = 0.0;
    bool liquidLeaderLp = true;
    // Liquid's notch: a band cut after the sum, and where it is in its zigzag
    Svf notch[2];
    SvfCoeffs notchC;
    double zigPhase = 0.0, lastLpSemis = -1.0, notchHz = 1000.0;
    float notchCut = 0.0f, notchCutT = 0.0f;
    double prevHpBase = -1.0, prevLpBase = -1.0, curHp = 0.0, curLp = 0.0, rawHp = 0.0, rawLp = 0.0;
    float hpMul = 1.0f, lpMul = 1.0f, hpMulT = 1.0f, lpMulT = 1.0f;
    smacheratr::Tail tail;
};

} // namespace para

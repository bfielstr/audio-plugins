// Para: a high-pass and a low-pass filter in parallel (their outputs are summed, with the
// polarity that makes two filters meeting at one cutoff sum flat), so with the
// high-pass above the low-pass there is a notch between them and with the two meeting there is
// nothing. Split moves them apart or together around their set frequencies and an envelope
// triggered by MIDI notes adds to Split. The cutoffs do not follow the notes (they used to: Key,
// Transpose, Bend and Root are left unused).
// Movement: Free keeps the filters independent. Vocal couples them: the filter that moved last
// leads. A low-pass leading takes the high-pass with it once it rises past Dip Start (80 Hz by
// default): the high-pass rises as far as the low-pass has gone past Dip Start (and never sits below
// it) and fades out, to -inf Fade semitones past Dip Start (an octave by default, equal-power: -3 dB
// half way), so the low-pass ends up sweeping alone. A high-pass leading pushes the low-pass down
// once it crosses it, fading it from the crossing the same way. Split also swings with the sweep:
// the leader overshoots in the direction it moves (and the other filter the other way) by as far as
// it moved in the last ~150 ms, then flows back when it stops; on a Reese that is the liquid, techy
// movement (this used to be a separate Liquid mode).
// Floor: the low-pass never goes below Low-Pass Floor (40 Hz by default), whatever Split, the
// envelope or the swing do, so the sub stays.
// Drive: Smacheratr's Analog curve (DriveStage, oversampled), Pre: on the input, before the filters
// (and the dry/wet); Post: on what comes out of them (after the dry/wet, before Output). Either way
// it delays the sound by the same amount, on or off, so Para's latency never changes. Moving it
// between Pre and Post fades the output out and back in (the stage's delay moves with it).
#pragma once

#include "Drive.h"
#include "Params.h"
#include "Svf.h"

#include "pluginkit/ScopeBuffer.h"
#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>
#include <vector>

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

// Vocal movement, shared with the display (see the top of this file). The low-pass leading: past
// dipHz the high-pass rises with it and fades, from 0 dB at dipHz to -inf fadeSemis past it (or from
// the crossing, if the high-pass is set below dipHz), along an equal-power curve (-3 dB half way).
// The high-pass leading: once it crosses the low-pass, the low-pass sits at its cutoff and fades
// from the crossing the same way.
inline void vocalPush (double& hpHz, double& lpHz, bool leaderLp, double fadeSemis, double dipHz, float& hpMul,
                       float& lpMul)
{
    hpMul = lpMul = 1.0f;
    const double cross = 12.0 * std::log2 (lpHz / hpHz);
    auto fadeAt = [&] (double over) {
        return (float)std::cos (0.5 * M_PI * std::fmin (1.0, over / std::fmax (0.5, fadeSemis)));
    };
    if (leaderLp)
    {
        const double past = 12.0 * std::log2 (lpHz / std::fmax (1.0, dipHz));
        const double over = std::fmax (past, cross);
        if (over <= 0.0)
            return;
        hpHz = std::fmax (hpHz * std::pow (2.0, std::fmax (0.0, past) / 12.0), lpHz);
        hpMul = fadeAt (over);
    }
    else if (cross > 0.0)
    {
        lpHz = hpHz;
        lpMul = fadeAt (cross);
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
        else if (id >= kTailExtBase && id < kTailExtBase + pk::kTailExtFields)
            tail.setParam (pk::kTailFields + (id - kTailExtBase), plain);
    }
    double param (uint32_t id) const { return p[id]; }
    // Depends on the sample rate only: the drive's delay, whether it is on and wherever it is, and
    // the end-of-chain Smacheratr's.
    int latency () const { return drive.latency () + (hasTail ? tail.latency () : 0); }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    void noteOn (int note);
    void noteOff (int note);
    void setPitchBend (float bipolar) { bend = bipolar; }
    int trackedNote () const { return lastNote; }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
    // Switched off where Para is built into another plug-in: only the delay latency () stands for,
    // so switching Para off does not move the sound in time. In place.
    void processBypassed (float* l, float* r, int n);

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

    void processBlock (const float* inL, const float* inR, float* outL, float* outR, int n);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int maxBlock = 512;
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
    // Vocal's swing: where the leader has been lately (a ~150 ms follower)
    double liquidSlow = 0.0, liquidA = 0.0;
    bool liquidLeaderLp = true;
    double prevHpBase = -1.0, prevLpBase = -1.0, curHp = 0.0, curLp = 0.0, rawHp = 0.0, rawLp = 0.0;
    float hpMul = 1.0f, lpMul = 1.0f, hpMulT = 1.0f, lpMulT = 1.0f;
    smacheratr::Tail tail;
    // the drive, where it is (kDrivePos) and the fade that covers moving it: out to silence, held
    // while the stage fills again, back in
    DriveStage drive;
    bool drivePost = false;
    float duck = 1.0f, duckStep = 0.0f;
    int duckHold = 0;
    std::vector<float> src[2], mixed[2], gOut, scopeIn; // the input (process may be in place), the dry/wet sum (Post)
    std::vector<float> bypassDelay[2];
    int bypassPos = 0;
};

} // namespace para

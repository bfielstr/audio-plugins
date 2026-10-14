// Tapr: Scream 4's Tape mode, measured knob by knob (sweepr's TapeShape.h), with its Cut EQ (CutShape.h).
//
//   input -> [Cut, Pre] -> 4x up -> Drive -> [leveller, Pre] -> sine clipper -> [leveller, Post] -> 4x down
//         -> DC blocker -> Speed's high shelf -> [Cut, Post] -> Mix (with the input delayed as much) -> Output
//         -> Smacheratr (the end saturator, off by default)
//
// The tape (Tape on; switching fades over 10 ms). At 4x: Drive's gain (-10 to +50 dB), the leveller (a
// stereo-linked peak follower, 2.2 ms attack, 21 ms release, and a gain 1 / (1 + 3.21 Compression level))
// and the sine clipper (flat at +-0.5, anti-aliased with its antiderivative). Compression Place puts the
// leveller before the clipper (Pre, Scream's: it holds the level into the curve, so more Compression is
// softer and cleaner) or after it (Post: it rides the clipped signal down, so the drive stays as hard and the
// level is held). At the base rate: a 10 Hz DC blocker and Speed's high shelf.
//
// The Cut EQ (Cut on; switching fades over 10 ms): Lo, Mid and Hi bells as trapezoidal SVFs at the base rate.
// Cut Place puts them after the tape (Post, Scream's) or before it (Pre: the EQ shapes what saturates).
// Switching either Place takes effect at once.
//
// Latency: the oversampler's (constant whatever the settings), plus the end saturator's. Mix blends with
// the input delayed by the same amount; Mix 0 is the delayed input bit for bit.
#pragma once

#include "CutShape.h"
#include "Params.h"

#include "para/src/core/Oversampler.h"
#include "smacheratr/src/core/Tail.h"
#include "sweepr/src/core/Dsp.h"
#include "sweepr/src/core/TapeShape.h"

#include <array>
#include <atomic>
#include <vector>

namespace tapr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// What the editor shows (written by the audio thread once per block).
struct Meters
{
    std::atomic<uint32_t> blocks {0};
    std::atomic<float> sampleRate {48000.0f};
    std::atomic<float> levellerDb {0.0f}; // the leveller's gain now (dB, 0 or below)
    std::atomic<float> curvePeak {0.0f};  // the peak into the curve over the block (linear)
};

class Engine
{
public:
    static constexpr int kOversample = 4;
    static constexpr int kChunk = 64; // the control rate's chunk (input samples)

    Engine ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return ownLatency () + tail.latency (); }
    int ownLatency () const { return osLatency; }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // In place is fine.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // for the tests: the leveller's gain at the end of the last block (linear)
    double levellerGain () const { return levelNow; }

private:
    void controlChunk (int m);
    void tapeFilters ();
    void cutBells (float* x, int c, int m);

    ParamArray p = defaultParams ();
    double sr = 48000.0, srOs = 192000.0;
    // smoothing
    double cSmooth = 0.001, cFade = 0.002;
    double tapeAmt = 1.0, cutAmt = 1.0, drive = 0.75, speed = 0.8, comp = 0.35, mixNow = 1.0;
    double cutGain[cut::kBands] {};
    float out = 1.0f;
    // per input sample of a chunk: the tape's and the Cut's amounts and the tape's gains
    double tapeAt[kChunk] {}, cutAt[kChunk] {}, gainAt[kChunk] {}, compAt[kChunk] {};
    // oversampling
    para::Oversampler4x os[2];
    int osLatency = 0;
    std::vector<float> up[2], wet[2];
    // the tape at 4x: the leveller's level and the curve's last input (the anti-aliasing)
    double env = 0.0, cAtk = 0.0, cRel = 0.0, levelNow = 1.0, peakNow = 0.0;
    double lastZ[2] {};
    // the tape at the base rate
    sweepr::dsp::SvfCoefs shelfC;
    sweepr::dsp::Svf shelf[2];
    double shelfHigh = 1.0, shelfMid = 1.0;
    double dcG = 0.0;
    sweepr::dsp::OnePole dc[2];
    double shownSpeed = -1.0;
    // the Cut EQ
    sweepr::dsp::SvfCoefs cutC[cut::kBands];
    sweepr::dsp::Svf cutF[cut::kBands][2];
    // the dry path, delayed as much as the wet one
    std::vector<float> dryBuf[2];
    int dryPos = 0, drySize = 0;
    Meters* meters = nullptr;
    smacheratr::Tail tail;
};

} // namespace tapr

// Sunder: a germanium compressor with a bass enhancer ahead of it or after it. The compressor is the Softube model of the
// Chandler Limited Germanium Compressor measured setting by setting (germcal: REAPER renders through the
// plug-in, fitted in Python), as a starting point; the enhancer is bubr's (BassEngine.h).
//
//   input -> [the bass enhancer: Bass, Bass Position Pre] -> Input -> [Mid/Side] -> 4x up -> the FET compressor
//         (feed-forward detector, CompShape.h, DetectorShape.h) -> Mix with the uncompressed signal -> the output
//         stage (Drive, Feedback: StageShape.h) -> 4x down -> [Mid/Side back] -> [the bass enhancer: Bass Position
//         Post] -> Output -> Soft Clip, Hard Clip (pluginkit/EndClip.h)
//
// The compressor. The detector hears the signal after Input (the same channel, or both with Link: the louder
// sets the gain), or with Ext SC the sidechain input (SC Gain; Mid/Side like the input): two first-order
// high-passes, SC Filter's shelf, a rectifier (the positive half), a compression (the knee's diodes ahead of the
// capacitor: short loud bursts charge it little), a peak follower (Attack charges it, Release discharges it)
// and a one-pole smoother, read back as a sine's level at the reference timing. That level reads the
// measured gain reduction of the Curve at the Ratio (positions 1 .. 10 of the eleven measured); the FET's
// gain is that, 1.52 dB below unity at rest, down to -25.2 dB. The FET's second harmonic is always in (pinchr's
// Dirty: the normal sound).
//
// Mix blends the compressed signal and the uncompressed one (1.52 dB louder, as in the model), before the
// output stage, so both have its colour. The output stage is linear: per Drive position a response fitted
// at every Feedback position (a gain, three resonant low sections, a high peak), interpolated along
// Feedback; Drive glides from one step's to the next. Drive and Feedback are steps 1 .. 10 spanning the
// model's positions 1 .. 15 (stagePosition): above the measured 10 the stage is extrapolated (stageAt).
//
// Saturation: the output stage's germanium transistor, an asymmetric soft curve (more second harmonic than
// third) inside a negative-feedback loop solved every sample. Feedback sets the loop's gain: low Feedback,
// much feedback, clean until the stage nears its headroom, then a firmer knee; high Feedback, little feedback,
// warm from much lower levels. Saturation sets the headroom (100 %: -12 dBFS at the stage's output; 0 %: off,
// the measured linear stage exactly). Small signals pass as the linear stage has them; the distortion's DC is
// blocked.
//
// The bass enhancer (Bass above 0) runs on the input before anything else (Pre, the default), or on the output
// stage's left and right ahead of Output and the clips (Post: the compressor does not hear it, Output and the
// clips take it in); at 0 nothing of it runs and the sound is the compressor's alone, to the bit, at either place
// (with Bass Oversample on, delayed by its latency). A move of Bass Position crossfades two compressor chains,
// one with the enhancer ahead of it and one with it after (kMoveHoldSec, then kMoveSec; with the enhancer silent
// and no latency both are the same, and it moves at once).
//
// Latency: the oversampler's, plus the bass enhancer's oversampler's (Bass Oversample: none with Off), the same
// at either place. It moves only when Bass Oversample does.
#pragma once

#include "BassEngine.h"
#include "Params.h"

#include "pluginkit/dsp/Oversampler4x.h"
#include "sweepr/src/core/Dsp.h"

#include <array>
#include <atomic>
#include <complex>
#include <vector>

namespace sunder {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// What the editor shows (written by the audio thread once per block).
struct Meters
{
    std::atomic<uint32_t> blocks {0};
    std::atomic<float> sampleRate {48000.0f};
    std::atomic<float> grDb[2] {0.0f, 0.0f}; // the gain reduction now, per channel (dB, 0 or below)
    std::atomic<float> levelDb {-120.0f};    // the detector's level (the table's input, dB)
    std::atomic<bool> sidechainConnected {false}; // (written by the processor)
};

// The output stage's parameters (StageShape.h) at Drive position d and Feedback position f (0 .. 15, pinchr's
// scale; fractional while they glide; above 10 extrapolated from the measured 7 .. 10), and its analog response
// at hz (the display, the tests).
struct StageParams
{
    double gainDb, fa, qa, fb, qb, fc, qc, fz, qz, fh, qh;
};
StageParams stageAt (double drive, double feedback);
std::complex<double> stageResponse (const StageParams& s, double hz);
// The measured static gain reduction (dB) for a steady sine at u dB into the detector (the reference timing),
// for a Curve and a Ratio (1 .. 11).
double staticGrDb (int curve, double ratio, double u);

class Engine
{
public:
    static constexpr int kOversample = 4;
    static constexpr int kChunk = 64; // the control rate's chunk (input samples)
    // Bass Position's crossfade, after the chain that comes in has run unheard for a moment
    static constexpr double kMoveSec = 0.03, kMoveHoldSec = 0.1;

    Engine ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return osLatency + bassPre.latency (); } // (the same at either place)
    void setMeters (Meters* m) { meters = m; }

    // In place is fine. scL / scR: the sidechain input (null when the host routes none; Ext SC uses it).
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const float* scL = nullptr,
                  const float* scR = nullptr);

    // for the tests: the gain reduction now (dB), the detector's level (dB); the model's positions instead of
    // the steps (Ratio 1 .. 11, Drive and Feedback 0 .. 15: what pinchr's tests measured), until setParam sets
    // one of them again; whether the bass enhancer ran in the last block
    double grDb (int c) const { return active ().grNow[c]; }
    double detectorDb () const { return active ().levelNow; }
    void setModelPositions (double ratioPos, double drivePos, double feedbackPos);
    bool bassRan () const { return target == kBassPre ? preActive : postActive; }
    // for the tests: Bass Position's crossfade under way
    bool moving () const { return xf != (double)target; }

private:
    struct Section
    {
        sweepr::dsp::SvfCoefs c;
        double mHp = 1.0, mBp = 0.0, mLp = 0.0; // the numerator (high-pass, band-pass, low-pass mix)
    };
    static constexpr int kSatHalf = 2048; // the saturation curve's table: -16 .. +16 times the headroom
    static constexpr double kSatStep = 16.0 / kSatHalf;

    // One compressor chain: Input, the detector, the FET, Mix, the output stage, Mid/Side (everything between
    // the bass enhancer's two places). There are two so that a move of Bass Position crossfades: one with the
    // enhancer ahead of it, one with it after.
    struct Comp
    {
        // smoothed controls
        double inGain = 1.0, scGain = 1.0, ratio = 4.0, attack = 3.0, release = 3.0, mix = 1.0, drive = 5.0, feedback = 5.0;
        // the detector (at 4x)
        double scPoleG = 0.0, scZeroW = 0.0, scPoleW = 1.0;
        sweepr::dsp::OnePole hp1[2], hp2[2], scLp[2];
        double peak[2] {}, smooth[2] {};
        double kAtk = 0.0, kRel = 0.0;
        double grNow[2] {}, levelNow = -120.0;
        // the output stage (at 4x)
        Section sec[4];
        sweepr::dsp::Svf svf[4][2];
        double stageGain = 1.0;
        // the germanium saturation: the loop gain, the headroom (linear) and per channel the DC
        double satAmt = 0.5, satLoop = 5.0, satHead = 1.0;
        double satTab[2 * kSatHalf + 1] {}, satTabLoop = -1.0;
        sweepr::dsp::OnePole satDc[2];
        double shownDrive = -1.0, shownFeedback = -1.0;
        // oversampling
        pk::dsp::Oversampler4x os[2], scOs[2]; // (the sidechain's: only up)
        std::vector<float> up[2], wet[2], scUp[2];
    };
    // what a block's chunks share (the targets the controls glide to)
    struct Block
    {
        double inT = 1.0, scT = 1.0, mixT = 1.0;
        bool ext = false, midSide = false, link = true;
        int curve = 0;
    };

    void prepareComp (Comp& k);
    void resetComp (Comp& k);
    void controlChunk (Comp& k);
    void stageCoefs (Comp& k);
    // one chunk (m samples at a, the input already through the enhancer when it is ahead) to out (left and
    // right, Mid/Side decoded, before Output)
    void compChunk (Comp& k, const float* inL, const float* inR, const float* sl, const float* sr2, int a, int m, double (*out)[kChunk]);
    // the enhancer at one place for a chunk: true when it wrote o (else the signal passes as it is)
    bool bassChunk (bass::Engine& b, bool on, const float* inL, const float* inR, float* oL, float* oR, int m);
    bool bassOn (const bass::Engine& b, bool active) const { return p[kBass] > 0.0 || (active && !b.dryOnly ()); }
    void startMove (int to);
    Comp& compFor (int pos) { return comp[pos == kBassPre ? preComp : 1 - preComp]; }
    const Comp& active () const { return comp[target == kBassPre ? preComp : 1 - preComp]; }

    void bassParam (uint32_t id);

    ParamArray p = defaultParams ();
    // the targets the controls glide to: the Ratio's, Drive's and Feedback's model positions
    double ratioT = 4.0, driveT = 5.0, feedbackT = 5.0;
    double sr = 48000.0, srOs = 192000.0;
    double cSmooth = 0.001;
    double hp1G = 0.0, hp2G = 0.0, satDcG = 0.0, kSm = 0.0;
    int osLatency = 0;
    Block blk;
    Comp comp[2];
    int preComp = 0; // the chain with the enhancer ahead of it (the other has it after)
    float out = 1.0f;
    // the bass enhancer, ahead of the compressor (Pre) and after it (Post); whether each ran in the last block
    bass::Engine bassPre, bassPost;
    bool preActive = false, postActive = false;
    // Bass Position: the place now (target) and the crossfade towards it (0 Pre .. 1 Post; it waits moveHold
    // samples first, while the chain that comes in settles unheard)
    int target = kBassPre;
    double xf = 0.0;
    int moveHold = 0;
    // the last input and the last compressor output (Mid/Side decoded), for the enhancer that comes in to start
    // its latency's delay from (Bass Oversample)
    static constexpr int kHist = 1024;
    float histIn[2][kHist] {}, histComp[2][kHist] {};
    int histPos = 0;
    Meters* meters = nullptr;
};

} // namespace sunder

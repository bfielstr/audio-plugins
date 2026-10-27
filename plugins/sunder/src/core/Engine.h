// Sunder: a germanium compressor with a bass enhancer ahead of it. The compressor is the Softube model of the
// Chandler Limited Germanium Compressor measured setting by setting (germcal: REAPER renders through the
// plug-in, fitted in Python), as a starting point; the enhancer is bubr's (BassEngine.h).
//
//   input -> [the bass enhancer: Bass] -> Input -> [Mid/Side] -> 4x up -> the FET compressor (feed-forward detector, CompShape.h,
//         DetectorShape.h) -> Mix with the uncompressed signal -> the output stage (Drive, Feedback:
//         StageShape.h) -> 4x down -> [Mid/Side back] -> Output -> Soft Clip, Hard Clip (pluginkit/EndClip.h)
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
// The bass enhancer (Bass above 0) runs on the input before anything else; at 0 nothing of it runs and the
// sound is the compressor's alone, to the bit (with Bass Oversample on, delayed by its latency).
//
// Latency: the oversampler's, plus the bass enhancer's oversampler's (Bass Oversample: none with Off). It
// moves only when Bass Oversample does.
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

    Engine ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return osLatency + bass.latency (); }
    void setMeters (Meters* m) { meters = m; }

    // In place is fine. scL / scR: the sidechain input (null when the host routes none; Ext SC uses it).
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const float* scL = nullptr,
                  const float* scR = nullptr);

    // for the tests: the gain reduction now (dB), the detector's level (dB); the model's positions instead of
    // the steps (Ratio 1 .. 11, Drive and Feedback 0 .. 15: what pinchr's tests measured), until setParam sets
    // one of them again; whether the bass enhancer ran in the last block
    double grDb (int c) const { return grNow[c]; }
    double detectorDb () const { return levelNow; }
    void setModelPositions (double ratioPos, double drivePos, double feedbackPos);
    bool bassRan () const { return bassActive; }

private:
    void controlChunk ();
    void stageCoefs ();

    struct Section
    {
        sweepr::dsp::SvfCoefs c;
        double mHp = 1.0, mBp = 0.0, mLp = 0.0; // the numerator (high-pass, band-pass, low-pass mix)
    };

    void bassParam (uint32_t id);

    ParamArray p = defaultParams ();
    // the targets the controls glide to: the Ratio's, Drive's and Feedback's model positions
    double ratioT = 4.0, driveT = 5.0, feedbackT = 5.0;
    double sr = 48000.0, srOs = 192000.0;
    double cSmooth = 0.001;
    // smoothed controls
    double inGain = 1.0, scGain = 1.0, ratio = 4.0, attack = 3.0, release = 3.0, mix = 1.0, drive = 5.0, feedback = 5.0;
    float out = 1.0f;
    // the detector (at 4x)
    double hp1G = 0.0, hp2G = 0.0, scPoleG = 0.0, scZeroW = 0.0, scPoleW = 1.0;
    sweepr::dsp::OnePole hp1[2], hp2[2], scLp[2];
    double peak[2] {}, smooth[2] {};
    double kAtk = 0.0, kRel = 0.0, kSm = 0.0;
    double grNow[2] {}, levelNow = -120.0;
    // the output stage (at 4x)
    Section sec[4];
    sweepr::dsp::Svf svf[4][2];
    double stageGain = 1.0;
    // the germanium saturation: the loop gain, the headroom (linear) and per channel the last solution and the DC
    double satAmt = 0.5, satLoop = 5.0, satHead = 1.0;
    static constexpr int kSatHalf = 2048;            // the curve's table: -16 .. +16 times the headroom
    static constexpr double kSatStep = 16.0 / kSatHalf;
    double satTab[2 * kSatHalf + 1] {}, satTabLoop = -1.0;
    sweepr::dsp::OnePole satDc[2];
    double satDcG = 0.0;
    double shownDrive = -1.0, shownFeedback = -1.0;
    // oversampling
    pk::dsp::Oversampler4x os[2], scOs[2]; // (the sidechain's: only up)
    int osLatency = 0;
    std::vector<float> up[2], wet[2], scUp[2];
    // the bass enhancer ahead of everything
    bass::Engine bass;
    bool bassActive = false;
    Meters* meters = nullptr;
};

} // namespace sunder

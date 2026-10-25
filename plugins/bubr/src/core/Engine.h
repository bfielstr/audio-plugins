// Bubr: a bass enhancer, BOOM Interactive's Uberbass measured setting by setting (bubrcal: REAPER renders
// through the plug-in, fitted in Python), as a 1:1 model to start from.
//
//   input -+-> the gate's detector (the input's peak) ---------------------------+
//          |                                                                       v
//          +-> the band: two band-passes (Freq, Q-Factor: BandShape.h), a tilt -> Intensity and Boost -> gate gain
//          |     -> the compressor (linked) -> [2x/4x up] the rectified band (a shelf) added -> the clipper
//          |     -> Drive [down] -> the tilt undone, two more band-passes, a gain (the ceiling at Freq stays put) -> Boost
//          |                                                                       |
//          +-> dry ------------------------------> Dry/Wet: dry and the effect, then Low Cut (blended) <-+
//                -> the output limiter (0 dBFS) -> Output -> Soft Clip -> Hard Clip
//
// The band. Four band-pass sections whose frequencies and Qs were fitted at every Freq (13) and Q-Factor (11)
// of the reference, interpolated between them (log Freq, Q-Factor). The first two are the focus, ahead of the
// clipper; the last two follow it. Their gain is split so that the clipper's ceiling, read at Freq, is the same
// for every Freq and Q-Factor (as measured): Q-Factor's extra gain and Intensity drive the clipper harder, they do
// not make the effect louder once it clips.
//
// Boost and the compressor. Boost adds 0.3 dB a percent, part ahead of the clipper and the rest after it. The
// part ahead is taken back as the band gets loud: a compressor on both channels together (the louder sets it;
// attack about 19 ms, release about 34 ms) whose range is exactly that part, so Boost lifts the quiet passages
// and leaves the loud ones their own level into the clipper (as measured with a quiet probe on one channel while
// the other played loud).
//
// The clipper. A biased soft clipper (a knee, second harmonic near the knee that fades in deep clipping), the
// band's full-wave rectified copy added in front of it through a low shelf (the even harmonics the reference
// has at every level), and a high-passed negative feedback around it that the output undoes (no change while it
// is linear; the phase lead the reference shows when it clips). Around it a second-order tilt (before it, undone
// after it) sets how the band's filtering splits between its two sides. Drive is a gain after it; x2 doubles
// Drive's stage (measured).
//
// The gate. The input's peak against Gate; below it the band is turned down (Ratio - 1) dB a dB, from about -100
// dB shut; its gain moves in the linear domain, up with Attack and down with Release (one pole each). It acts
// ahead of the clipper (a gated signal clips less).
//
// Dry/Wet (the reference's law, measured): from -100 % to 0 % the effect comes in (its own curve) while the
// whole mix is blended into Low Cut's high-pass (whose Q rises with it); at 0 % both at full level through Low
// Cut; to +100 % the dry fades out (its own curve). Low Cut is two second-order high-passes (Q 0.8785).
//
// The output limiter: 0 dBFS, instant (nothing passes above it); a fast envelope and a slower one with a hold,
// the larger sets the gain reduction. Lookahead ramps it in over 1 ms (that latency). Output follows it (so it
// can only make it quieter), then the clips.
//
// Latency: the oversampler's (2x, 4x; the dry path is delayed to match) and the lookahead's.
#pragma once

#include "Params.h"

#include "para/src/core/Oversampler.h"
#include "sweepr/src/core/Dsp.h"

#include <array>
#include <atomic>
#include <complex>
#include <vector>

namespace bubr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// What the editor shows (written by the audio thread once per block).
struct Meters
{
    std::atomic<uint32_t> blocks {0};
    std::atomic<float> sampleRate {48000.0f};
    std::atomic<float> inDb {-120.0f};   // the gate's detector (the input's peak over the block, dB)
    std::atomic<float> gateDb {0.0f};    // the gate's gain now (dB, 0 or below)
    std::atomic<float> wetDb {-120.0f};  // the effect's peak over the block (dB)
    std::atomic<float> limitDb {0.0f};   // the output limiter's gain reduction now (dB, 0 or below)
};

// The band's sections for a Freq (Hz) and a Q-Factor (0 .. 1): four (frequency, Q) pairs, low to high, and the
// gain (BandShape.h interpolated).
struct Band
{
    double f[4], q[4], gain;
};
Band bandAt (double freq, double qFactor);
// The effect's linear response at hz for these settings (the display, the tests): the four sections, the gains
// and Low Cut, at a sample rate (the sections are bilinear), Dry/Wet at +100 %.
std::complex<double> wetResponse (const ParamArray& p, double hz, double sampleRate);

class Engine
{
public:
    static constexpr int kChunk = 32; // the control rate's chunk (input samples)
    static constexpr int kMaxOs = 4;

    Engine ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    int latency () const;
    void setMeters (Meters* m) { meters = m; }

    // In place is fine.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // for the tests
    double gateGain (int c) const { return gateG[c]; }
    double limiterDb () const { return limGrNow; }

private:
    void controlChunk ();
    void bandCoefs ();
    void processWet (const double (*in)[kChunk], double (*out)[kChunk], int m, const double* gate);
    void clipAndPost (float* u, double* out, int c, int m);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    double cSmooth = 0.001;
    int osFactor = 1, osNow = -1;
    // smoothed controls
    double freq = 40.0, qf = 0.2, intensity = 0.5, boost = 0.55, drive = 0.2, x2 = 0.0, lowCut = 20.0, dryWet = 0.0;
    double outGain = 1.0;
    double shownFreq = -1.0, shownQ = -1.0, shownLowCut = -1.0, shownMix = -2.0;
    // the band (at the base rate) and after the clipper
    sweepr::dsp::SvfCoefs bandC[4];
    sweepr::dsp::Svf band[4][2];
    double gPre = 1.0, gPost = 1.0, gDrive = 1.0, bandGain = -1.0, bandPostNorm = 1.0;
    // the tilt around the clipper: a second-order pole-zero pair before it (en / ed), undone after it (ed / en)
    double en[3] {1.0, 0.0, 0.0}, ed[3] {1.0, 0.0, 0.0};
    double eX[2][2] {}, eY[2][2] {}, iX[2][2] {}, iY[2][2] {};
    // the compressor (linked; at the base rate)
    double compEnv = 0.0, compAtk = 0.0, compRel = 0.0, compRange = 0.0, compDet = 1.0, compGrNow = 1.0;
    // the clipper (at the oversampled rate)
    double tB0 = 1.0, tB1 = 0.0, tA1 = 0.0; // the rectified band's shelf
    double tZ[2] {}, tY[2] {};
    double fbA = 0.0, fbL[2] {}, fbH[2] {};
    para::Halfband2xPoly os2[2];
    para::Oversampler4x os4[2];
    std::vector<float> upBuf[2];
    // the gate
    double gateG[2] {1.0, 1.0}, gateThr = 0.0316, gateExp = 3.0, gAtk = 1.0, gRel = 0.0;
    // Dry/Wet and Low Cut
    sweepr::dsp::SvfCoefs lcC;
    sweepr::dsp::Svf lc[2][2];
    double mixK = 1.0, dryG = 1.0, wetG = 1.0;
    std::vector<float> dryDelay[2];
    int dryLen = 0, dryPos = 0;
    // the output limiter
    double limFast[2] {}, limSlow[2] {};
    int limHold[2] {};
    double limAf = 0.0, limAa = 0.0, limAr = 0.0, limGrNow = 0.0;
    int limHoldN = 0, look = 0, lookNow = 0;
    std::vector<float> lookBuf[2];
    std::vector<double> grBuf[2];
    int lookPos = 0;
    double grSum[2] {};
    Meters* meters = nullptr;
};

} // namespace bubr

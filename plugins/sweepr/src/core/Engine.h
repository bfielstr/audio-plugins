// Sweepr: two bells whose frequencies an LFO each sweeps, into tape.
//
//   input -> bell A -> bell B -> 4x up -> tape (Drive, the leveller, the sine clipper) -> 4x down -> tape
//         (DC blocker, the Speed shelf) -> Mix (with the input delayed as much) -> Output -> Soft Clip, Hard
//         Clip (pluginkit/EndClip.h)
//
// The bells. Each is Reason's PEQ-2 bell (BellShape.h: its knobs' ranges and how Q and Gain shape it,
// measured) as a trapezoidal state-variable filter, prewarped at its frequency. The filter runs at the
// sample rate, as PEQ-2's does (so a bell high up cramps towards Nyquist the same way), and its
// coefficients are worked out again every input sample from the swept frequency and the smoothed Gain and
// Q: the SVF's state stays valid under any coefficient change, so fast sweeps neither zipper nor click.
//
// The LFOs. Each band's LFO moves its Freq by Depth octaves (bipolar: up and down from the knob's setting).
// Synced, its phase is a function of the song position (beats / the division's length + Phase), so playback
// from any point sounds the same and nothing retriggers; with the host stopped it runs on from where it was
// at the last tempo. Free, it runs at Rate Hz from Phase. Random shapes take a value per cycle from the
// cycle's number, so they too repeat with the song. Link makes B's LFO A's (its sync, rate and shape), with
// B's Phase as the offset from A and B's own Depth. The modulation glides over 2 ms, so a Square or a
// Random step sweeps quickly instead of jumping.
//
// The tape stage (Tape on; switching fades over 10 ms): Scream 4's Tape mode, measured (TapeShape.h). At 4x:
// Drive's gain (-10 to +50 dB), a stereo-linked peak leveller (Compression: 1 / (1 + 3.21 Compression level),
// 2 ms attack, 21 ms release) and a sine clipper (flat at +-0.5, anti-aliased with its antiderivative). At
// the base rate: a 10 Hz DC blocker and Speed's high shelf (-48 dB above 2 kHz at 0 to -2 dB above 11 kHz at
// 100 %). No makeup: like Scream's, the output settles near the clipper's level whatever goes in.
//
// Latency: the oversampler's (constant whatever the settings). Mix blends with the input delayed by the same amount; Mix 0 is the delayed input bit for bit.
#pragma once

#include "Dsp.h"
#include "Params.h"
#include "TapeShape.h"

#include "pluginkit/dsp/Oversampler4x.h"

#include <array>
#include <atomic>
#include <vector>

namespace sweepr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// What the editor shows (written by the audio thread once per block).
struct Meters
{
    std::atomic<uint32_t> blocks {0};
    std::atomic<float> sampleRate {48000.0f};
    std::array<std::atomic<float>, kBands> freqHz {}; // each bell's frequency now (swept)
    std::array<std::atomic<float>, kBands> lfo {};    // each LFO's value now (-1 .. 1)
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
    // The song position at the start of the next block: tempo (BPM, <= 0: unknown, the last one is kept),
    // ppq (quarter notes) and whether the host plays (then the synced LFOs follow ppq; else they run on).
    void setTransport (double tempo, double ppq, bool playing);
    int latency () const { return ownLatency (); }
    int ownLatency () const { return osLatency; }
    void setMeters (Meters* m) { meters = m; }

    // In place is fine.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // for the tests and the display
    double bandFreqNow (int b) const { return fNow[b]; } // the swept frequency at the end of the last block
    double lfoNow (int b) const { return lfoVal[b]; }
    double beatsNow () const { return beatPos; }

private:
    struct Band
    {
        double logF = 0.0, logQ = 0.0, gainDb = 0.0; // smoothed
        double modOct = 0.0;                        // the LFO's octaves, glided
        double freeCycles = 0.0;                    // the free LFO's position (cycles, the fraction)
        int64_t freeIndex = 0;                      // and its whole cycles
        dsp::Svf svf[2];
    };
    void controlChunk (int m);
    void tapeFilters ();
    double lfoValue (int b, double phaseOffsetDeg) const;

    ParamArray p = defaultParams ();
    double sr = 48000.0, srOs = 192000.0;
    Band band[kBands];
    double fNow[kBands] {}, lfoVal[kBands] {};
    // the transport
    double bpm = 120.0, beatPos = 0.0;
    bool playing = false;
    // smoothing
    double cSmooth = 0.001, cMod = 0.02, cFade = 0.002;
    double tapeAmt = 1.0, drive = 0.75, speed = 0.8, comp = 0.35, mixNow = 1.0;
    float out = 1.0f;
    // per input sample of a chunk: the bells' coefficients, the tape's amount and gains
    dsp::SvfCoefs coef[kBands][kChunk];
    double tapeAt[kChunk] {}, gainAt[kChunk] {}, compAt[kChunk] {};
    // oversampling
    pk::dsp::Oversampler4x os[2];
    int osLatency = 0;
    std::vector<float> up[2], wet[2];
    // the tape at 4x: the leveller's level and the curve's last input (the anti-aliasing)
    double env = 0.0, cAtk = 0.0, cRel = 0.0;
    double lastZ[2] {};
    // the tape at the base rate
    dsp::SvfCoefs shelfC;
    dsp::Svf shelf[2];
    double shelfHigh = 1.0, shelfMid = 1.0;
    double dcG = 0.0;
    dsp::OnePole dc[2];
    double shownSpeed = -1.0;
    // the dry path, delayed as much as the wet one
    std::vector<float> dryBuf[2];
    int dryPos = 0, drySize = 0;
    Meters* meters = nullptr;
};

} // namespace sweepr

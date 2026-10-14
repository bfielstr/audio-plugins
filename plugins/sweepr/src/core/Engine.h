// Sweepr: two bells whose frequencies an LFO each sweeps, into tape.
//
//   input -> bell A -> bell B -> 4x up -> tape curve (compression, pre-emphasis, drive, hysteresis-like
//         saturator, de-emphasis, makeup) -> 4x down -> tape filters (head bump, high roll-off, DC) -> wow
//         -> Mix (with the input delayed as much) -> Output -> Smacheratr (the end saturator, off by default)
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
// The tape stage (Tape on; switching fades over 10 ms). At 4x: a stereo-linked peak compressor
// (Compression: more squash before the curve), a +6 dB high shelf, the Drive gain (-6 to +24 dB) into an
// asymmetric tanh (a bias: even harmonics; anti-aliased with its antiderivative) with a hysteresis-like
// memory (the "magnetisation" follows the curve at a rate that grows with how fast the field moves, so the
// transfer traces a loop that depends on the level), the inverse shelf (the highs, driven harder, come back softer) and a makeup gain worked out
// so a -15 dBFS sine comes out as loud as it went in at any Drive and Compression. At the base rate: a head
// bump (30 Hz at Speed 0 to 100 Hz at 100 %, up to +4.5 dB, with a dip above it), a 12 dB / octave
// roll-off (6 kHz at Speed 0 to 22 kHz at 100 %), a 10 Hz DC blocker and wow and flutter (a modulated
// delay, none at Speed 100 %, about 0.2 % of pitch at 0 %).
//
// Latency: the oversampler's plus the wow delay's centre (constant whatever the settings), plus the end
// saturator's. Mix blends with the input delayed by the same amount; Mix 0 is the delayed input bit for bit.
#pragma once

#include "Dsp.h"
#include "Params.h"

#include "para/src/core/Oversampler.h"
#include "smacheratr/src/core/Tail.h"

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
    int latency () const { return ownLatency () + tail.latency (); }
    int ownLatency () const { return osLatency + wowCentre; }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // In place is fine.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // for the tests and the display
    double bandFreqNow (int b) const { return fNow[b]; } // the swept frequency at the end of the last block
    double lfoNow (int b) const { return lfoVal[b]; }
    double beatsNow () const { return beatPos; }
    static double driveDb (double drive) { return -6.0 + 30.0 * drive; }
    // the makeup gain the tape stage applies at Drive d and Compression c (linear)
    static double tapeMakeup (double d, double c);
    static double compAmount (double c) { return 8.0 * c; }
    static double biasOf (double d) { return 0.25 * d; }
    // the tape filters at Speed s
    static double bumpHz (double s) { return 30.0 * std::pow (100.0 / 30.0, s); }
    static double bumpDb (double s) { return 1.5 + 3.0 * (1.0 - s); }
    static double rollOffHz (double s) { return 6000.0 * std::pow (22000.0 / 6000.0, std::pow (std::clamp (s, 0.0, 1.0), 0.7)); }
    static double wowAmount (double s) { return std::pow (std::clamp (1.0 - s, 0.0, 1.0), 1.5); }

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
    double tapeAt[kChunk] {}, gainAt[kChunk] {}, makeupAt[kChunk] {}, biasAt[kChunk] {}, tanhBiasAt[kChunk] {}, compAt[kChunk] {},
        holdAt[kChunk] {};
    double makeupNow = 1.0, makeupFor[2] {-1.0, -1.0};
    // oversampling
    para::Oversampler4x os[2];
    int osLatency = 0;
    std::vector<float> up[2], wet[2];
    // the tape at 4x
    double env = 0.0, cAtk = 0.0, cRel = 0.0, cHyst = 0.0;
    double emphG = 0.0, deemphG = 0.0;
    dsp::OnePole emph[2], deemph[2];
    double mag[2] {}, lastH[2] {};
    // the tape at the base rate
    dsp::SvfCoefs bumpC, dipC, lpC;
    dsp::Svf bump[2], dip[2], hf[2];
    double dcG = 0.0;
    dsp::OnePole dc[2];
    double shownSpeed = -1.0;
    // wow: a modulated delay around its centre
    std::vector<float> wowBuf[2];
    int wowCentre = 0, wowPos = 0, wowSize = 0;
    double wowPh = 0.0, driftPh = 0.0, flutterPh = 0.0;
    // the dry path, delayed as much as the wet one
    std::vector<float> dryBuf[2];
    int dryPos = 0, drySize = 0;
    Meters* meters = nullptr;
    smacheratr::Tail tail;
};

} // namespace sweepr

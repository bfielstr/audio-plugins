// The cinema stage's element lanes: the input separated into five parts that sum back to it exactly.
//
// An STFT (1024 points at 44.1 / 48 kHz, 2048 at 88.2 / 96 kHz, 4096 above; hop half of it; square
// root Hann windows for analysis and synthesis, weighted overlap-add) looks at each bin of each frame:
//   Voice     the part of the mid common to L and R (a least-squares / Wiener centre estimate from the
//             smoothed cross-spectrum, Re(L R*) / |M|^2), kept where the bin's level moves at a syllable
//             rate (band-passed at 2 .. 8 Hz, it swings by several dB), rises over the bin's steady floor
//             (minimum statistics over ~0.5 s: a held chord under the voice stays out), is not a hit and
//             lies in the voice band (120 Hz .. 7 kHz). Voice is mono: the same on both sides.
//   Bass      what Voice leaves below ~100 Hz, fading out by 160 Hz.
//   Hits      onsets: a bin rising well over the median of its last ~50 ms (harmonic / percussive
//             separation by median filtering, after Fitzgerald 2010, made causal), averaged over +-7 bins
//             (a hit is broadband) and held while it rings down (40 ms).
//   Tones     what is steady: pads, leads, chords.
//   Ambience  diffuse bins (low inter-channel coherence) and noise-like ones (the level moves from frame
//             to frame without a steady floor): room, reverb, noise. It is the input minus the four
//             others, so the five always sum to the (delayed) input, within float rounding.
// Every mask is soft: per bin the Bass, Hits, Tones and Ambience masks sum to 1 over what Voice leaves.
//
// The engine does not need the five lanes one by one, only what its Positions make of them, so per frame
// the splitter mixes (by each lane's Position and Width, set per block) and returns, besides the input
// delayed: the Voice lane, the side the Centre lanes lose, the Bass lane's mid (for Depth), and the feeds
// of the Wide and Beyond lanes (their mid, by Width, for the voices, the hall and the Beyond cues) with
// the Beyond lanes' side (for their crosstalk cue). The feeds give way where the voice is (per bin, and
// by up to 10 dB across the voice band while it speaks) and where a hit is: the voice guard and the hit
// guard, so what the separation leaves of a voice or a hit in another lane is not widened. The full five
// lanes are there too for the tests (setFullLanes).
// Latency: the FFT size less one sample (1023 at 48 kHz, 21.3 ms).
#pragma once

#include "Params.h"

#include "locus/src/core/Fft.h"

#include <array>
#include <complex>
#include <vector>

namespace widr {

struct LaneSample
{
    float inL = 0.0f, inR = 0.0f; // the input, delayed by the latency
    float voice = 0.0f;           // the Voice lane (mono)
    float sideCut = 0.0f;         // the side the Centre lanes lose at Cinema 100 % (by 1 - their Width)
    float bassMid = 0.0f;         // the Bass lane's mid
    float feedWide = 0.0f, feedBeyond = 0.0f; // the Wide / Beyond lanes' mids by Width (guarded)
    float beyondSide = 0.0f;      // the Beyond lanes' side by Width
    // with setFullLanes (true): each lane, left and right (Voice: voice on both)
    std::array<float, kNumLanes> l {}, r {};
};

class LaneSplitter
{
public:
    void prepare (double sampleRate);
    void reset ();
    int latency () const { return n - 1; }
    int fftSize () const { return n; }
    int bins () const { return nb; }

    // Per block: each lane's Position and Width (0 .. 1).
    void setLanes (const std::array<int, kNumLanes>& position, const std::array<float, kNumLanes>& width)
    {
        pos = position;
        wid = width;
    }
    // The tests: also return the five lanes one by one (costs six more inverse FFTs).
    void setFullLanes (bool on) { full = on; }

    // One stereo sample in, the sample `latency ()` ago out.
    void tick (float l, float r, LaneSample& out);

    // Smoothed energy per lane (linear, the sum of both channels' power), for the display.
    const std::array<float, kNumLanes>& levels () const { return level; }
    // The last frame's masks, for the tests: the Voice gain on the mid per bin, then the share of what
    // is left for each other lane.
    const float* mask (int lane) const { return masks[(size_t)lane].data (); }
    uint64_t frames () const { return frameCount; } // frames analysed (a test reads the masks of each)
    const std::vector<float>& window () const { return win; }
    // The tests: record every frame's masks into `to` (allocates), or use recorded ones instead of the
    // signal's own (nullptr: neither), to see where each part of a mix goes.
    void recordMasks (std::vector<float>* to) { record = to; }
    void replayMasks (const std::vector<float>* from)
    {
        replay = from && from->size () >= kRec ? from : nullptr;
        replayPos = 0;
    }

private:
    void frame ();

    using cf = std::complex<float>;
    double sr = 48000.0;
    int n = 1024, hop = 512, nb = 513, mask_ = 1023, count = 0;
    float olaScale = 1.0f;
    uint64_t t = 0, frameCount = 0;
    locus::Fft fft {1024};
    std::vector<float> win, inL, inR, buf;
    std::vector<cf> xl, xr;
    // overlap-add outputs: Voice, the side cut, the Bass mid, the Wide feed, the Beyond feed, the Beyond
    // side; then (full lanes) Bass, Hits, Tones left and right
    enum Out { kVoice = 0, kSideCut, kBassMid, kFeedWide, kFeedBeyond, kBeyondSide, kNumMixes, kFullBassL = kNumMixes, kNumOut = kNumMixes + 6 };
    std::array<std::vector<float>, kNumOut> acc;
    std::array<std::vector<cf>, kNumOut> outSpec;
    std::array<int, kNumLanes> pos {kPosCentre, kPosCentre, kPosCentre, kPosWide, kPosBeyond};
    std::array<float, kNumLanes> wid {};
    bool full = false;
    // per bin
    std::vector<float> pLL, pRR, pMM, pLRre, pLRim;  // smoothed (cross-)spectra
    std::vector<float> bp1, bp2, modPow;             // syllable-rate modulation of the level
    std::vector<float> gVoice, voiceNear;            // the Voice gain, held where the voice is (the guard)
    std::vector<float> onset, perc, percHold;        // onsets, spread over the bins, held
    std::vector<float> stab, lmPrev;                 // frame-to-frame movement of the level (dB)
    std::vector<float> aSm, floorNow, curMin, subMin; // the steady floor (minima over kSub sub-windows)
    std::vector<float> hist;                         // the last kHist frames' magnitudes
    std::vector<float> bassW, voiceW;                // frequency weights
    std::array<std::vector<float>, kNumLanes> masks;
    int histPos = 0, subPos = 0, subCount = 0, subLen = 12;
    double bpB0 = 0, bpB2 = 0, bpA1 = 0, bpA2 = 0;
    float aSpec = 0.2f, aMod = 0.1f, aStab = 0.2f, aSmooth = 0.5f, aVoiceUp = 0.5f, aVoiceDown = 0.2f, aNear = 0.1f, aAct = 0.1f,
          percRel = 0.9f, aLevel = 0.1f, voiceAct = 0.0f;
    std::array<float, kNumLanes> level {};
    std::vector<float>* record = nullptr;
    const std::vector<float>* replay = nullptr;
    size_t replayPos = 0;
    static constexpr size_t kRec = 5; // floats recorded per bin
    static constexpr int kHist = 5, kVert = 7, kSub = 4;
};

} // namespace widr

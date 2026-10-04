// Gentlr: Smacheratr's Gentlr on its own, without the saturation curve around it. Up to two bands
// (smacheratr/src/core/ClarityBand.h, around each band's frequency: 12 dB/oct below and above, or
// with the Slope Signature 24 / 12 or Classic 12 / 6), the Sub band (a shelf from the very bottom up to where its cut starts to let go:
// smacheratr::subBand) and the High band (its mirror, a shelf from where its cut starts to let go up
// to the very top: smacheratr::highBand), each a gentle compressor on its region: when the band's level goes over the
// threshold (-18 dB, or the band's Threshold with Advanced on) the band is turned down, 3 dB for every
// 5 over, at most by its Range (smacheratr::clarityCutDb). The bands work one after the other (band 1,
// band 2, Sub, High, as in Smacheratr), each measuring its own band: x + (g - 1) * band, so with no cut
// a band leaves the signal exactly as it was. Glued bands keep their shared border, and with No Overlap
// on the working bands are kept apart, before they are designed (smacheratr::applyGlue and
// resolveOverlaps: the editor writes both as the bands are dragged, so these only act on automation,
// or a band switched on, that moves a glued border or makes bands overlap).
//
//   input -> [to mid / side] -> band 1 -> band 2 -> Sub -> High (each cut) -> [back to left / right] -> delay
//                                    \ the cut bands -> 4x up -> region Drive -> 4x down -> added
//         -> Mix (against the input, delayed the same) -> Output -> Smacheratr at the end
//
// Advanced's region Drive puts the cut bands through Smacheratr's Analog curve on their own, level
// matched (smacheratr::clarityRegionDrive: denser, not louder), oversampled 4x. The oversampler's
// delay is always in the path (the dry signal is delayed to match), so the latency never changes.
// The detector follows each band's peak level (a sine's peak from its mean square), with Attack and
// Release (Smacheratr's 15 and 150 ms by default). Stereo: left and right share one detector (the
// image stays put); Mid/Side works on the mid and the side, each with its own; Mid or Side works on
// that one only.
//
// On its own (another plug-in hosting it, e.g. a rack): Engine (false) (no end saturator), prepare,
// setParam (Gentlr's IDs below kTailBase, plain values), process; latency () is fixed per sample rate.
#pragma once

#include "Params.h"

#include "pluginkit/ScopeBuffer.h"
#include "smacheratr/src/core/Biquad.h"
#include "smacheratr/src/core/Engine.h"
#include "smacheratr/src/core/Oversampler.h"
#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>
#include <vector>

namespace gentlr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// For the editor (written by the audio thread).
struct Meters
{
    pk::ScopeBuffer<8192> scope;           // mono input (a) and output (b), for the analyser
    smacheratr::Meters bands;              // each band's cut (clarityDb, clarity2Db, claritySubDb, clarityHighDb) and level (clarityLevelDb, ...)
    std::atomic<uint32_t> blocks {0};      // counts processed blocks
    std::atomic<float> sampleRate {48000.0f};
};

class Engine
{
public:
    // withTail: the end-of-chain Smacheratr (off where Gentlr is built into another plug-in, and in tests)
    explicit Engine (bool withTail = true) : hasTail (withTail) {}
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    // Depends on the sample rate only (the region Drive's oversampler, and the end Smacheratr's).
    int latency () const { return chan[0].os.latency () + (hasTail ? tail.latency () : 0); }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // In place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    static constexpr int kChunk = 256;
    static constexpr int kCtrl = 16; // samples between updates of the cuts
    struct Delay
    {
        std::vector<float> buf;
        int pos = 0;
        void resize (int n)
        {
            buf.assign ((size_t)std::max (0, n), 0.0f);
            pos = 0;
        }
        void reset ()
        {
            std::fill (buf.begin (), buf.end (), 0.0f);
            pos = 0;
        }
        float push (float x)
        {
            if (buf.empty ())
                return x;
            const float y = buf[(size_t)pos];
            buf[(size_t)pos] = x;
            if (++pos >= (int)buf.size ())
                pos = 0;
            return y;
        }
    };
    struct Channel
    {
        smacheratr::Biquad hp[kAllBands], lp[kAllBands]; // the bands (Sub, High: their filters as smacheratr::ClarityBand has them)
        smacheratr::Biquad hp2[kAllBands];               // a band's second section below (Signature's 24 dB/oct: hp2On)
        smacheratr::Oversampler os;                // the region Drive
        Delay dryDelay, wetDelay;
        void reset ();
    };
    void processChunk (const float* inL, const float* inR, float* outL, float* outR, int n); // n <= kChunk
    void retune (bool force, const bool* works); // works: which bands work (No Overlap keeps those apart)
    void resetBand (int k); // its filters and detectors, from silence

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    Channel chan[2];
    int mode = kStereoLinked; // the stereo mode in use (a new one waits for the fade, see processChunk)
    // the detector: each band's level (mean square of its peak), per channel (channel 0's for both
    // while linked), the cut it asks for and the band's (smoothed) gain
    double env[2][kAllBands] {}, atk = 0.0, rel = 0.0;
    float cutDb[2][kAllBands] {}, gBand[2][kAllBands] {};
    int ctrlCountdown = 0;
    float gTarget[2][kAllBands] {};
    double bandFreq[kAllBands] = {-1.0, -1.0, -1.0, -1.0}, bandWidth[kAllBands] = {-1.0, -1.0, -1.0, -1.0};
    int bandSlope[kAllBands] = {-1, -1, -1, -1};             // the Slope each band was designed with
    bool hp2On[kAllBands] = {false, false, false, false};    // runs its second section below
    float bandNorm[kAllBands] = {1.0f, 1.0f, 1.0f, 1.0f};
    // a band runs while it works (on, Range above 0) and, after it stops, until its cut has let go
    bool running[kAllBands] = {false, false, false, false};
    // how much of Gentlr is in (1 normally): faded out and back in around a change of stereo mode
    float fx = 1.0f, fxStep = 0.0f;
    int hold = 0; // samples left, faded out, before the new mode starts
    // the region Drive: its gain and how much of it is in (both smoothed), the cut bands per channel
    float regionGain = 1.0f, regionMix = 0.0f;
    float region[2][kChunk] {}, regionOut[2][kChunk] {}, dryBuf[2][kChunk] {}, wetBuf[2][kChunk] {}, inMono[kChunk] {};
    float gRegion[kChunk] {}, gRegionMix[kChunk] {};
    std::vector<float> osBuf;
    float out = 1.0f, mix = 1.0f, smooth = 0.0f, smoothCut = 0.0f;
    smacheratr::Tail tail;
    bool hasTail = true;
    Meters* meters = nullptr;
};

} // namespace gentlr

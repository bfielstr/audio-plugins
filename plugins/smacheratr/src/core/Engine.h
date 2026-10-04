// A saturator in the spirit of Live's Saturator, with its Analog curve.
//
//   input -> [DC filter] -> [pre-limiter] -> Drive -> [colour pre-EQ] -> Analog curve
//         -> [colour post-EQ] -> [post clip] -> Dry/Wet -> Output
//
// The optional pre-limiter holds the input at its threshold with a 1 ms look-ahead, so a transient
// cannot push further into the curve than the rest of the sound: the drive is applied after it.
// Everything between the Drive gain and the post clip runs oversampled, 2x or 4x (Oversampling; 4x by
// default), or at the plug-in's rate with Oversampling off. The look-ahead delay is always in the path
// and the dry signal is delayed to match, so the latency reported to the host is the look-ahead's plus
// the oversampler's (none with Off, most of 4x's at 2x: Oversampler.h) and never changes while a
// setting is held. A new setting takes effect at the start of the next block: the wet path starts
// again from silence (a short gap), the dry delay takes the new length and latency () reports it.
//
// Gentlr (called Clarity before) works between the Drive and the curve: see process() and
// ClarityBand.h. Its Advanced mode gives each band a Threshold (clarityCutDb in Params.h) and can
// drive the region it cuts: the cut bands are split out again (the same band filters) and put
// through the Analog curve on their own, level-matched (clarityRegionDrive in ClarityBand.h),
// oversampled with the rest (at the same factor), so the region gets denser without the latency
// changing. With Advanced off none of that runs and the output is what it was before it. Glued bands
// keep their shared border (applyGlue, Glue.h), and with No Overlap on the working bands are kept apart
// (resolveOverlaps, NoOverlap.h), before they are designed.
#pragma once

#include "Biquad.h"
#include "Oversampler.h"
#include "Params.h"
#include "Shaper.h"

#include <array>
#include <atomic>
#include <vector>

namespace smacheratr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// Levels for the editor (written by the audio thread once per block, linear): the peak of the
// driven input, which is what enters the curve, and the peak of the shaped output.
struct Meters
{
    std::atomic<float> inPeak {0.0f};
    std::atomic<float> outPeak {0.0f};
    std::atomic<float> clarityDb {0.0f};  // Gentlr's cut before the curve (dB, 0 or less)
    std::atomic<float> clarity2Db {0.0f}; // its second band's
    // Gentlr's bands' levels as it measures them (the band's peak level into the curve, dB; -120
    // while the band is off), for the Threshold sliders
    std::atomic<float> clarityLevelDb {-120.0f};
    std::atomic<float> clarity2LevelDb {-120.0f};
    std::atomic<float> claritySubDb {0.0f};        // the Sub band's cut
    std::atomic<float> claritySubLevelDb {-120.0f}; // and its level
    std::atomic<float> clarityHighDb {0.0f};         // the High band's cut
    std::atomic<float> clarityHighLevelDb {-120.0f}; // and its level
    // the latency the engine runs at (samples; -1 before it is prepared): the look-ahead's and the
    // oversampler's, which changes with Oversampling (a controller watching it tells the host:
    // pk::ControllerBase::watchLatency)
    std::atomic<int> latency {-1};
};

// Band k's cut and level meters (Gentlr's order: the two bands, Sub, High).
inline const std::atomic<float>& clarityCutMeter (const Meters& m, int k)
{
    return k == 0 ? m.clarityDb : k == 1 ? m.clarity2Db : k == 2 ? m.claritySubDb : m.clarityHighDb;
}
inline const std::atomic<float>& clarityLevelMeter (const Meters& m, int k)
{
    return k == 0 ? m.clarityLevelDb : k == 1 ? m.clarity2LevelDb : k == 2 ? m.claritySubLevelDb : m.clarityHighLevelDb;
}

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain) { p[id] = plain; }
    double param (uint32_t id) const { return p[id]; }
    // The look-ahead's and the oversampler's at the Oversampling set (the pre-limiter on or off, the
    // Dry/Wet at 0 or not: the same). Set with setParam, it is what the next block runs at.
    int latency () const { return latencyAt (oversamplingFactor (p[kOversampling])); }
    // at an oversampling factor (1, 2 or 4)
    int latencyAt (int factor) const { return chan[0].os.latency (factor) + look; }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
    // (Hard Clip's ceiling at the very end: see Engine.cpp)
    void clipCeiling (float* outL, float* outR, int n) const;

    // Optional destination for level snapshots (may be null).
    void setMeters (Meters* m)
    {
        meters = m;
        publishLatency ();
    }

private:
    static constexpr int kMaxLook = 256; // 1 ms up to 256 kHz
    // A delay of len samples, up to the capacity given to resize (setLength allocates nothing).
    struct Delay
    {
        std::vector<float> buf;
        int pos = 0, len = 0;
        void resize (int capacity)
        {
            buf.assign ((size_t)std::max (0, capacity), 0.0f);
            pos = 0;
            len = (int)buf.size ();
        }
        void setLength (int n)
        {
            len = std::clamp (n, 0, (int)buf.size ());
            reset ();
        }
        void reset ()
        {
            std::fill (buf.begin (), buf.end (), 0.0f);
            pos = 0;
        }
        float push (float x)
        {
            if (len <= 0)
                return x;
            const float y = buf[(size_t)pos];
            buf[(size_t)pos] = x;
            if (++pos >= len)
                pos = 0;
            return y;
        }
    };
    struct Channel
    {
        Biquad dc, preLo, preHi, postLo, postHi;
        Biquad bandHp[kGentlrBands], bandLp[kGentlrBands], postHp[kGentlrBands], postLp[kGentlrBands]; // Clarity's bands, before and after the curve
        Biquad bandHp2[kGentlrBands], postHp2[kGentlrBands]; // their second section below (Signature's 24 dB/oct; bandHp2On)
        Oversampler os, regionOs; // regionOs: Gentlr's driven region, oversampled beside the rest
        Delay dryDelay, lookDelay;
        void reset ();
    };
    void updateFilters (bool force);
    void applyOversampling (); // a new Oversampling setting: the delays and the wet path, from silence
    void publishLatency ();
    float shapeChain (Channel& c, float v, bool color, int post) const;

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int maxBlock = 512, look = 48;
    int osFactor = 4; // the oversampling the engine runs at (1, 2 or 4: applyOversampling)
    Channel chan[2];
    std::vector<float> dry[2], pre[2], wet, osBuf, gDrive, gOut, gMix, msMid, msSide;
    bool inMs = false;
    // Clarity: the level of its band going into the curve (mean square, both channels), the cut it
    // asks for, the band in use and the (smoothed) gains of the band before and after the curve
    double lmEnv[kGentlrBands] {}, lmAtk = 0.0, lmRel = 0.0;
    float lmCutDb[kGentlrBands] {};
    double bandFreq[kGentlrBands] = {-1.0, -1.0, -1.0, -1.0}, bandWidth[kGentlrBands] = {-1.0, -1.0, -1.0, -1.0};
    int bandSlope[kGentlrBands] = {-1, -1, -1, -1};              // the Slope each band was designed with
    bool bandHp2On[kGentlrBands] = {false, false, false, false}; // runs its second section below
    float bandNorm[kGentlrBands] = {1.0f, 1.0f, 1.0f, 1.0f}, gBandPre[kGentlrBands] = {1.0f, 1.0f, 1.0f, 1.0f},
          gBandPost[kGentlrBands] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<float> gPost[kGentlrBands];
    bool clarityWas[kGentlrBands] = {false, false, false, false};
    // Gentlr's region drive: the cut bands per channel, oversampled, and the drive's (smoothed) gain
    // and how much of it is in (faded in and out, so switching it clicks nowhere)
    std::vector<float> region[2], regionOsBuf, gRegion, gRegionMix;
    float regionGain = 1.0f, regionMix = 0.0f;
    float drive = 1.0f, out = 1.0f, mix = 1.0f, smooth = 0.0f;
    // pre-limiter: the input peaks inside the look-ahead window and the gain (dB), smoothed in dB
    std::vector<float> lookPeaks;
    int lookPos = 0;
    float limGainDb = 0.0f, limAtk = 0.0f, limRel = 0.0f;
    double cLo = 0.0, cHi = 0.0, cFreq = 0.0, cWidth = 0.0, cRate = 0.0; // colour settings in use
    Meters* meters = nullptr;
    bool wetIdle = false; // fully dry: the curve is skipped (see process)
    bool prepared = false; // (the latency is published from then on)
};

} // namespace smacheratr

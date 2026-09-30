// A saturator in the spirit of Live's Saturator, with its Analog curve.
//
//   input -> [DC filter] -> [pre-limiter] -> Drive -> [colour pre-EQ] -> Analog curve
//         -> [colour post-EQ] -> [post clip] -> Dry/Wet -> Output
//
// The optional pre-limiter holds the input at its threshold with a 1 ms look-ahead, so a transient
// cannot push further into the curve than the rest of the sound: the drive is applied after it.
// Everything between the Drive gain and the post clip runs 4x oversampled with Hi-Quality on. The
// look-ahead delay is always in the path and the dry signal is delayed to match, so the latency
// reported to the host never changes while the plug-in is running.
//
// Gently (called Clarity before) works between the Drive and the curve: see process() and
// ClarityBand.h. Its Advanced mode gives each band a Threshold (clarityCutDb in Params.h) and can
// drive the region it cuts: the cut bands are split out again (the same band filters) and put
// through the Analog curve on their own, level-matched (clarityRegionDrive in ClarityBand.h),
// oversampled with the rest when Hi-Quality is on, so the region gets denser without the latency
// changing. With Advanced off none of that runs and the output is what it was before it.
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
    std::atomic<float> clarityDb {0.0f};  // Gently's cut before the curve (dB, 0 or less)
    std::atomic<float> clarity2Db {0.0f}; // its second band's
    // Gently's bands' levels as it measures them (the band's peak level into the curve, dB; -120
    // while the band is off), for the Threshold sliders
    std::atomic<float> clarityLevelDb {-120.0f};
    std::atomic<float> clarity2LevelDb {-120.0f};
    std::atomic<float> claritySubDb {0.0f};        // the Sub band's cut
    std::atomic<float> claritySubLevelDb {-120.0f}; // and its level
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain) { p[id] = plain; }
    double param (uint32_t id) const { return p[id]; }
    // Depends on the sample rate only (the same with Hi-Quality and the pre-limiter on or off).
    int latency () const { return chan[0].os.latency () + look; }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // Optional destination for level snapshots (may be null).
    void setMeters (Meters* m) { meters = m; }

private:
    static constexpr int kMaxLook = 256; // 1 ms up to 256 kHz
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
        Biquad dc, preLo, preHi, postLo, postHi;
        Biquad bandHp[kGentlyBands], bandLp[kGentlyBands], postHp[kGentlyBands], postLp[kGentlyBands]; // Clarity's bands, before and after the curve
        Oversampler os, regionOs; // regionOs: Gently's driven region, oversampled beside the rest
        Delay dryDelay, wetDelay, lookDelay;
        void reset ();
    };
    void updateFilters (bool force);
    float shapeChain (Channel& c, float v, bool color, int post) const;

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int maxBlock = 512, look = 48;
    Channel chan[2];
    std::vector<float> dry[2], pre[2], wet, osBuf, gDrive, gOut, gMix, msMid, msSide;
    bool inMs = false;
    // Clarity: the level of its band going into the curve (mean square, both channels), the cut it
    // asks for, the band in use and the (smoothed) gains of the band before and after the curve
    double lmEnv[kGentlyBands] {}, lmAtk = 0.0, lmRel = 0.0;
    float lmCutDb[kGentlyBands] {};
    double bandFreq[kGentlyBands] = {-1.0, -1.0, -1.0}, bandWidth[kGentlyBands] = {-1.0, -1.0, -1.0};
    float bandNorm[kGentlyBands] = {1.0f, 1.0f, 1.0f}, gBandPre[kGentlyBands] = {1.0f, 1.0f, 1.0f}, gBandPost[kGentlyBands] = {1.0f, 1.0f, 1.0f};
    std::vector<float> gPost[kGentlyBands];
    bool clarityWas[kGentlyBands] = {false, false, false};
    // Gently's region drive: the cut bands per channel, oversampled, and the drive's (smoothed) gain
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
};

} // namespace smacheratr

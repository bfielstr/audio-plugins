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
        Biquad lmSense, lmCut, clarityPre, clarityPost; // Clarity
        Oversampler os;
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
    // Clarity: the level of the low mids going into the curve (mean square, both channels) and the
    // cut it asks for
    double lmEnv = 0.0, lmAtk = 0.0, lmRel = 0.0;
    float lmCutDb = 0.0f;
    bool clarityWas = false;
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

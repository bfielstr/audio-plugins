// Waveshaping saturator in the spirit of Live's Saturator.
//
//   input -> [DC filter] -> Drive -> [colour pre-EQ] -> curve -> [colour post-EQ] -> [post clip]
//         -> Dry/Wet -> Output
//
// Everything between the Drive gain and the post clip is the nonlinear chain; with Hi-Quality
// on it runs 4x oversampled (see Oversampler). The dry path is delayed to match, and with
// Hi-Quality off the wet path is delayed instead, so the latency reported to the host never
// changes while the plug-in is running.
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
// driven input, which is what enters the shaper, and the peak of the shaped output.
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
    // Depends on the sample rate only (the same with Hi-Quality on or off).
    int latency () const { return chan[0].os.latency (); }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // Optional destination for level snapshots (may be null).
    void setMeters (Meters* m) { meters = m; }

    ShaperSettings shaperSettings () const;

private:
    struct Delay
    {
        std::vector<float> buf;
        int pos = 0;
        void resize (int n) { buf.assign ((size_t)n, 0.0f); pos = 0; }
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
        Oversampler os;
        Delay dryDelay, wetDelay;
        void reset ();
    };
    void updateFilters (bool force);
    float shapeChain (Channel& c, float v, const ShaperSettings& s, bool color, int post) const;

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int maxBlock = 512;
    Channel chan[2];
    std::vector<float> dry, pre, wet, osBuf, gDrive, gOut, gMix;
    float drive = 1.0f, out = 1.0f, mix = 1.0f, smooth = 0.0f;
    double cLo = 0.0, cHi = 0.0, cFreq = 0.0, cWidth = 0.0, cRate = 0.0; // colour settings in use
    Meters* meters = nullptr;
};

} // namespace smacheratr

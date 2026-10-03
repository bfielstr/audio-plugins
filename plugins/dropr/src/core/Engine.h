// Dropr: a transient designer you draw. Every hit in the audio starts the drawn shape, which sets the
// level over its Length: the top is 0 dB, the bottom -Depth dB. After the shape it stays at the shape's
// last level until the next hit (so a shape ending at the top lets everything between hits through).
//
//   hits: a fast level (peak, 0.1 ms up / 10 ms down) jumping Sensitivity dB over a slow one (50 ms),
//         above -50 dBFS, at least Retrigger ms apart; both channels together
//   the audio and the dry signal go through a fixed look-ahead (kLookaheadMs), so the shape can start
//   up to that long before the hit (Pre): its first part is heard on the hit's attack itself
//   -> Mix -> Output -> Smacheratr (the optional saturator at the end of every plug-in)
//
// A hit during the shape starts it again; the gain is eased over 0.5 ms so the jump never clicks.
// The latency (the look-ahead and the end saturator's) never changes.
#pragma once

#include "Params.h"
#include "Shape.h"

#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>
#include <vector>

namespace dropr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// For the editor (written by the audio thread once per block).
struct Meters
{
    std::atomic<float> position {1.0f}; // where in the shape the gain is (0 .. 1; 1 after it)
    std::atomic<float> gainDb {0.0f};   // the gain applied (dB)
    std::atomic<int> hits {0};          // counts the hits (the editor flashes on a change)
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain)
    {
        p[id] = plain;
        if (id >= kPointCount && id < kTailBase)
            shapeDirty = true;
        else if (id >= kTailExt2Base)
            tail.setParam (pk::kTailFields + pk::kTailExtFields + (id - kTailExt2Base), plain);
        else if (id >= kTailExtBase)
            tail.setParam (pk::kTailFields + (id - kTailExtBase), plain);
        else if (id >= kTailBase)
            tail.setParam (id - kTailBase, plain);
    }
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return look + tail.latency (); }
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int look = 240;
    std::vector<float> delay[2]; // the look-ahead (audio and dry)
    int delayPos = 0;
    // the gain path: computed when the hit is seen, then delayed by (look - Pre) to meet the audio
    std::vector<float> gainLine;
    int gainPos = 0;
    Shape shape;
    bool shapeDirty = true;
    double fastEnv = 0.0, slowEnv = 0.0, fastA = 0.0, fastR = 0.0, slowC = 0.0;
    int holdOff = 0;
    double envPos = 1.0; // where in the shape (1: after it)
    float gain = 1.0f, ease = 1.0f, mix = 1.0f, out = 1.0f, smooth = 0.0f;
    int hits = 0;
    Meters* meters = nullptr;
    smacheratr::Tail tail;
};

} // namespace dropr

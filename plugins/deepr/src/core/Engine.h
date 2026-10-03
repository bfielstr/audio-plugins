// Deepr: makes a bass sound deeper by contrast, without latency of its own.
//
//   input -> Linkwitz-Riley split (24 dB/oct at Split)
//     sub:  its side folded into its mid (Mono Sub), Sub Gain; its level is the dip's key
//     rest: the low mids dipped by up to Depth dB while the sub is over the Threshold: a band-pass
//           around Dip Freq (Dip Width octaves wide) taken out in part, a peaking cut exactly Depth deep
//           at its centre
//   -> sum (an all-pass of the input: flat) -> Mix with the dry signal (put through the same all-pass,
//   so they line up) -> Output -> Smacheratr (the optional saturator at the end of every plug-in)
//
// Why it sounds deeper: low frequencies mask the band just above them, and the ear is least
// sensitive down in the sub (equal-loudness contours), so what a listener hears as a bass's weight is
// largely the sub against the low mids. Dipping the low mids only while the sub plays makes the sub
// the loudest part of each note without making the track louder or the sub harder to drive; folding
// the sub to mono stops a reese's detuned layers from cancelling each other down there.
// Everything runs sample by sample (IIR filters and an envelope follower): the only latency is the
// end saturator's, which is always in the path so it never changes.
#pragma once

#include "Params.h"

#include "levlr/src/core/Crossover.h"
#include "smacheratr/src/core/Biquad.h"
#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>

namespace deepr {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// Levels for the editor (written by the audio thread once per block).
struct Meters
{
    std::atomic<float> subDb {-120.0f}; // the sub's level as the key measures it (dB)
    std::atomic<float> cutDb {0.0f};    // the dip it is making (dB, 0 or less)
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain)
    {
        p[id] = plain;
        if (id >= kTailExt3Base)
            tail.setParam (smacheratr::kTailExt3First + (id - kTailExt3Base), plain);
        else if (id >= kTailExt2Base)
            tail.setParam (pk::kTailFields + pk::kTailExtFields + (id - kTailExt2Base), plain);
        else if (id >= kTailExtBase)
            tail.setParam (pk::kTailFields + (id - kTailExtBase), plain);
        else if (id >= kTailBase)
            tail.setParam (id - kTailBase, plain);
    }
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return tail.latency (); } // Deepr itself adds none
    void setMeters (Meters* m) { meters = m; }
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    void updateFilters (bool force);

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    levlr::LrSplit split;
    levlr::LrAllpass dryAllpass;
    smacheratr::Biquad band[2]; // the dipped band (a band-pass, 0 dB and in phase at its centre)
    double splitHz = -1.0, dipHz = -1.0, dipWidth = -1.0;
    float env = 0.0f;   // the sub's level (linear peak envelope)
    float gCut = 1.0f;  // the dip's gain, smoothed
    float mono = 1.0f, subGain = 1.0f, mix = 1.0f, out = 1.0f, smooth = 0.0f;
    Meters* meters = nullptr;
    smacheratr::Tail tail;
};

} // namespace deepr

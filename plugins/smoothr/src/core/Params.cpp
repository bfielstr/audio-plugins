#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace smoothr {

using namespace pk;
using namespace pk::make;

namespace {

std::vector<ParamInfo> buildTable ()
{
    std::vector<ParamInfo> v;
    v.push_back (real (kInput, "Input", "Input", -12.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    // -1 dB: true-peak safe for lossy encoding (streaming services ask for -1 dBTP)
    v.push_back (real (kCeiling, "Ceiling", "Ceiling", -12.0, 0.0, -1.0, Curve::Linear, Disp::Db));
    v.push_back (real (kRelease, "Release", "Release", 5.0, 1000.0, 80.0, Curve::Log, Disp::Ms));
    v.push_back (toggle (kAutoRelease, "Auto Release", "Auto", true));
    v.push_back (percent (kSmooth, "Smooth", "Smooth", 0.5));
    // a little of the dip by default: it only works on loud low mids, and makes room for the lows
    v.push_back (percent (kCharacter, "Character", "Character", 0.3));

    // The Smacheratr before the limiter: on, mild. Pre-Limit off (it is a fast full-band limiter, the
    // very thing that roughens the lows; the limiter after it does that job smoothly), Drive 0 dB (the
    // Analog curve only rounds what passes half scale) and half wet, so peaks are rounded a little
    // before the limiter has to catch them and anything under -6 dBFS passes clean.
    pk::addTailParams (v, kTailBase, true);
    for (auto& pi : v)
    {
        if (pi.id == kTailBase + kTailPreLimit)
            pi.def = 0.0;
        if (pi.id == kTailBase + kTailMix)
            pi.def = 0.5;
    }
    smacheratr::addTailExtParams (v, kTailExtBase);
    smacheratr::addTailExt2Params (v, kTailExt2Base);
    smacheratr::addTailExt3Params (v, kTailExt3Base);
    smacheratr::addTailExt4Params (v, kTailExt4Base);
    static_assert (kNumParams == kTailExt4Base + pk::kTailExt4Fields, "the tail's fifth block is the last");
    return v;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

} // namespace smoothr

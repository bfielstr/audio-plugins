#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <cstdio>
#include <string>
#include <vector>

namespace dropr {

using namespace pk;
using namespace pk::make;

namespace {
void setDefault (std::vector<ParamInfo>& v, uint32_t id, double def)
{
    for (auto& p : v)
        if (p.id == id)
            p.def = def;
}
} // namespace

std::string negRatioText (double x)
{
    if (x >= kRatioInf * 0.999)
        return "1 : -inf";
    char buf[32];
    std::snprintf (buf, sizeof (buf), x < 10.0 ? "1 : -%.2f" : "1 : -%.1f", x);
    return buf;
}

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (real (kInput, "Input", "Input", -24.0, 48.0, 30.0, Curve::Linear, Disp::Db));
        v.push_back (integer (kBands, "Bands", "Bands", 1.0, (double)kMaxBands, (double)kMaxBands, Disp::Plain));
        for (int j = 0; j < kNumXovers; ++j)
            v.push_back (real (kXover1 + (uint32_t)j, keep ("Crossover " + std::to_string (j + 1)), keep ("Xover " + std::to_string (j + 1)),
                               kMinXoverHz, kMaxXoverHz, kXoverDefaults[j], Curve::Log, Disp::Hz));
        for (int k = 0; k < kMaxBands; ++k)
            v.push_back (real (kBandGain1 + (uint32_t)k, keep ("Band " + std::to_string (k + 1) + " Gain"),
                               keep ("Gain " + std::to_string (k + 1)), -36.0, 36.0, 12.0, Curve::Linear, Disp::Db));
        v.push_back (choice (kMode, "Mode", "Mode", {"Stereo", "Mid-Side"}, kModeStereo));
        v.push_back (percent (kLink, "Channel Link", "Link", 1.0));
        v.push_back (percent (kAdaptive, "Adaptive Time", "Adaptive", 0.5));
        v.push_back (real (kAttack, "Attack", "Attack", 0.1, 100.0, 6.0, Curve::Log, Disp::Ms));
        v.push_back (real (kRelease, "Release", "Release", 5.0, 2000.0, 350.0, Curve::Log, Disp::Ms));
        v.push_back (real (kDownThreshold, "Downward Threshold", "Down Thr", -96.0, 0.0, -72.0, Curve::Linear, Disp::Db));
        v.push_back (real (kDownRatio, "Downward Ratio", "Down Ratio", 1.0, kRatioInf, 4.0, Curve::Log, Disp::Ratio));
        v.push_back (toggle (kNegative, "Negative Ratio", "Negative", true));
        v.push_back (real (kNegRatio, "Downward Negative Ratio", "Neg Ratio", 0.1, kRatioInf, kRatioInf, Curve::Log, Disp::Ratio));
        v.push_back (real (kRange, "Negative Range", "Range", 1.0, 60.0, 12.0, Curve::Linear, Disp::Db));
        v.push_back (real (kUpThreshold, "Upward Threshold", "Up Thr", -96.0, 0.0, -48.0, Curve::Linear, Disp::Db));
        v.push_back (real (kUpRatio, "Upward Ratio", "Up Ratio", 1.0, kRatioInf, 1.0, Curve::Log, Disp::Ratio));
        v.push_back (real (kKnee, "Soft Knee", "Knee", 0.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kTilt, "Tilt", "Tilt", -3.0, 3.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kMakeup, "Makeup", "Makeup", -24.0, 72.0, 48.0, Curve::Linear, Disp::Db));
        v.push_back (percent (kMix, "Dry/Wet", "Dry/Wet", 1.0));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        // into a saturator: Smacheratr on and driven +18 dB, hard clipped at the very end (0 dBFS)
        pk::addTailParams (v, kTailBase, true);
        setDefault (v, kTailBase + pk::kTailDrive, 18.0);
        setDefault (v, kTailBase + pk::kTailPostClip, 2.0); // Hard Clip
        // no Pre-Limit: the hits that get through before the attack go into the curve (and the clip) instead
        // of ducking the body after them
        setDefault (v, kTailBase + pk::kTailPreLimit, 0.0);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        smacheratr::addTailExt3Params (v, kTailExt3Base);
        static_assert (kNumParams == kTailExt3Base + pk::kTailExt3Fields, "the tail's fourth block is the last");
        return v;
    }());
    return t;
}

} // namespace dropr

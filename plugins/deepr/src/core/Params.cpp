#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace deepr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (real (kDepth, "Depth", "Depth", 0.0, 12.0, 6.0, Curve::Linear, Disp::Db));
        v.push_back (real (kDipFreq, "Dip Frequency", "Dip", 80.0, 800.0, 250.0, Curve::Log, Disp::Hz));
        v.push_back (real (kDipWidth, "Dip Width", "Width", 0.5, 4.0, 1.5, Curve::Linear, Disp::Number));
        v.push_back (real (kThreshold, "Threshold", "Thresh", -60.0, 0.0, -30.0, Curve::Linear, Disp::Db));
        v.push_back (real (kAttack, "Attack", "Attack", 1.0, 100.0, 5.0, Curve::Log, Disp::Ms));
        v.push_back (real (kRelease, "Release", "Release", 20.0, 1000.0, 150.0, Curve::Log, Disp::Ms));
        v.push_back (real (kSplit, "Sub Split", "Split", 40.0, 200.0, 100.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kMonoSub, "Mono Sub", "Mono", 1.0));
        v.push_back (real (kSubGain, "Sub Gain", "Sub", -6.0, 6.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (choice (kListen, "Listen", "Listen", {"Off", "Sub", "Cut"}, kListenOff));
        v.push_back (percent (kMix, "Mix", "Mix", 1.0));
        v.push_back (real (kOutput, "Output", "Output", -12.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        pk::addTailParams (v, kTailBase);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        smacheratr::addTailExt3Params (v, kTailExt3Base);
        static_assert (kNumParams == kTailExt3Base + pk::kTailExt3Fields, "the tail's fourth block is the last");
        return v;
    }());
    return t;
}

} // namespace deepr

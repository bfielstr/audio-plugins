#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <string>
#include <vector>

namespace dropr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (real (kSensitivity, "Sensitivity", "Sens", 2.0, 24.0, 6.0, Curve::Linear, Disp::Db));
        v.push_back (real (kRetrigger, "Retrigger", "Retrig", 10.0, 1000.0, 50.0, Curve::Log, Disp::Ms));
        v.push_back (real (kLength, "Length", "Length", 5.0, 2000.0, 150.0, Curve::Log, Disp::Ms));
        v.push_back (real (kDepth, "Depth", "Depth", 0.0, 60.0, 30.0, Curve::Linear, Disp::Db));
        v.push_back (real (kPre, "Pre", "Pre", 0.0, kLookaheadMs, 2.0, Curve::Linear, Disp::Ms));
        v.push_back (percent (kMix, "Mix", "Mix", 1.0));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (integer (kPointCount, "Points", "Points", 2.0, (double)kMaxPoints, 3.0, Disp::Plain));
        // the default shape: the hit dropped to the bottom, coming back up over the Length
        const double defX[kMaxPoints] = {0.0, 0.35, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
        const double defY[kMaxPoints] = {0.0, 0.55, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
        const double defC[kMaxPoints] = {0.4, 0.3, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        for (int i = 0; i < kMaxPoints; ++i)
        {
            const std::string pn = "Point " + std::to_string (i + 1) + " ";
            v.push_back (real (pointParam (i, kPtX), keep (pn + "Time"), "Time", 0.0, 1.0, defX[i], Curve::Linear, Disp::Percent));
            v.push_back (real (pointParam (i, kPtY), keep (pn + "Level"), "Level", 0.0, 1.0, defY[i], Curve::Linear, Disp::Percent));
            v.push_back (real (pointParam (i, kPtCurve), keep (pn + "Curve"), "Curve", -1.0, 1.0, defC[i], Curve::Linear, Disp::Percent));
        }
        pk::addTailParams (v, kTailBase);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        static_assert (kNumParams == kTailExt2Base + pk::kTailExt2Fields, "the tail's third block is the last");
        return v;
    }());
    return t;
}

} // namespace dropr

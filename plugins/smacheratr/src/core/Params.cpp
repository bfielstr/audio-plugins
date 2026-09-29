#include "Params.h"

#include <vector>

namespace smacheratr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (real (kDrive, "Drive", "Drive", -36.0, 36.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (toggle (kPreLimit, "Pre-Limit", "Pre-Limit", false));
        v.push_back (real (kPreLimitThreshold, "Pre-Limit Threshold", "Limit", -30.0, 0.0, -6.0, Curve::Linear, Disp::Db));
        v.push_back (choice (kPostClip, "Post Clip Mode", "Post Clip", {"No Clip", "Soft Clip", "Hard Clip"}, kPostOff));
        v.push_back (toggle (kColorOn, "Color", "Color", true));
        v.push_back (real (kColorLo, "Color Amount Low", "Amt Lo", -1.0, 1.0, 0.0, Curve::Linear, Disp::Percent));
        v.push_back (real (kColorHi, "Color Amount High", "Amt Hi", -1.0, 1.0, 0.0, Curve::Linear, Disp::Percent));
        v.push_back (real (kColorFreq, "Color Frequency", "Freq", 50.0, 18000.0, 1000.0, Curve::Log, Disp::Hz));
        v.push_back (real (kColorWidth, "Color Width", "Width", 0.1, 4.0, 1.0, Curve::Log, Disp::Number));
        v.push_back (real (kOutput, "Output", "Output", -36.0, 0.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (percent (kDryWet, "Dry/Wet", "Dry/Wet", 1.0));
        v.push_back (toggle (kHiQuality, "Hi-Quality", "Hi-Q", true));
        v.push_back (toggle (kDcFilter, "Pre-DC Filter", "DC Filter", false));
        v.push_back (toggle (kMidSide, "Mid/Side", "M/S", false));
        v.push_back (toggle (kClarity, "Clarity", "Clarity", false));
        return v;
    }());
    return t;
}

} // namespace smacheratr

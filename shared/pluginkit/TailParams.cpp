#include "TailParams.h"

namespace pk {

void addTailParams (std::vector<ParamInfo>& v, uint32_t base, bool on)
{
    using namespace make;
    v.push_back (toggle (base + kTailOn, "Saturator", "Saturator", on));
    v.push_back (toggle (base + kTailPreLimit, "Saturator Pre-Limit", "Pre-Limit", true));
    v.push_back (real (base + kTailDrive, "Saturator Drive", "Drive", -36.0, 36.0, 0.0, Curve::Linear, Disp::Db));
    v.push_back (choice (base + kTailPostClip, "Saturator Post Clip", "Post Clip", {"No Clip", "Soft Clip", "Hard Clip"}, 0));
    v.push_back (percent (base + kTailMix, "Saturator Dry/Wet", "Dry/Wet", 1.0));
    v.push_back (real (base + kTailThreshold, "Saturator Pre-Limit Threshold", "Limit", -30.0, 0.0, -6.0, Curve::Linear, Disp::Db));
}

} // namespace pk

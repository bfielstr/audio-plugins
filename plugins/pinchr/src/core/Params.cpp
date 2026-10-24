#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace pinchr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        // a new instance: the model's own defaults (Input at unity, Ratio 4, Germ Soft, Attack and Release 3,
        // Drive 5, Feedback 5), clean, fully compressed
        v.push_back (real (kInput, "Input", "Input", -21.0, 21.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kRatio, "Ratio", "Ratio", 1.0, 11.0, 4.0, Curve::Linear, Disp::Number));
        v.push_back (choice (kCurve, "Curve", "Curve", {"R Soft", "Germ Soft", "Germ Med", "Silicon Med", "Silicon Hard", "Zener Hard"}, kGermSoft));
        v.push_back (real (kAttack, "Attack", "Attack", 0.0, 10.0, 3.0, Curve::Linear, Disp::Number));
        v.push_back (real (kRelease, "Release", "Release", 0.0, 10.0, 3.0, Curve::Linear, Disp::Number));
        v.push_back (choice (kScFilter, "SC Filter", "SC Filter", {"Off", "30 Hz", "60 Hz", "90 Hz", "150 Hz", "300 Hz"}, 0));
        v.push_back (toggle (kDirty, "Dirty", "Dirty", false));
        v.push_back (percent (kMix, "Mix", "Mix", 1.0));
        v.push_back (integer (kDrive, "Drive", "Drive", 0.0, 10.0, 5.0, Disp::Plain));
        v.push_back (real (kFeedback, "Feedback", "Feedback", 0.0, 10.0, 5.0, Curve::Linear, Disp::Number));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (toggle (kLink, "Link", "Link", true));
        v.push_back (choice (kMode, "Mode", "Mode", {"Stereo", "Mid/Side"}, kStereo));
        v.push_back (toggle (kExtSc, "Ext SC", "Ext SC", false));
        v.push_back (real (kScGain, "SC Gain", "SC Gain", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        // the end saturator: off in a new instance (the output stage is the colour here)
        pk::addTailParams (v, kTailBase, false);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        smacheratr::addTailExt3Params (v, kTailExt3Base);
        smacheratr::addTailExt4Params (v, kTailExt4Base);
        return v;
    }());
    return t;
}

} // namespace pinchr

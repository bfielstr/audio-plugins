#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace locus {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        // Contrast is shown as a bipolar percentage (-100 % .. +100 %).
        v.push_back (real (kContrast, "Contrast", "Contrast", -1.0, 1.0, 0.0, Curve::Linear, Disp::Percent));
        v.push_back (choice (kMode, "Mode", "Mode", {"Punchy", "Smooth"}, kPunchy));
        v.push_back (real (kGain, "Gain", "Gain", -12.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kLowFreq, "Low Frequency", "Low", 20.0, 1000.0, 30.0, Curve::Log, Disp::Hz));
        v.push_back (real (kHighFreq, "High Frequency", "High", 20.0, 1000.0, 300.0, Curve::Log, Disp::Hz));
        v.push_back (toggle (kSolo, "Solo Focus Range", "Solo", false));
        v.push_back (real (kOutput, "Output", "Output", -12.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        pk::addTailParams (v, kTailBase);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        smacheratr::addTailExt3Params (v, kTailExt3Base);
        smacheratr::addTailExt4Params (v, kTailExt4Base);
        static_assert (kNumParams == kTailExt4Base + pk::kTailExt4Fields, "the tail's fifth block is the last");
        return v;
    }());
    return t;
}

} // namespace locus

#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace smeezr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (percent (kSqueeze, "Squeeze", "Squeeze", 0.4));
        v.push_back (choice (kSpeed, "Speed", "Speed", {"Fast", "Slow"}, kSpeedFast));
        v.push_back (percent (kMix, "Mix", "Mix", 1.0));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        // the end saturator
        pk::addTailParams (v, kTailBase);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        smacheratr::addTailExt3Params (v, kTailExt3Base);
        smacheratr::addTailExt4Params (v, kTailExt4Base);
        return v;
    }());
    return t;
}

} // namespace smeezr

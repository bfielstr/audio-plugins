#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace orbitr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        const Defaults& d = kLiquidDebris;
        std::vector<ParamInfo> v;
        // the ranges and names are Detonatr's Motion stage's
        v.push_back (integer (kOrbs, "Orbs", "Orbs", 1, 16, d.orbs, Disp::Plain));
        v.push_back (choice (kPattern, "Pattern", "Pattern", {"Orbit", "Swarm"}, d.pattern));
        v.push_back (real (kSpeed, "Speed", "Speed", 0.0, 80.0, d.speed, Curve::Power3, Disp::Number));
        v.push_back (real (kDistance, "Distance", "Distance", 0.5, 20.0, d.distance, Curve::Log, Disp::Number));
        v.push_back (real (kRadius, "Radius", "Radius", 0.1, 3.0, d.radius, Curve::Log, Disp::Number));
        v.push_back (percent (kSpread, "Spread", "Spread", d.spread));
        v.push_back (percent (kRandom, "Randomness", "Random", d.randomness));
        v.push_back (toggle (kFloor, "Floor", "Floor", d.floor));
        v.push_back (percent (kMix, "Mix", "Mix", d.mix));
        v.push_back (percent (kDryWet, "Dry/Wet", "Dry/Wet", 1.0));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        pk::addTailParams (v, kTailBase);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        smacheratr::addTailExt3Params (v, kTailExt3Base);
        static_assert (kNumParams == kTailExt3Base + pk::kTailExt3Fields, "the tail's fourth block is the last");
        return v;
    }());
    return t;
}

} // namespace orbitr

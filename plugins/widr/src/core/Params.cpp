#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace widr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (real (kWidth, "Width", "Width", 0.0, 2.0, 1.0, Curve::Linear, Disp::Percent));
        v.push_back (choice (kCharacter, "Character", "Character", {"Tight", "Wide", "Epic", "Surround"}, kWide));
        v.push_back (percent (kSize, "Size", "Size", 0.5));
        v.push_back (percent (kSpace, "Space", "Space", 0.15));
        v.push_back (real (kDecay, "Decay", "Decay", 200.0, 3000.0, 1200.0, Curve::Log, Disp::Ms));
        v.push_back (real (kPreDelay, "Pre-Delay", "Pre-Delay", 0.0, 80.0, 12.0, Curve::Linear, Disp::Ms));
        v.push_back (real (kDamping, "Damping", "Damping", 1000.0, 20000.0, 5500.0, Curve::Log, Disp::Hz));
        v.push_back (real (kAir, "Air", "Air", 0.0, 6.0, 1.0, Curve::Linear, Disp::Db));
        v.push_back (percent (kBeyond, "Beyond", "Beyond", 0.0));
        v.push_back (real (kMonoBelow, "Mono Below", "Mono Below", 40.0, 400.0, 150.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kGuard, "Mono Guard", "Guard", 0.6));
        v.push_back (choice (kRole, "Role", "Role", {"Anchor", "Support", "Wide", "Ambient"}, kSupport));
        v.push_back (percent (kAware, "Mix Aware", "Aware", 0.5));
        v.push_back (integer (kGroup, "Group", "Group", 1.0, 8.0, 1.0, Disp::Plain));
        v.push_back (toggle (kMonoCheck, "Mono Check", "Mono", false));
        v.push_back (real (kOutput, "Output", "Output", -12.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        pk::addTailParams (v, kTailBase);
        v.push_back (percent (kContrast, "Contrast", "Contrast", 0.5));
        v.push_back (real (kDryLevel, "Dry Level", "Dry", kLevelMinDb, 6.0, 0.0, Curve::Linear, Disp::DbGain));
        v.push_back (real (kWetLevel, "Wet Level", "Wet", kLevelMinDb, 6.0, 0.0, Curve::Linear, Disp::DbGain));
        smacheratr::addTailExtParams (v, kTailExtBase, true); // Mid/Side on: it keeps the width when pushed
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        static_assert (kNumParams == kTailExt2Base + pk::kTailExt2Fields, "Gently's Advanced block is the last");
        return v;
    }());
    return t;
}

} // namespace widr

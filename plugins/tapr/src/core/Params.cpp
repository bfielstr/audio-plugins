#include "Params.h"

#include "CutShape.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace tapr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        // a new instance: the tape the plug-in was measured against (Drive 75 %, Speed 80 %, Compression 35 %,
        // its Master's -4.4 dB as Output), the Cut EQ on and flat after the tape
        v.push_back (toggle (kTapeOn, "Tape", "Tape", true));
        v.push_back (percent (kDrive, "Drive", "Drive", 0.75));
        v.push_back (percent (kSpeed, "Speed", "Speed", 0.8));
        v.push_back (percent (kCompression, "Compression", "Compression", 0.35));
        v.push_back (choice (kCompPlace, "Compression Place", "Comp", {"Pre", "Post"}, kPre));
        v.push_back (toggle (kCutOn, "Cut", "Cut", true));
        v.push_back (real (kCutLo, "Cut Lo", "Lo", cut::kGainMin, cut::kGainMax, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kCutMid, "Cut Mid", "Mid", cut::kGainMin, cut::kGainMax, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kCutHi, "Cut Hi", "Hi", cut::kGainMin, cut::kGainMax, 0.0, Curve::Linear, Disp::Db));
        v.push_back (choice (kCutPlace, "Cut Place", "Cut", {"Pre", "Post"}, kPost));
        v.push_back (percent (kMix, "Mix", "Mix", 1.0));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 12.0, -4.4, Curve::Linear, Disp::Db));
        // the end saturator: off in a new instance (the tape stage is the saturator here)
        pk::addTailParams (v, kTailBase, false);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        smacheratr::addTailExt3Params (v, kTailExt3Base);
        smacheratr::addTailExt4Params (v, kTailExt4Base);
        return v;
    }());
    return t;
}

} // namespace tapr

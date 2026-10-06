#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace ciphr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        // GENERATOR
        v.push_back (percent (kTimbre, "Timbre", "Timbre", 0.0));
        v.push_back (real (kCross, "Cross", "Cross", -1.0, 1.0, 0.0, Curve::Linear, Disp::Percent));
        v.push_back (percent (kCharacter, "Character", "Character", 0.5));
        v.push_back (integer (kVariant, "Variant", "Variant", kMinVariant, kMaxVariant, kMinVariant, Disp::Plain));
        v.push_back (percent (kDrift, "Drift", "Drift", 0.0));
        v.push_back (real (kTune, "Tune", "Tune", -24.0, 24.0, 0.0, Curve::Linear, Disp::Semis));
        // INPUT
        v.push_back (percent (kInput, "Input", "Input", 0.0));
        v.push_back (choice (kInputPath, "Input Path", "Path", {"Direct", "Voices"}, kPathDirect));
        // FILTER
        v.push_back (real (kCutoff, "Cutoff", "Cutoff", 20.0, 20000.0, 6000.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kResonance, "Resonance", "Resonance", 0.2));
        v.push_back (percent (kFilterType, "Filter Type", "Type", 0.0));
        v.push_back (percent (kKeyTrack, "Key Track", "Key Track", 0.5));
        v.push_back (real (kEnvAmount, "Env Amount", "Env Amt", -1.0, 1.0, 0.3, Curve::Linear, Disp::Percent));
        // AMP ENVELOPE
        v.push_back (real (kAttack, "Attack", "Attack", 1.0, 10000.0, 5.0, Curve::Log, Disp::Ms));
        v.push_back (real (kDecay, "Decay", "Decay", 1.0, 10000.0, 400.0, Curve::Log, Disp::Ms));
        v.push_back (percent (kSustain, "Sustain", "Sustain", 0.8));
        v.push_back (real (kRelease, "Release", "Release", 1.0, 10000.0, 500.0, Curve::Log, Disp::Ms));
        // FILTER ENVELOPE
        v.push_back (real (kFilterAttack, "Filter Attack", "Attack", 1.0, 10000.0, 2.0, Curve::Log, Disp::Ms));
        v.push_back (real (kFilterDecay, "Filter Decay", "Decay", 1.0, 10000.0, 600.0, Curve::Log, Disp::Ms));
        v.push_back (percent (kFilterSustain, "Filter Sustain", "Sustain", 0.2));
        v.push_back (real (kFilterRelease, "Filter Release", "Release", 1.0, 10000.0, 600.0, Curve::Log, Disp::Ms));
        v.push_back (percent (kVelocity, "Velocity", "Velocity", 0.6));
        // PROCESSOR
        v.push_back (percent (kSpace, "Space", "Space", 0.5));
        v.push_back (real (kLength, "Length", "Length", 10.0, 2000.0, 420.0, Curve::Log, Disp::Ms));
        v.push_back (percent (kMovement, "Movement", "Movement", 0.3));
        v.push_back (real (kRegen, "Regen", "Regen", -1.0, 1.0, 0.35, Curve::Linear, Disp::Percent));
        v.push_back (real (kShift, "Shift", "Shift", -250.0, 250.0, 0.0, Curve::Linear, Disp::Hz));
        // OUTPUT
        v.push_back (percent (kBlend, "Blend", "Blend", 0.35));
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

} // namespace ciphr

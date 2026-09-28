#include "Params.h"

#include <vector>

namespace perrera {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (real (kHpFreq, "High-Pass Frequency", "HP Freq", 20.0, 20000.0, 800.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kHpRes, "High-Pass Resonance", "HP Res", 0.0));
        v.push_back (real (kLpFreq, "Low-Pass Frequency", "LP Freq", 20.0, 20000.0, 200.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kLpRes, "Low-Pass Resonance", "LP Res", 0.0));
        v.push_back (choice (kSlope, "Slope", "Slope", {"12 dB", "24 dB"}, kSlope12));
        v.push_back (real (kSplit, "Split", "Split", -48.0, 48.0, 0.0, Curve::Linear, Disp::Semis));
        v.push_back (real (kEnvAmount, "Envelope Amount", "Env Amt", -48.0, 48.0, 0.0, Curve::Linear, Disp::Semis));
        v.push_back (real (kEnvAttack, "Envelope Attack", "Attack", 0.1, 2000.0, 5.0, Curve::Log, Disp::Ms));
        v.push_back (real (kEnvDecay, "Envelope Decay", "Decay", 1.0, 5000.0, 300.0, Curve::Log, Disp::Ms));
        v.push_back (percent (kKey, "Key Tracking", "Key", 1.0));
        v.push_back (integer (kTranspose, "Transpose", "Transpose", -48.0, 48.0, 0.0, Disp::Semis));
        v.push_back (integer (kPbRange, "Pitch Bend Range", "Bend", 0.0, 24.0, 2.0, Disp::Semis));
        v.push_back (integer (kRoot, "Root Note", "Root", 0.0, 127.0, kDefaultRoot, Disp::Plain));
        v.push_back (percent (kDryWet, "Dry/Wet", "Dry/Wet", 1.0));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        return v;
    }());
    return t;
}

} // namespace perrera

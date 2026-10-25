#include "Params.h"

#include "pluginkit/EndClip.h"

#include <vector>

namespace bubr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        // a new instance: the model's own defaults (Boost 55 %, 40 Hz, Intensity 50 %, Q-Factor 20 %, Drive 20 %,
        // the gate at -30 dB with Attack 0 ms, Release 100 ms and Ratio 4, Low Cut at its lowest, both at full level)
        v.push_back (real (kBoost, "Boost", "Boost", 0.0, 1.1, 0.55, Curve::Linear, Disp::Percent));
        v.push_back (real (kFreq, "Freq", "Freq", 20.0, 125.0, 40.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kIntensity, "Intensity", "Intensity", 0.5));
        v.push_back (percent (kQFactor, "Q-Factor", "Q-Factor", 0.2));
        v.push_back (percent (kDrive, "Drive", "Drive", 0.2));
        v.push_back (toggle (kDriveX2, "Drive x2", "x2", false));
        v.push_back (real (kGate, "Gate", "Gate", -60.0, 0.0, -30.0, Curve::Linear, Disp::Db));
        v.push_back (real (kAttack, "Attack", "Attack", 0.0, 300.0, 0.0, Curve::Power3, Disp::Ms));
        v.push_back (real (kRelease, "Release", "Release", 20.0, 500.0, 100.0, Curve::Log, Disp::Ms));
        v.push_back (real (kRatio, "Ratio", "Ratio", 1.0, 100.0, 4.0, Curve::Log, Disp::Number));
        v.push_back (real (kLowCut, "Low Cut", "Low Cut", 20.0, 125.0, 20.0, Curve::Log, Disp::Hz));
        v.push_back (real (kDryWet, "Dry/Wet", "Dry/Wet", -1.0, 1.0, 0.0, Curve::Linear, Disp::Percent));
        v.push_back (choice (kOversample, "Oversample", "Oversample", {"Off", "2x", "4x"}, kOsOff));
        v.push_back (toggle (kLookahead, "Lookahead", "Lookahead", true));
        v.push_back (real (kOutput, "Output", "Output", -60.0, 0.0, 0.0, Curve::Linear, Disp::Db));
        pk::addEndClipParams (v, kSoftClip, kHardClip);
        return v;
    }());
    return t;
}

} // namespace bubr

#include "Params.h"

#include "pluginkit/EndClip.h"
#include "smacheratr/src/core/TailExt.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sunder {

using namespace pk;
using namespace pk::make;

int stepNearest (double position) { return std::clamp ((int)std::lround (1.0 + (position - 1.0) * 9.0 / 14.0), 1, kSteps); }

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        // a new instance: the model's own defaults as near as the steps go (pinchr's Ratio 4, Germ Soft, Attack
        // and Release 3; Drive 4 and Feedback 3, the steps nearest its Drive 5 and Feedback 5 in level and tone),
        // fully compressed, the bass enhancer off
        v.push_back (real (kInput, "Input", "Input", -21.0, 21.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (integer (kRatio, "Ratio", "Ratio", 1.0, (double)kSteps, 4.0, Disp::Plain));
        v.push_back (choice (kCurve, "Curve", "Curve", {"R Soft", "Germ Soft", "Germ Med", "Silicon Med", "Silicon Hard", "Zener Hard"}, kGermSoft));
        v.push_back (real (kAttack, "Attack", "Attack", 0.0, 10.0, 3.0, Curve::Linear, Disp::Number));
        v.push_back (real (kRelease, "Release", "Release", 0.0, 10.0, 3.0, Curve::Linear, Disp::Number));
        v.push_back (choice (kScFilter, "SC Filter", "SC Filter", {"Off", "30 Hz", "60 Hz", "90 Hz", "150 Hz", "300 Hz"}, 0));
        v.push_back (toggle (kDirty, "Dirty", "Dirty", false)); // (retired below: always on)
        v.push_back (percent (kMix, "Mix", "Mix", 1.0));
        v.push_back (integer (kDrive, "Drive", "Drive", 1.0, (double)kSteps, kDefaultDrive, Disp::Plain));
        v.push_back (integer (kFeedback, "Feedback", "Feedback", 1.0, (double)kSteps, kDefaultFeedback, Disp::Plain));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (toggle (kLink, "Link", "Link", true));
        v.push_back (choice (kMode, "Mode", "Mode", {"Stereo", "Mid/Side"}, kStereo));
        v.push_back (toggle (kExtSc, "Ext SC", "Ext SC", false));
        v.push_back (real (kScGain, "SC Gain", "SC Gain", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (percent (kSaturation, "Saturation", "Saturation", 0.5));
        // the old end saturator, retired (its IDs kept for saved projects)
        pk::addTailParams (v, kTailBase, false);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        smacheratr::addTailExt3Params (v, kTailExt3Base);
        smacheratr::addTailExt4Params (v, kTailExt4Base);
        pk::addEndClipParams (v, kSoftClip, kHardClip);
        // the bass enhancer: off; the rest bubr's own defaults (Boost 55 %, 40 Hz, Intensity 50 %, Q-Factor 20 %,
        // Drive 20 %, the gate at -30 dB, Attack 0 ms, Release 100 ms, Ratio 4, Low Cut at its lowest, Off)
        v.push_back (percent (kBass, "Bass", "Bass", 0.0));
        v.push_back (real (kBassBoost, "Bass Boost", "Bass Boost", 0.0, 1.1, 0.55, Curve::Linear, Disp::Percent));
        v.push_back (real (kBassFreq, "Bass Freq", "Bass Freq", 20.0, 125.0, 40.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kBassIntensity, "Bass Intensity", "Intensity", 0.5));
        v.push_back (percent (kBassQ, "Bass Q-Factor", "Q-Factor", 0.2));
        v.push_back (percent (kBassDrive, "Bass Drive", "Bass Drive", 0.2));
        v.push_back (toggle (kBassDriveX2, "Bass Drive x2", "x2", false));
        v.push_back (real (kBassGate, "Bass Gate", "Bass Gate", -60.0, 0.0, -30.0, Curve::Linear, Disp::Db));
        v.push_back (real (kBassAttack, "Bass Attack", "Gate Attack", 0.0, 300.0, 0.0, Curve::Power3, Disp::Ms));
        v.push_back (real (kBassRelease, "Bass Release", "Gate Release", 20.0, 500.0, 100.0, Curve::Log, Disp::Ms));
        v.push_back (real (kBassRatio, "Bass Ratio", "Gate Ratio", 1.0, 100.0, 4.0, Curve::Log, Disp::Number));
        v.push_back (real (kBassLowCut, "Bass Low Cut", "Low Cut", 20.0, 125.0, 20.0, Curve::Log, Disp::Hz));
        v.push_back (choice (kBassOversample, "Bass Oversample", "Oversample", {"Off", "2x", "4x"}, kBassOsOff));
        pk::retire (v, kDirty, kDirty + 1);
        pk::retire (v, kTailBase, kTailExt4Base + pk::kTailExt4Fields);
        return v;
    }());
    return t;
}

} // namespace sunder

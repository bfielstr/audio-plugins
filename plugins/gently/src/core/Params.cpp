#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <string>
#include <vector>

namespace gently {

using namespace pk;
using namespace pk::make;

namespace {

std::vector<ParamInfo> buildTable ()
{
    // Smacheratr's Gently parameters, for the ranges the two share (the Threshold sliders are
    // Smacheratr's, working on normalized values, so the Thresholds must match exactly)
    const auto& sm = smacheratr::paramTable ();
    auto like = [&] (uint32_t id, uint32_t smId, const std::string& name, const char* shortName, double def) {
        ParamInfo pi = sm.info (smId);
        pi.id = id;
        pi.name = keep (name);
        pi.shortName = shortName;
        pi.def = def;
        return pi;
    };

    std::vector<ParamInfo> v;
    v.push_back (toggle (kAdvanced, "Advanced", "Advanced", true)); // on: the Threshold sliders show (each at -18 dB, the same sound as off)
    v.push_back (like (kDrive, smacheratr::kClarityDrive, "Drive", "Drive", 0.0));
    v.push_back (like (kDriveAmount, smacheratr::kClarityDriveAmount, "Drive Amount", "Amount", 12.0));
    v.push_back (real (kAttack, "Attack", "Attack", 0.5, 100.0, kDefaultAttackMs, Curve::Log, Disp::Ms));
    v.push_back (real (kRelease, "Release", "Release", 20.0, 2000.0, kDefaultReleaseMs, Curve::Log, Disp::Ms));
    v.push_back (choice (kStereo, "Stereo", "Stereo", {"Stereo", "Mid/Side", "Mid", "Side"}, kStereoLinked));
    v.push_back (percent (kMix, "Mix", "Mix", 1.0));
    v.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    // band 1 on the low mids (the mud), band 2 on the upper mids (the harshness)
    const double freqs[kBands] = {250.0, 3000.0}, ranges[kBands] = {8.0, 6.0};
    for (int b = 0; b < kBands; ++b)
    {
        const std::string n = "Band " + std::to_string (b + 1) + " ";
        v.push_back (toggle (bandParam (b, kOn), keep (n + "On"), "On", true));
        v.push_back (like (bandParam (b, kFreq), smacheratr::kClarityFreq, n + "Frequency", "Freq", freqs[b]));
        v.push_back (like (bandParam (b, kWidth), smacheratr::kClarityWidth, n + "Width", "Width", 2.0));
        v.push_back (like (bandParam (b, kRange), smacheratr::kClarityRange, n + "Range", "Range", ranges[b]));
        v.push_back (like (bandParam (b, kThreshold), smacheratr::kClarityThreshold, n + "Threshold", "Thresh",
                           smacheratr::kClarityThresholdDb));
    }
    // the Sub band: Smacheratr's (off by default)
    v.push_back (like (kSubOn, smacheratr::kClaritySub, "Sub", "Sub", 0.0));
    v.push_back (like (kSubFreq, smacheratr::kClaritySubFreq, "Sub Frequency", "Freq", smacheratr::kSubDefaultHz));
    v.push_back (like (kSubRange, smacheratr::kClaritySubRange, "Sub Range", "Range", 8.0));
    v.push_back (like (kSubThreshold, smacheratr::kClaritySubThreshold, "Sub Threshold", "Thresh", smacheratr::kClarityThresholdDb));
    pk::addTailParams (v, kTailBase);
    smacheratr::addTailExtParams (v, kTailExtBase);
    smacheratr::addTailExt2Params (v, kTailExt2Base);
    static_assert (kNumParams == kTailExt2Base + pk::kTailExt2Fields, "the end saturator's Gently block is the last");
    return v;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

} // namespace gently

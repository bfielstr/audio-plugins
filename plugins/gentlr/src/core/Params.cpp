#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <string>
#include <vector>

namespace gentlr {

using namespace pk;
using namespace pk::make;

namespace {

std::vector<ParamInfo> buildTable ()
{
    // Smacheratr's Gentlr parameters, for the ranges the two share (the Threshold sliders are
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
    // the Sub band: Smacheratr's (no button, its Range 0 by default: it cuts nothing until it gets one)
    v.push_back (like (kSubOn, smacheratr::kClaritySub, "Sub (unused)", "Sub", 0.0));
    v.push_back (like (kSubFreq, smacheratr::kClaritySubFreq, "Sub Frequency", "Freq", smacheratr::kSubDefaultHz));
    v.push_back (like (kSubRange, smacheratr::kClaritySubRange, "Sub Range", "Range", 0.0));
    v.push_back (like (kSubThreshold, smacheratr::kClaritySubThreshold, "Sub Threshold", "Thresh", smacheratr::kClarityThresholdDb));
    pk::addTailParams (v, kTailBase);
    smacheratr::addTailExtParams (v, kTailExtBase);
    smacheratr::addTailExt2Params (v, kTailExt2Base);
    // the High band and No Overlap: Smacheratr's (the High band as the Sub band, No Overlap off)
    v.push_back (like (kHighOn, smacheratr::kClarityHigh, "High (unused)", "High", 0.0));
    v.push_back (like (kHighFreq, smacheratr::kClarityHighFreq, "High Frequency", "Freq", smacheratr::kHighDefaultHz));
    v.push_back (like (kHighRange, smacheratr::kClarityHighRange, "High Range", "Range", 0.0));
    v.push_back (like (kHighThreshold, smacheratr::kClarityHighThreshold, "High Threshold", "Thresh", smacheratr::kClarityThresholdDb));
    v.push_back (like (kNoOverlap, smacheratr::kClarityNoOverlap, "No Overlap", "No Overlap", 0.0));
    smacheratr::addTailExt3Params (v, kTailExt3Base);
    static_assert (kNumParams == kTailExt3Base + pk::kTailExt3Fields, "the end saturator's fourth block is the last");
    return v;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

} // namespace gentlr

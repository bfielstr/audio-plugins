#include "Params.h"

#include <vector>

namespace multidyn {

using namespace pk;
using namespace pk::make;

namespace {

std::vector<ParamInfo> buildTable ()
{
    std::vector<ParamInfo> t;
    t.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db)); // 0 dB = the baked -7 dB
    t.push_back (percent (kAmount, "Amount", "Amount", 1.0));
    t.push_back (real (kTime, "Time", "Time", 0.1, 10.0, 1.0, Curve::Log, Disp::Percent));
    t.push_back (toggle (kSoftKnee, "Soft Knee", "Soft Knee", true));
    t.push_back (choice (kDetector, "Peak/RMS", "Detect", {"Peak", "RMS"}, kRms));
    t.push_back (choice (kBands, "Bands", "Bands", {"1", "2", "3", "4"}, 2));
    t.push_back (real (kXover1, "Crossover 1", "X1", 20.0, 16000.0, 88.3, Curve::Log, Disp::Hz));
    t.push_back (real (kXover2, "Crossover 2", "X2", 20.0, 16000.0, 2500.0, Curve::Log, Disp::Hz));
    t.push_back (real (kXover3, "Crossover 3", "X3", 20.0, 16000.0, 8000.0, Curve::Log, Disp::Hz));
    t.push_back (toggle (kScOn, "Sidechain On", "Sidechain", false));
    t.push_back (real (kScGain, "Sidechain Gain", "SC Gain", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    t.push_back (percent (kScMix, "Sidechain Dry/Wet", "SC Mix", 1.0));
    t.push_back (toggle (kScListen, "Sidechain Listen", "Listen", false));

    // Defaults (3 bands, Character mode): heavy upward compression (the "OTT" preset pushed further: the
    // Below blocks at 1:inf lift everything to the threshold), with the attack / release times of
    // OTT. The preset's gains are baked in (see kBakedInputDb): every gain control defaults to 0 dB.
    struct BandDefaults
    {
        double below, belowRatio, above, aboveRatio, attack, release;
    };
    const BandDefaults defs[kMaxBands] = {
        {-40.8, kRatioInf, -33.8, 66.7, 47.8, 282.0},      // band 1 (low)
        {-41.8, kRatioInf, -30.2, 66.7, 22.4, 282.0},      // band 2
        {-40.8, kRatioInf, -35.5, kRatioInf, 13.5, 132.0}, // band 3
        {-40.8, 4.17, -35.5, kRatioInf, 13.5, 132.0},      // band 4 (high)
    };
    for (int b = 0; b < kMaxBands; ++b)
    {
        const std::string n = "Band " + std::to_string (b + 1);
        const BandDefaults& d = defs[b];
        auto id = [b] (int f) { return bandParam (b, f); };
        t.push_back (toggle (id (kBandActive), keep (n + " Active"), keep (n + " On"), true));
        t.push_back (toggle (id (kBandSolo), keep (n + " Solo"), "Solo", false));
        t.push_back (real (id (kBandInput), keep (n + " Input Gain"), "Input", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        t.push_back (real (id (kBandOutput), keep (n + " Output Gain"), "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        t.push_back (real (id (kAboveThresh), keep (n + " Above Threshold"), "Above", -80.0, 0.0, d.above, Curve::Linear, Disp::Db));
        t.push_back (real (id (kAboveRatio), keep (n + " Above Ratio"), "Ratio", kRatioMin, kRatioInf, d.aboveRatio, Curve::Ratio, Disp::Ratio));
        t.push_back (real (id (kBelowThresh), keep (n + " Below Threshold"), "Below", -80.0, 0.0, d.below, Curve::Linear, Disp::Db));
        t.push_back (real (id (kBelowRatio), keep (n + " Below Ratio"), "Ratio", kRatioMin, kRatioInf, d.belowRatio, Curve::Ratio, Disp::Ratio));
        t.push_back (real (id (kAttack), keep (n + " Attack"), "Attack", 0.1, 1000.0, d.attack, Curve::Log, Disp::Ms));
        t.push_back (real (id (kRelease), keep (n + " Release"), "Release", 1.0, 3000.0, d.release, Curve::Log, Disp::Ms));
    }
    t.push_back (choice (kMode, "Mode", "Mode", {"Base", "Character"}, kCharacter));
    t.push_back (toggle (kPreLimit, "Pre-Limit", "Pre-Limit", false));
    t.push_back (real (kPreLimitCeiling, "Pre-Limit Above Threshold", "Ceiling", -12.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    // the end-of-chain Smacheratr (off, Drive 0 dB): its fields line up with kSatOn ... kSatPreLimitThreshold
    static_assert (kSatPreLimit == kSatOn + pk::kTailPreLimit && kSatDrive == kSatOn + pk::kTailDrive &&
                   kSatPostClip == kSatOn + pk::kTailPostClip && kSatMix == kSatOn + pk::kTailMix &&
                   kSatPreLimitThreshold == kSatOn + pk::kTailThreshold);
    pk::addTailParams (t, kSatOn);
    return t;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

} // namespace multidyn

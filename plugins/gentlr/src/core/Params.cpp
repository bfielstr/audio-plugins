#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <string>
#include <vector>

namespace gentlr {

using namespace pk;
using namespace pk::make;

namespace {

// rack: the table as it was up to 0.30.0 (rackTable)
std::vector<ParamInfo> buildTable (bool rack)
{
    // Smacheratr's Gentlr parameters, for the ranges the two share (the Thresholds go lower in Gentlr:
    // threshold () below; the sliders' host shows them with sliderTable ())
    const auto& sm = smacheratr::paramTable ();
    auto like = [&] (uint32_t id, uint32_t smId, const std::string& name, const char* shortName, double def) {
        ParamInfo pi = sm.info (smId);
        pi.id = id;
        pi.name = keep (name);
        pi.shortName = shortName;
        pi.def = def;
        return pi;
    };
    auto threshold = [rack] (uint32_t id, const std::string& name, double def) {
        return real (id, keep (name), "Thresh", rack ? kThresholdMinBeforeDb : kThresholdMinDb, kThresholdMaxDb, rack ? smacheratr::kClarityThresholdDb : def,
                     Curve::Linear, Disp::Db);
    };

    std::vector<ParamInfo> v;
    v.push_back (toggle (kAdvanced, "Advanced", "Advanced", true)); // on: the Threshold sliders show (band 1's at -82 dB, the others at -18)
    v.push_back (like (kDrive, smacheratr::kClarityDrive, "Drive", "Drive", 0.0));
    v.push_back (like (kDriveAmount, smacheratr::kClarityDriveAmount, "Drive Amount", "Amount", 12.0));
    v.push_back (real (kAttack, "Attack", "Attack", 0.5, 100.0, kDefaultAttackMs, Curve::Log, Disp::Ms));
    v.push_back (real (kRelease, "Release", "Release", 20.0, 2000.0, kDefaultReleaseMs, Curve::Log, Disp::Ms));
    v.push_back (choice (kStereo, "Stereo", "Stereo", {"Stereo", "Mid/Side", "Mid", "Side"}, kStereoLinked));
    v.push_back (percent (kMix, "Mix", "Mix", 1.0));
    v.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    // band 1 on the low end, cutting from far down (250 Hz, 8 dB, -18 dB up to 0.30.0: State.cpp), band 2
    // on the upper mids (the harshness)
    const double freqs[kBands] = {rack ? kBand1BeforeHz : kBand1DefaultHz, 3000.0}, ranges[kBands] = {rack ? kBand1BeforeRangeDb : kBand1DefaultRangeDb, 6.0},
                 thresholds[kBands] = {kBand1DefaultThresholdDb, smacheratr::kClarityThresholdDb};
    for (int b = 0; b < kBands; ++b)
    {
        const std::string n = "Band " + std::to_string (b + 1) + " ";
        v.push_back (toggle (bandParam (b, kOn), keep (n + "On"), "On", true));
        v.push_back (like (bandParam (b, kFreq), smacheratr::kClarityFreq, n + "Frequency", "Freq", freqs[b]));
        v.push_back (like (bandParam (b, kWidth), smacheratr::kClarityWidth, n + "Width", "Width", 2.0));
        v.push_back (like (bandParam (b, kRange), smacheratr::kClarityRange, n + "Range", "Range", ranges[b]));
        v.push_back (threshold (bandParam (b, kThreshold), n + "Threshold", thresholds[b]));
    }
    // the Sub band: Smacheratr's (no button, its Range 0 by default: it cuts nothing until it gets one)
    v.push_back (like (kSubOn, smacheratr::kClaritySub, "Sub (unused)", "Sub", 0.0));
    v.push_back (like (kSubFreq, smacheratr::kClaritySubFreq, "Sub Frequency", "Freq", smacheratr::kSubDefaultHz));
    v.push_back (like (kSubRange, smacheratr::kClaritySubRange, "Sub Range", "Range", 0.0));
    v.push_back (threshold (kSubThreshold, "Sub Threshold", smacheratr::kClarityThresholdDb));
    pk::addTailParams (v, kTailBase);
    smacheratr::addTailExtParams (v, kTailExtBase);
    smacheratr::addTailExt2Params (v, kTailExt2Base);
    // the High band and No Overlap: Smacheratr's (the High band as the Sub band, No Overlap off)
    v.push_back (like (kHighOn, smacheratr::kClarityHigh, "High (unused)", "High", 0.0));
    v.push_back (like (kHighFreq, smacheratr::kClarityHighFreq, "High Frequency", "Freq", smacheratr::kHighDefaultHz));
    v.push_back (like (kHighRange, smacheratr::kClarityHighRange, "High Range", "Range", 0.0));
    v.push_back (threshold (kHighThreshold, "High Threshold", smacheratr::kClarityThresholdDb));
    v.push_back (like (kNoOverlap, smacheratr::kClarityNoOverlap, "No Overlap", "No Overlap", 0.0));
    smacheratr::addTailExt3Params (v, kTailExt3Base);
    v.push_back (like (kSlope, smacheratr::kClaritySlope, "Slope", "Slope", smacheratr::kSlopeSignature)); // (12 / 12 up to 0.24)
    // the glue switches: Smacheratr's (off), without its "Gentlr " in front
    for (int g = 0; g < smacheratr::kGluePairs; ++g)
    {
        const ParamInfo& pi = sm.info (smacheratr::kClarityGlueIds[g]);
        v.push_back (like (kGlueIds[g], smacheratr::kClarityGlueIds[g], std::string (pi.name).substr (7), pi.shortName, 0.0));
    }
    smacheratr::addTailExt4Params (v, kTailExt4Base);
    static_assert (kNumParams == kTailExt4Base + pk::kTailExt4Fields, "the end saturator's fifth block is the last");
    return v;
}

std::vector<ParamInfo> buildSliderTable ()
{
    const auto& sm = smacheratr::paramTable ();
    std::vector<ParamInfo> v;
    for (uint32_t id = 0; id < sm.size (); ++id)
    {
        ParamInfo pi = sm.info (id);
        for (int k = 0; k < kAllBands; ++k)
            if (id == smacheratr::kGentlrThresholdIds[k])
            {
                const ParamInfo& own = paramTable ().info (thresholdParam (k));
                pi.min = own.min;
                pi.max = own.max;
                pi.def = own.def;
            }
        v.push_back (pi);
    }
    return v;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable (false));
    return t;
}

const ParamTable& rackTable ()
{
    static const ParamTable t (buildTable (true));
    return t;
}

namespace {
bool differs (uint32_t id)
{
    for (int k = 0; k < kAllBands; ++k)
        if (id == thresholdParam (k))
            return true;
    return false;
}
} // namespace

double rackFromPlugin (uint32_t id, double norm)
{
    return differs (id) ? rackTable ().toNormalized (id, paramTable ().toPlain (id, norm)) : norm;
}

double pluginFromRack (uint32_t id, double norm)
{
    return differs (id) ? paramTable ().toNormalized (id, rackTable ().toPlain (id, norm)) : norm;
}

void rackFromPlugin (std::vector<double>& norm)
{
    for (uint32_t id = 0; id < norm.size (); ++id)
        norm[id] = rackFromPlugin (id, norm[id]);
}

void pluginFromRack (std::vector<double>& norm)
{
    for (uint32_t id = 0; id < norm.size (); ++id)
        norm[id] = pluginFromRack (id, norm[id]);
}

const ParamTable& sliderTable ()
{
    static const ParamTable t (buildSliderTable ());
    return t;
}

} // namespace gentlr

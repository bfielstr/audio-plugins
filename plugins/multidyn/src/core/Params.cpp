#include "Params.h"

#include "Crossover.h"

#include "smacheratr/src/core/TailExt.h"

#include <algorithm>
#include <vector>

namespace multidyn {

using namespace pk;
using namespace pk::make;

namespace {

std::vector<ParamInfo> buildTable ()
{
    std::vector<ParamInfo> t;
    t.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db)); // 0 dB = the baked kBakedMasterDb
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

    // Defaults (3 bands at 88.3 Hz and 2.5 kHz): Live's Multiband Dynamics "OTT" preset (the sound Xfer's
    // OTT gets close to): Above 1:66.7 and Below 1:4.17 on every band, Amount and Time 100 %. The
    // preset's band Output gains are baked in (kBakedOutputDb): every gain control defaults to 0 dB.
    // A fourth band (above Crossover 3, 8 kHz) starts like the top band of three.
    struct BandDefaults
    {
        double below, belowRatio, above, aboveRatio, attack, release;
    };
    const BandDefaults defs[kMaxBands] = {
        {-40.8, 4.17, -33.8, 66.7, 47.8, 282.0}, // band 1 (low)
        {-41.8, 4.17, -30.2, 66.7, 22.4, 282.0}, // band 2 (mid)
        {-40.8, 4.17, -35.5, 66.7, 13.5, 132.0}, // band 3 (high, with three bands)
        {-40.8, 4.17, -35.5, 66.7, 13.5, 132.0}, // band 4 (high, with four)
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
    t.push_back (choice (kMode, "Mode (unused)", "Mode", {"Base", "Character"}, kCharacter));
    t.push_back (toggle (kPreLimit, "Pre-Limit", "Pre-Limit", false));
    t.push_back (real (kPreLimitCeiling, "Pre-Limit Above Threshold", "Ceiling", -12.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    // the end-of-chain Smacheratr (off, Drive 0 dB): its fields line up with kSatOn ... kSatPreLimitThreshold
    static_assert (kSatPreLimit == kSatOn + pk::kTailPreLimit && kSatDrive == kSatOn + pk::kTailDrive &&
                   kSatPostClip == kSatOn + pk::kTailPostClip && kSatMix == kSatOn + pk::kTailMix &&
                   kSatPreLimitThreshold == kSatOn + pk::kTailThreshold);
    pk::addTailParams (t, kSatOn);
    t.push_back (real (kRmsWindow, "RMS Window", "RMS", 5.0, 300.0, 50.0, Curve::Log, Disp::Ms));
    t.push_back (percent (kSoften, "Soften", "Soften", 0.5));
    smacheratr::addTailExtParams (t, kSatExtBase);
    smacheratr::addTailExt2Params (t, kSatExt2Base);
    t.push_back (choice (kXoverSlope, "Crossover Slope", "Slope", {"6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB", "60 dB", "72 dB", "84 dB", "96 dB", "Brickwall"},
                         kXover24));
    t.push_back (toggle (kSoftenColor, "Soften Color", "Color", false));
    // the Sub band: off; when on, below 40 Hz, compressed 4:1 above -18 dB with slow enough times for
    // the longest cycles (a 20 Hz cycle is 50 ms)
    t.push_back (toggle (kSubOn, "Sub Band", "Sub", false));
    t.push_back (real (kSubFreq, "Sub Frequency", "Sub Freq", 20.0, 100.0, 40.0, Curve::Log, Disp::Hz));
    t.push_back (real (kSubThresh, "Sub Threshold", "Thresh", -80.0, 0.0, -18.0, Curve::Linear, Disp::Db));
    t.push_back (real (kSubRatio, "Sub Ratio", "Ratio", kRatioMin, kRatioInf, 4.0, Curve::Ratio, Disp::Ratio));
    t.push_back (real (kSubAttack, "Sub Attack", "Attack", 0.1, 1000.0, 30.0, Curve::Log, Disp::Ms));
    t.push_back (real (kSubRelease, "Sub Release", "Release", 1.0, 3000.0, 200.0, Curve::Log, Disp::Ms));
    t.push_back (real (kSubOutput, "Sub Output Gain", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    t.push_back (choice (kStyle, "Style", "Style", {"OTT", "Character"}, kStyleOtt));
    t.push_back (real (kSubInput, "Sub Input Gain", "Input", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    static_assert (kNumParams == kSubInput + 1, "the table ends with Sub Input");
    return t;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

double oldBakedShiftDb (uint32_t id)
{
    if (id == kOutput)
        return kOldBakedMasterDb - kBakedMasterDb;
    if (id >= kBandBase && id < kBandBase + kMaxBands * kBandBlock)
    {
        const int band = (int)((id - kBandBase) / kBandBlock), field = (int)((id - kBandBase) % kBandBlock);
        if (field == kBandInput)
            return kOldBakedInputDb - kBakedInputDb;
        if (field == kBandOutput)
            return kOldBakedOutputDb[band] - kBakedOutputDb[band];
    }
    return 0.0;
}

double migrateOldBakedNorm (uint32_t id, double norm)
{
    const double shift = oldBakedShiftDb (id);
    if (shift == 0.0)
        return norm;
    const auto& info = paramTable ().info (id);
    return toNormalized (id, std::clamp (toPlain (id, norm) + shift, info.min, info.max));
}

void migrateOldBaked (double* norm)
{
    for (uint32_t id = 0; id < kNumParams; ++id)
        norm[id] = migrateOldBakedNorm (id, norm[id]);
}

} // namespace multidyn

#include "Params.h"

#include "Crossover.h"

#include "smacheratr/src/core/TailExt.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace levlr {

using namespace pk;
using namespace pk::make;

namespace {

std::vector<ParamInfo> buildTable ()
{
    std::vector<ParamInfo> v;
    v.push_back (choice (kSlope, "Slope", "Slope",
                         {"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "60 dB/oct", "72 dB/oct", "84 dB/oct", "96 dB/oct"}, 1));
    v.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    // lows / low mids / high mids / highs
    const double xovers[kCrossovers] = {120.0, 1000.0, 6000.0};
    for (int k = 0; k < kCrossovers; ++k)
        v.push_back (real (xoverParam (k), keep ("Crossover " + std::to_string (k + 1)), keep ("X" + std::to_string (k + 1)),
                           kMinXoverHz, kMaxXoverHz, xovers[k], Curve::Log, Disp::Hz));
    pk::addTailParams (v, kTailBase);
    for (int b = 0; b < kBands; ++b)
    {
        const std::string n = "Band " + std::to_string (b + 1) + " ";
        v.push_back (real (bandParam (b, kGain), keep (n + "Gain"), "Gain", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (toggle (bandParam (b, kMute), keep (n + "Mute"), "Mute", false));
        v.push_back (toggle (bandParam (b, kSolo), keep (n + "Solo"), "Solo", false));
    }
    smacheratr::addTailExtParams (v, kTailExtBase);
    smacheratr::addTailExt2Params (v, kTailExt2Base);
    v.push_back (choice (kBandCount, "Bands", "Bands", {"1", "2", "3", "4"}, kBands - 1));
    for (int b = 0; b < kBands; ++b)
    {
        const std::string n = "Band " + std::to_string (b + 1) + " ";
        v.push_back (real (driveParam (b, kDriveDb), keep (n + "Drive"), "Drive", 0.0, kMaxDriveDb, 0.0, Curve::Linear, Disp::Db));
        v.push_back (choice (driveParam (b, kDriveType), keep (n + "Drive Type"), "Type", {"Analog", "Tape", "Tube", "Hard Clip", "Fold"},
                             kDriveAnalog));
    }
    smacheratr::addTailExt3Params (v, kTailExt3Base);
    smacheratr::addTailExt4Params (v, kTailExt4Base);
    // (4x, what the drives always ran at before)
    v.push_back (choice (kDriveOversampling, "Drive Oversampling", "Oversampling", {"Off", "2x", "4x"}, kDriveOs4x));
    static_assert (kNumParams == kDriveOversampling + 1, "the drives' Oversampling is the last");
    return v;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

void migrateState (int version, double norm[kNumParams], const bool has[kNumParams])
{
    // version 1's Slope was 12 / 24 / 48 dB/oct: the same slope among the eight
    if (version < 2 && has[kSlope])
    {
        static const int kOld[3] = {kSlope12, kSlope24, kSlope48};
        const int old = std::clamp ((int)std::lround (norm[kSlope] * 2.0), 0, 2);
        norm[kSlope] = toNormalized (kSlope, kOld[old]);
    }
    // before Bands and the drives: all four bands, clean (so it sounds as it did, only later by the
    // drives' latency)
    if (version < 3)
        for (uint32_t id = kFirstAddedAfter060; id < kEndAddedAfter060; ++id)
            norm[id] = defaultNormalized (id);
    // before 4 the end saturator's Sub and High bands had a button each (off by default) and Ranges of 8
    // and 6 dB by default: one that was off gets Range 0, one that was on keeps its Range (the same sound)
    if (version < 4)
        smacheratr::subHighStateToRange (norm, has, kTailExt2Base + pk::kTailExt2Sub, kTailExt2Base + pk::kTailExt2SubRange,
                                         kTailExt3Base + pk::kTailExt3High, kTailExt3Base + pk::kTailExt3HighRange);
    // before 5 the end saturator's Gentlr bands had one shape: Classic, the same sound (a new instance
    // gets 12 / 12)
    if (version < 5)
        norm[kTailExt3Base + pk::kTailExt3Slope] = smacheratr::classicSlopeNorm ();
    // before 6 the end saturator's Oversampling was its Hi-Quality switch: on is 4x, off is Off
    if (version < kOversamplingChoiceVersion)
        smacheratr::tailOversamplingFromHiQuality (norm, has, kTailExtBase);
}

} // namespace levlr

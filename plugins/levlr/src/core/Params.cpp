#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

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
    static_assert (kNumParams == kDriveBase + kBands * kDriveBlock, "the bands' drives are the last block");
    return v;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

} // namespace levlr

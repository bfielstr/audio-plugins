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
    v.push_back (choice (kSlope, "Slope", "Slope", {"12 dB/oct", "24 dB/oct", "48 dB/oct"}, 1));
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
    return v;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

} // namespace levlr

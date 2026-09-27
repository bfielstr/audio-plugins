#include "Params.h"

#include <vector>

namespace multidyn {

using namespace pk;
using namespace pk::make;

namespace {

std::vector<ParamInfo> buildTable ()
{
    std::vector<ParamInfo> t;
    t.push_back (real (kOutput, "Output", "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    t.push_back (percent (kAmount, "Amount", "Amount", 1.0));
    t.push_back (real (kTime, "Time", "Time", 0.1, 10.0, 1.0, Curve::Log, Disp::Percent));
    t.push_back (toggle (kSoftKnee, "Soft Knee", "Soft Knee", true));
    t.push_back (choice (kDetector, "Peak/RMS", "Detect", {"Peak", "RMS"}, kRms));
    t.push_back (choice (kBands, "Bands", "Bands", {"1", "2", "3", "4"}, 2));
    t.push_back (real (kXover1, "Crossover 1", "X1", 20.0, 16000.0, 120.0, Curve::Log, Disp::Hz));
    t.push_back (real (kXover2, "Crossover 2", "X2", 20.0, 16000.0, 1200.0, Curve::Log, Disp::Hz));
    t.push_back (real (kXover3, "Crossover 3", "X3", 20.0, 16000.0, 6000.0, Curve::Log, Disp::Hz));
    t.push_back (toggle (kScOn, "Sidechain On", "Sidechain", false));
    t.push_back (real (kScGain, "Sidechain Gain", "SC Gain", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    t.push_back (percent (kScMix, "Sidechain Dry/Wet", "SC Mix", 1.0));
    t.push_back (toggle (kScListen, "Sidechain Listen", "Listen", false));

    for (int b = 0; b < kMaxBands; ++b)
    {
        const std::string n = "Band " + std::to_string (b + 1);
        auto id = [b] (int f) { return bandParam (b, f); };
        t.push_back (toggle (id (kBandActive), keep (n + " Active"), keep (n + " On"), true));
        t.push_back (toggle (id (kBandSolo), keep (n + " Solo"), "Solo", false));
        t.push_back (real (id (kBandInput), keep (n + " Input Gain"), "Input", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        t.push_back (real (id (kBandOutput), keep (n + " Output Gain"), "Output", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        t.push_back (real (id (kAboveThresh), keep (n + " Above Threshold"), "Above", -70.0, 6.0, -12.0, Curve::Linear, Disp::Db));
        t.push_back (real (id (kAboveRatio), keep (n + " Above Ratio"), "Ratio", 0.5, 50.0, 1.0, Curve::Ratio, Disp::Ratio));
        t.push_back (real (id (kBelowThresh), keep (n + " Below Threshold"), "Below", -70.0, 6.0, -40.0, Curve::Linear, Disp::Db));
        t.push_back (real (id (kBelowRatio), keep (n + " Below Ratio"), "Ratio", 0.5, 50.0, 1.0, Curve::Ratio, Disp::Ratio));
        t.push_back (real (id (kAttack), keep (n + " Attack"), "Attack", 0.1, 1000.0, 10.0, Curve::Log, Disp::Ms));
        t.push_back (real (id (kRelease), keep (n + " Release"), "Release", 1.0, 3000.0, 150.0, Curve::Log, Disp::Ms));
    }
    return t;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

} // namespace multidyn

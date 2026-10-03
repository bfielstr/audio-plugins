#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <string>
#include <vector>

namespace wubr {

using namespace pk;
using namespace pk::make;

namespace {

std::vector<ParamInfo> buildTable ()
{
    std::vector<ParamInfo> v;
    v.push_back (choice (kMode, "Mode", "Mode", {"LFO", "Envelope"}, kLfo));
    v.push_back (choice (kTrigger, "Envelope Trigger", "Trigger", {"MIDI", "Transient"}, kMidi));
    v.push_back (real (kSensitivity, "Transient Sensitivity", "Sens", 3.0, 24.0, 8.0, Curve::Linear, Disp::Db));
    v.push_back (percent (kDryWet, "Dry/Wet", "Dry/Wet", 1.0));
    v.push_back (real (kOutput, "Output", "Output", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
    pk::addTailParams (v, kTailBase);

    // the bands: band 1 low (a bass wub), band 2 higher; both on, sweeping their centres, free at 0.75 Hz
    const double freqs[kBands] = {120.0, 2000.0};
    for (int b = 0; b < kBands; ++b)
    {
        const std::string n = "Band " + std::to_string (b + 1) + " ";
        auto id = [b] (uint32_t f) { return bandParam (b, f); };
        v.push_back (toggle (id (kBandOn), keep (n + "On"), "On", true));
        v.push_back (choice (id (kTarget), keep (n + "Target"), "Target", {"Gain", "Frequency", "Both"}, kTargetFreq));
        v.push_back (real (id (kFreq), keep (n + "Frequency"), "Freq", 20.0, 20000.0, freqs[b], Curve::Log, Disp::Hz));
        v.push_back (real (id (kWidth), keep (n + "Width"), "Width", 0.5, 4.0, 1.5, Curve::Linear, Disp::Number));
        v.push_back (real (id (kGain), keep (n + "Gain"), "Gain", -24.0, 24.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (id (kDepth), keep (n + "Depth"), "Depth", -24.0, 24.0, 12.0, Curve::Linear, Disp::Db));
        v.push_back (real (id (kSweep), keep (n + "Sweep"), "Sweep", 0.0, 6.0, 2.0, Curve::Linear, Disp::Number));
        v.push_back (choice (id (kRateMode), keep (n + "Rate Mode"), "Rate", {"Sync", "Free"}, kFree));
        v.push_back (choice (id (kSync), keep (n + "Sync Rate"), "Sync",
                             {"1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2T", "1/2",
                              "1/2D", "1 bar", "2 bars", "4 bars"},
                             8));
        v.push_back (real (id (kRateHz), keep (n + "Rate"), "Rate", 0.05, 40.0, 0.75, Curve::Log, Disp::Hz));
        v.push_back (real (id (kPhase), keep (n + "Phase"), "Phase", 0.0, 360.0, 0.0, Curve::Linear, Disp::Degrees));
        v.push_back (integer (id (kPointCount), keep (n + "Points"), "Points", 2.0, (double)kMaxPoints, 3.0, Disp::Plain));
        v.push_back (integer (id (kHold), keep (n + "Hold Point"), "Hold", 1.0, (double)kMaxPoints, (double)kMaxPoints, Disp::Plain));
        // the default shape: down, up, down (a triangle through the cycle)
        const double defX[kMaxPoints] = {0.0, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
        const double defY[kMaxPoints] = {-1.0, 1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0};
        for (int i = 0; i < kMaxPoints; ++i)
        {
            const std::string pn = n + "Point " + std::to_string (i + 1) + " ";
            v.push_back (real (pointParam (b, i, kPtX), keep (pn + "Time"), "Time", 0.0, 1.0, defX[i], Curve::Linear, Disp::Percent));
            v.push_back (real (pointParam (b, i, kPtY), keep (pn + "Level"), "Level", -1.0, 1.0, defY[i], Curve::Linear, Disp::Percent));
            v.push_back (real (pointParam (b, i, kPtCurve), keep (pn + "Curve"), "Curve", -1.0, 1.0, 0.0, Curve::Linear, Disp::Percent));
        }
    }
    smacheratr::addTailExtParams (v, kTailExtBase);
    static_assert (kLinkRate == kTailExtBase + pk::kTailExtFields, "the link comes right after the end saturator's block");
    v.push_back (toggle (kLinkRate, "Link Rates", "Link", true));
    static_assert (kTailExt2Base == kLinkRate + 1 && kNumParams == kTailExt3Base + pk::kTailExt3Fields,
                   "Gently's Advanced block comes after the link, and the High band's block is the last");
    smacheratr::addTailExt2Params (v, kTailExt2Base);
    smacheratr::addTailExt3Params (v, kTailExt3Base);
    return v;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

} // namespace wubr

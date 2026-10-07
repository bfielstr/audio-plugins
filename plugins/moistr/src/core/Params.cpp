#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <vector>

namespace moistr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        // INPUT
        v.push_back (percent (kDrive, "Drive", "Drive", 0.1));
        // BANDS: a clear gap between Mid (the low mids) and High by default, the hollow middle
        v.push_back (real (kLowFreq, "Low Freq", "Freq", 40.0, 1000.0, 180.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kLowRes, "Low Res", "Res", 0.15));
        v.push_back (real (kLowLevel, "Low Level", "Level", kLevelOffDb, 12.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kMidFreq, "Mid Freq", "Freq", 100.0, 4000.0, 450.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kMidRes, "Mid Res", "Res", 0.35));
        v.push_back (real (kMidLevel, "Mid Level", "Level", kLevelOffDb, 12.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kHighFreq, "High Freq", "Freq", 500.0, 16000.0, 3000.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kHighRes, "High Res", "Res", 0.15));
        v.push_back (real (kHighLevel, "High Level", "Level", kLevelOffDb, 12.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kGap, "Gap", "Gap", -1.0, 1.0, 0.0, Curve::Linear, Disp::Percent));
        v.push_back (choice (kSlope, "Slope", "Slope", {"12 dB", "24 dB"}, kSlope12));
        // MOVEMENT
        v.push_back (percent (kMovement, "Movement", "Movement", 0.5));
        v.push_back (real (kRate, "Rate", "Rate", 0.05, 2.0, 0.3, Curve::Log, Disp::Hz));
        v.push_back (toggle (kSync, "Sync", "Sync", false));
        v.push_back (choice (kSyncRate, "Sync Rate", "Sync Rate", {"4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8"}, 2));
        v.push_back (percent (kLowMove, "Low Move", "Low Move", 0.2));
        v.push_back (percent (kMidMove, "Mid Move", "Mid Move", 0.5));
        v.push_back (percent (kHighMove, "High Move", "High Move", 1.0));
        v.push_back (real (kLevelMove, "Level Move", "Levels", 0.0, 12.0, 4.0, Curve::Linear, Disp::Db));
        v.push_back (integer (kSeed, "Seed", "Seed", kMinSeed, kMaxSeed, kMinSeed, Disp::Plain));
        // GLUE
        v.push_back (percent (kGlue, "Glue", "Glue", 0.4));
        v.push_back (percent (kGrit, "Grit", "Grit", 0.2));
        v.push_back (choice (kPasses, "Passes", "Passes", {"1", "2"}, kPasses1));
        // OUTPUT
        v.push_back (percent (kMix, "Mix", "Mix", 1.0));
        v.push_back (real (kOutput, "Output", "Output", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        // the end saturator
        pk::addTailParams (v, kTailBase);
        smacheratr::addTailExtParams (v, kTailExtBase);
        smacheratr::addTailExt2Params (v, kTailExt2Base);
        smacheratr::addTailExt3Params (v, kTailExt3Base);
        smacheratr::addTailExt4Params (v, kTailExt4Base);
        // the multiband split (0.19)
        v.push_back (choice (kBandCount, "Bands", "Bands", {"3 Bands", "4 Bands"}, kBands3));
        v.push_back (real (kXoverMid, "Mid X", "Mid X", 400.0, 6000.0, 1500.0, Curve::Log, Disp::Hz));
        v.push_back (real (kXoverHigh, "High X", "High X", 1500.0, 16000.0, 5000.0, Curve::Log, Disp::Hz));
        v.push_back (real (kAirLevel, "Air Level", "Level", kLevelOffDb, 12.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (percent (kAirMove, "Air Move", "Air Move", 1.0));
        v.push_back (real (kRise, "Rise", "Rise", 0.25, 4.0, 1.0, Curve::Log, Disp::Plain));
        v.push_back (real (kFall, "Fall", "Fall", 0.25, 4.0, 1.0, Curve::Log, Disp::Plain));
        v.push_back (real (kDepth, "Depth", "Depth", 0.0, 48.0, 24.0, Curve::Linear, Disp::Db));
        // the frequency shifter (on the bands above Low only)
        v.push_back (toggle (kShiftOn, "Shift On", "Shift", false));
        v.push_back (real (kShift, "Shift", "Shift", -500.0, 500.0, 0.0, Curve::Linear, Disp::Hz));
        v.push_back (percent (kShiftMix, "Shift Mix", "Shift Mix", 1.0));
        // the second seed, more extreme movement, the Low band's push and dip (0.22)
        v.push_back (integer (kSeedB, "Seed B", "Seed B", kMinSeed, kMaxSeed, 2, Disp::Plain));
        v.push_back (percent (kSeedBlend, "Seed Blend", "Blend", 0.0));
        v.push_back (real (kDensity, "Density", "Density", 0.25, 8.0, 1.0, Curve::Log, Disp::Plain));
        v.push_back (real (kLowPush, "Low Push", "Push", 0.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kLowDip, "Low Dip", "Dip", 0.0, 6.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (toggle (kDropOut, "Drop Out", "Drop Out", false));
        v.push_back (real (kSpeed, "Speed", "Speed", 1.0, 16.0, 1.0, Curve::Log, Disp::Plain));
        // Link and Liquid (0.23)
        v.push_back (percent (kLink, "Link", "Link", 0.0));
        v.push_back (percent (kLiquid, "Liquid", "Liquid", 0.0));
        v.push_back (percent (kLiquidRes, "Liquid Res", "Res", 0.5));
        v.push_back (real (kLiquidLow, "Liquid Low", "Low", kLiquidLowMin, kLiquidLowMax, 250.0, Curve::Log, Disp::Hz));
        v.push_back (real (kLiquidHigh, "Liquid High", "High", kLiquidHighMin, kLiquidHighMax, 1600.0, Curve::Log, Disp::Hz));
        return v;
    }());
    return t;
}

} // namespace moistr

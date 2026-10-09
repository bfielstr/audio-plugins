#include "Params.h"

#include "Dsp.h"
#include "Gesture.h"

#include "smacheratr/src/core/TailExt.h"
#include "smemplr/src/core/FxSlot.h"

#include <string>
#include <vector>

namespace moistr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        // INPUT
        // (0.24: the SWEEP stage is the default sound, so these start neutral; legacyDefaultNormalized has the old ones)
        v.push_back (percent (kDrive, "Drive", "Drive", 0.0));
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
        v.push_back (percent (kMovement, "Movement", "Movement", 0.0));
        v.push_back (real (kRate, "Rate", "Rate", 0.05, 2.0, 0.3, Curve::Log, Disp::Hz));
        v.push_back (toggle (kSync, "Sync", "Sync", false));
        v.push_back (choice (kSyncRate, "Sync Rate", "Sync Rate", {"4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8"}, 2));
        v.push_back (percent (kLowMove, "Low Move", "Low Move", 0.2));
        v.push_back (percent (kMidMove, "Mid Move", "Mid Move", 0.5));
        v.push_back (percent (kHighMove, "High Move", "High Move", 1.0));
        v.push_back (real (kLevelMove, "Level Move", "Levels", 0.0, 12.0, 4.0, Curve::Linear, Disp::Db));
        v.push_back (integer (kSeed, "Seed", "Seed", kMinSeed, kMaxSeed, kMinSeed, Disp::Plain));
        // GLUE
        v.push_back (percent (kGlue, "Glue", "Glue", 0.0));
        v.push_back (percent (kGrit, "Grit", "Grit", 0.0));
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
        // the SWEEP stage (0.24; 0.26 the "Ocean" recipe, kOcean*): eight sweeping bells, the High Shelf (off),
        // the saturator at 14 dB on its Soft curve, its sub option (kOceanSub), Tone at 7 kHz
        const std::vector<const char*> syncRates {"4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8"};
        v.push_back (toggle (kSweep, "Sweep", "Sweep", true));
        v.push_back (real (kSweepDrive, "Sweep Drive", "Drive", 0.0, kSweepDriveMax, kOceanDriveDb, Curve::Linear, Disp::Db));
        // A and B (their IDs and ranges from 0.24), then the High Shelf
        auto bell = [&] (int b) {
            static const char* const letters[kNumBells] = {"A", "B", "C", "D", "E", "F", "G", "H"};
            auto name = [&] (const char* what) { return keep (std::string (letters[b]) + " " + what); };
            const BellRecipe& d = kOceanBells[b];
            const double top = b < 2 ? kBellFreqMax : kBellFreqMaxWide;
            const uint32_t base = bellRateId (b);
            v.push_back (real (base + kBellRate, name ("Rate"), "Rate", kSweepRateMin, kSweepRateMax, d.rate, Curve::Log, Disp::Hz));
            v.push_back (toggle (base + kBellSync, name ("Sync"), "Sync", false));
            v.push_back (choice (base + kBellSyncRate, name ("Sync Rate"), "Sync Rate", syncRates, 2));
            v.push_back (real (base + kBellLow, name ("Low"), "Low", kBellFreqMin, top, d.low, Curve::Log, Disp::Hz));
            v.push_back (real (base + kBellHigh, name ("High"), "High", kBellFreqMin, top, d.high, Curve::Log, Disp::Hz));
            v.push_back (real (base + kBellGain, name ("Gain"), "Gain", -24.0, 24.0, d.gainDb, Curve::Linear, Disp::Db));
            v.push_back (real (base + kBellWidth, name ("Width"), "Width", kBellQMin, kBellQMax, d.q, Curve::Log, Disp::Number));
            v.push_back (real (base + kBellPhase, name ("Phase"), "Phase", 0.0, 360.0, d.phaseRad * 180.0 / dsp::kPi, Curve::Linear, Disp::Degrees));
        };
        bell (0);
        bell (1);
        v.push_back (toggle (kShelf, "High Shelf", "High Shelf", false));
        v.push_back (real (kShelfRate, "Shelf Rate", "Rate", kSweepRateMin, kSweepRateMax, 0.53, Curve::Log, Disp::Hz));
        v.push_back (real (kShelfLow, "Shelf Low", "Low", 50.0, 2000.0, 100.0, Curve::Log, Disp::Hz));
        v.push_back (real (kShelfHigh, "Shelf High", "High", 300.0, 5000.0, 1000.0, Curve::Log, Disp::Hz));
        v.push_back (real (kShelfMin, "Shelf Min", "Min", -24.0, 0.0, -18.0, Curve::Linear, Disp::Db));
        v.push_back (real (kShelfMax, "Shelf Max", "Max", -12.0, 12.0, 6.0, Curve::Linear, Disp::Db));
        v.push_back (real (kShelfQ, "Shelf Q", "Q", kShelfQMin, kShelfQMax, 18.0, Curve::Log, Disp::Number));
        v.push_back (percent (kShelfWander, "Wander", "Wander", 0.5));
        v.push_back (percent (kShelfTilt, "Tilt", "Tilt", 0.65));
        // 0.26: the saturator's Curve, Tone, Clean Sub, Sub Boost, the bells' switches and bells C .. H
        v.push_back (choice (kSweepCurve, "Curve", "Curve", {"Hard", "Soft"}, kCurveSoft));
        v.push_back (toggle (kToneOn, "Tone On", "Tone", true));
        v.push_back (real (kTone, "Tone", "Tone", kToneMin, kToneMax, kOceanToneHz, Curve::Log, Disp::Hz));
        v.push_back (toggle (kCleanSub, "Clean Sub", "Clean Sub", kOceanSub == OceanSub::CleanSub));
        v.push_back (real (kSplitFreq, "Split Freq", "Split", kSplitFreqMin, kSplitFreqMax, kOceanSplitHz, Curve::Log, Disp::Hz));
        v.push_back (real (kSplitLevel, "Split Level", "Level", kSplitLevelMin, kSplitLevelMax, kOceanSplitLevelDb, Curve::Linear, Disp::Db));
        v.push_back (real (kSplitDrive, "Split Drive", "Drive", 0.0, kSplitDriveMax, kOceanSplitDriveDb, Curve::Linear, Disp::Db));
        v.push_back (toggle (kSubBoost, "Sub Boost", "Sub Boost", kOceanSub == OceanSub::SubBoost));
        v.push_back (real (kSubFreq, "Sub Freq", "Freq", kSubFreqMin, kSubFreqMax, kOceanSubHz, Curve::Log, Disp::Hz));
        v.push_back (percent (kSubLevel, "Sub Level", "Level", kOceanSubLevel));
        v.push_back (toggle (kAOn, "A On", "On", true));
        v.push_back (toggle (kBOn, "B On", "On", true));
        for (int b = 2; b < kNumBells; ++b)
        {
            static const char* const onNames[kNumBells] = {"A On", "B On", "C On", "D On", "E On", "F On", "G On", "H On"};
            v.push_back (toggle (bellOnId (b), onNames[b], "On", true));
            bell (b);
        }
        // 0.27: the gestures (four slots), Intensity and Wobble. Every Target Off and Wobble Amount 0: off.
        std::vector<const char*> gestures;
        for (int g = 0; g < kNumFactoryGestures; ++g)
            gestures.push_back (factoryGestureName (g));
        gestures.push_back ("User");
        const std::vector<const char*> targets (std::begin (kTargetNames), std::begin (kTargetNames) + kNumSlotTargets);
        // (a different gesture in each slot to start from)
        static const int firstGesture[kNumGestureSlots] = {kGestureCellFade, kGestureStutter16, kGestureRateRise, kGestureResonantClose};
        for (int g = 0; g < kNumGestureSlots; ++g)
        {
            auto name = [&] (const char* what) { return keep ("G" + std::to_string (g + 1) + " " + what); };
            v.push_back (choice (gestureId (g, kGestureChoice), name ("Gesture"), "Gesture", gestures, firstGesture[g]));
            v.push_back (choice (gestureId (g, kGestureTarget), name ("Target"), "Target", targets, kTargetOff));
            v.push_back (choice (gestureId (g, kGestureMode), name ("Mode"), "Mode", {"Loop", "Walk"}, kModeLoop));
            v.push_back (choice (gestureId (g, kGestureLength), name ("Length"), "Length", {"Own", "1/2", "1", "2", "4", "8", "16", "32"}, 0));
            v.push_back (choice (gestureId (g, kGestureSpeed), name ("Speed"), "Speed", {"Hold", "x1/8", "x1/4", "x1/2", "x1", "x2", "x4"}, 4));
            v.push_back (percent (gestureId (g, kGesturePosition), name ("Position"), "Position", 0.0));
            v.push_back (percent (gestureId (g, kGestureSmooth), name ("Smooth"), "Smooth", 0.0));
            v.push_back (real (gestureId (g, kGestureDepth), name ("Depth"), "Depth", -1.0, 1.0, 1.0, Curve::Linear, Disp::Percent));
        }
        v.push_back (percent (kIntensity, "Intensity", "Intensity", 1.0));
        v.push_back (real (kWobbleRate, "Wobble Rate", "Rate", kWobbleRateMin, kWobbleRateMax, 2.0, Curve::Log, Disp::Number));
        v.push_back (percent (kWobbleAmount, "Wobble Amount", "Amount", 0.0));
        // 0.30: the one gesture (a Scene: many lanes on one clock). None by default: moistr as 0.27.
        std::vector<const char*> scenes {"None"};
        for (int g = 0; g < kNumFactoryScenes; ++g)
            scenes.push_back (factorySceneName (g));
        scenes.push_back ("User");
        v.push_back (choice (kScene, "Gesture", "Gesture", scenes, kSceneNone));
        v.push_back (choice (kSceneMode, "Gesture Mode", "Mode", {"Loop", "Walk"}, kModeLoop));
        v.push_back (choice (kSceneLength, "Gesture Length", "Length", {"Own", "1/2", "1", "2", "4", "8", "16", "32"}, 0));
        v.push_back (choice (kSceneSpeed, "Gesture Speed", "Speed", {"Hold", "x1/8", "x1/4", "x1/2", "x1", "x2", "x4"}, 4));
        v.push_back (percent (kScenePosition, "Gesture Position", "Position", 0.0));
        v.push_back (percent (kSceneSmooth, "Gesture Smooth", "Smooth", 0.0));
        v.push_back (percent (kSceneAmount, "Gesture Amount", "Amount", 1.0));
        // 0.30: the LAB. Every chain at 0 dB and every slot Empty: moistr as 0.29 (a new instance too: the Neuro
        // recipe is a preset, neuroRecipe). A slot's block is stored normalized (its kind reads it through its own
        // table, as in a smemplr rack slot); its defaults are those of the kind the slot is meant for (labSlotKind),
        // so loading that kind into it starts from the kind's defaults.
        for (int c = 0; c < kNumChains; ++c)
        {
            auto name = [&] (const char* what) { return keep (std::string (kChainNames[c]) + " Chain " + what); };
            v.push_back (real (chainId (c, kChainLevel), name ("Level"), "Level", kLevelOffDb, 12.0, 0.0, Curve::Linear, Disp::Db));
            v.push_back (toggle (chainId (c, kChainMute), name ("Mute"), "Mute", false));
            v.push_back (toggle (chainId (c, kChainSolo), name ("Solo"), "Solo", false));
            v.push_back (toggle (chainId (c, kChainMono), name ("Mono"), "Mono", false));
            v.push_back (toggle (chainId (c, kChainSource), name ("Source"), "Source", false)); // (kept for later)
            for (int k = 1; k <= 3; ++k)
                v.push_back (toggle (chainId (c, (ChainField)(kChainSource + k)), name (keep ("Spare " + std::to_string (k))), "Spare", false));
        }
        std::vector<const char*> kinds;
        for (int t = 0; t < kLabKinds; ++t)
            kinds.push_back (t == 0 ? "Empty" : t < smemplr::kNumFxTypes ? smemplr::fxName (t) : keep ("Kind " + std::to_string (t)));
        for (int s = 0; s < kNumLabSlots; ++s)
        {
            const std::string fx = labSlotName (s);
            v.push_back (choice (labSlotParam (s, kLabType), keep (fx + " Type"), keep (fx), kinds, smemplr::kFxEmpty));
            v.push_back (toggle (labSlotParam (s, kLabOn), keep (fx + " On"), keep (fx + " On"), true));
            const auto& t = smemplr::fxBlockTable (labSlotKind (s));
            for (uint32_t j = 0; j < smemplr::kSlotBlockAll; ++j)
            {
                const std::string n = fx + " " + std::to_string (j + 1);
                v.push_back (real (labBlockParam (s, j), keep (n), keep (n), 0.0, 1.0, j < t.size () ? t.defaultNormalized (j) : 0.0,
                                   Curve::Linear, Disp::Percent));
            }
        }
        // 0.30: Input, Loop Lock, PARA and Sub Guard (Sub Guard on: a state saved before reads it off)
        v.push_back (real (kInput, "Input", "Input", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (toggle (kLoopLock, "Loop Lock", "Loop Lock", false));
        v.push_back (real (kLoopPosition, "Loop Position", "Position", 0.0, kLoopPositionMax, 0.0, Curve::Linear, Disp::Beats));
        v.push_back (real (kLoopWindow, "Loop Window", "Window", kLoopWindowMin, kLoopWindowMax, kLoopWindowDefault, Curve::Log, Disp::Beats));
        v.push_back (choice (kLoopLength, "Loop Length", "Length", {"1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars"}, 4));
        v.push_back (choice (kLoopShape, "Loop Shape", "Shape", {"Wrap", "Bounce"}, kLoopWrap));
        v.push_back (toggle (kParaOn, "Split On", "Split", false));
        v.push_back (real (kParaLpFreq, "LP Freq", "LP Freq", kParaLpMin, kParaLpMax, 250.0, Curve::Log, Disp::Hz));
        v.push_back (real (kParaHpFreq, "HP Freq", "HP Freq", kParaHpMin, kParaHpMax, 250.0, Curve::Log, Disp::Hz));
        v.push_back (percent (kParaLpMove, "LP Move", "LP Move", 0.5));
        v.push_back (real (kParaHpMove, "HP Move", "HP Move", 0.0, kParaHpMoveMax, 2.0, Curve::Linear, Disp::Number));
        v.push_back (percent (kParaHpLevelMove, "HP Level Move", "HP Level", 0.5));
        v.push_back (choice (kParaRate, "Split Rate", "Rate", {"4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16"}, 2));
        v.push_back (percent (kParaMix, "Split Mix", "Mix", 1.0));
        v.push_back (toggle (kSubGuard, "Sub Guard", "Sub Guard", true));
        v.push_back (real (kSubGuardFreq, "Sub Guard Freq", "Freq", kGuardFreqMin, kGuardFreqMax, kGuardFreqDefault, Curve::Log, Disp::Hz));
        v.push_back (real (kSubFloor, "Sub Floor", "Floor", kSubFloorMin, 0.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (toggle (kGuardBells, "Guard Bells", "Guard Bells", false));
        return v;
    }());
    return t;
}

static_assert (kLabType == smemplr::kSlotType && kLabOn == smemplr::kSlotOn && kLabBlock == smemplr::kSlotParams &&
                   kLabSlotFields == smemplr::kSlotFields && smemplr::kNumFxTypes <= kLabKinds,
               "a lab slot is a smemplr rack slot: a Type, an On and its block");

std::string labSlotName (int slot)
{
    if (slot >= postSlot (0))
        return "Post FX " + std::to_string (slot - postSlot (0) + 1);
    return std::string (kChainNames[slot / kChainSlots]) + " FX " + std::to_string (slot % kChainSlots + 1);
}

int labSlotKind (int slot)
{
    if (slot == postSlot (0))
        return smemplr::kFxMultidyn;
    if (slot == postSlot (1))
        return smemplr::kFxSmacheratr;
    if (slot >= postSlot (0))
        return smemplr::kFxEmpty;
    const int k = slot % kChainSlots;
    return k == 0 ? smemplr::kFxSmacheratr : k == 1 ? smemplr::kFxMultidyn : smemplr::kFxEmpty;
}

namespace {
using Values = std::vector<std::pair<uint32_t, double>>;
// a LAB slot holding `kind`, some of the kind's own parameters (its IDs, plain values) set; a kind the slot is not meant
// for (labSlotKind) gets its own defaults in the block first, as the editor loads a kind
void labKind (Values& v, int slot, int kind, std::initializer_list<std::pair<uint32_t, double>> plain)
{
    v.emplace_back (labSlotParam (slot, kLabType), toNormalized (labSlotParam (slot, kLabType), kind));
    const auto& t = smemplr::fxBlockTable (kind);
    if (kind != labSlotKind (slot))
        for (uint32_t j = 0; j < t.size (); ++j)
            if (t.defaultNormalized (j) != defaultNormalized (labBlockParam (slot, j)))
                v.emplace_back (labBlockParam (slot, j), t.defaultNormalized (j));
    for (const auto& [id, x] : plain)
        if (const int64_t j = smemplr::fxBlockOf (kind, id); j >= 0)
            v.emplace_back (labBlockParam (slot, (uint32_t)j), t.toNormalized ((uint32_t)j, x));
}
} // namespace

std::vector<std::pair<uint32_t, double>> neuroRecipe ()
{
    Values v;
    auto set = [&] (uint32_t id, double plain) { v.emplace_back (id, toNormalized (id, plain)); };
    // four bands above a low Low X (Seed 2: 146 Hz), moving in time with the song; a quarter-note Wobble on the top
    set (kBandCount, kBands4);
    set (kSeed, 2.0);
    set (kXoverMid, 900.0);
    set (kXoverHigh, 3500.0);
    set (kMovement, 0.55);
    set (kSync, 1.0);
    set (kSyncRate, 3.0); // (1/2)
    set (kDensity, 2.0);
    set (kMidMove, 0.6);
    set (kHighMove, 1.0);
    set (kAirMove, 1.0);
    set (kDepth, 18.0);
    set (kWobbleRate, 1.0);
    set (kWobbleAmount, 0.85);
    // each band's chain: smacheratr driven into its hard clip (Drive, Output; at 2x, its Gentlr off: a band alone needs
    // neither), then an OTT (Amount) of one band: the chain is the band (with the three chains, a three-band OTT)
    struct ChainRecipe
    {
        double driveDb, outputDb, ott;
    };
    static constexpr ChainRecipe chains[kNumBandChains] = {{22.0, -10.0, 0.6}, {18.0, -10.0, 0.7}, {12.0, -8.0, 0.5}};
    for (int c = 0; c < kNumBandChains; ++c)
    {
        labKind (v, chainSlot (c, 0), smemplr::kFxSmacheratr,
                 {{smacheratr::kOversampling, smacheratr::kOs2x}, {smacheratr::kClarity, 0.0}, {smacheratr::kDrive, chains[c].driveDb},
                  {smacheratr::kPostClip, smacheratr::kPostHard}, {smacheratr::kOutput, chains[c].outputDb}});
        labKind (v, chainSlot (c, 1), smemplr::kFxMultidyn, {{multidyn::kBands, 0.0}, {multidyn::kAmount, chains[c].ott}});
    }
    // POST: an OTT on the chains' sum, then a hard clipper (Gentlr and the pre-limiter off)
    labKind (v, postSlot (0), smemplr::kFxMultidyn, {{multidyn::kAmount, 0.4}});
    labKind (v, postSlot (1), smemplr::kFxSmacheratr,
             {{smacheratr::kDrive, 27.0}, {smacheratr::kPostClip, smacheratr::kPostHard}, {smacheratr::kClarity, 0.0}, {smacheratr::kPreLimit, 0.0},
              {smacheratr::kOutput, -18.0}});
    return v;
}

double defaultNormalized025 (uint32_t id)
{
    switch (id)
    {
        // 0.24 and 0.25: Drive 18 dB on the Hard curve, the High Shelf on, A and B at Q 0.71 from Phase 0, at +-18 dB
        case kSweepDrive: return toNormalized (id, 18.0);
        case kShelf: return 1.0;
        case kAWidth:
        case kBWidth: return toNormalized (id, kBellQDefault);
        case kAGain: return toNormalized (id, 18.0);
        case kBGain: return toNormalized (id, -18.0);
        case kBPhase: return 0.0;
        // what 0.26 added, as it leaves their sound: bells C .. H, Tone, Clean Sub and Sub Boost off, Curve Hard
        case kSweepCurve: return toNormalized (id, kCurveHard);
        case kToneOn:
        case kCleanSub:
        case kSubBoost:
        case kSubGuard: return 0.0; // (0.30's Sub Guard off: the sound kept)
        default: break;
    }
    for (int b = 2; b < kNumBells; ++b)
        if (id == bellOnId (b))
            return 0.0;
    return defaultNormalized (id);
}

double gestureOffNormalized (uint32_t id)
{
    for (int g = 0; g < kNumGestureSlots; ++g)
        if (id == gestureId (g, kGestureTarget))
            return toNormalized (id, kTargetOff);
    return id == kWobbleAmount ? 0.0 : defaultNormalized (id);
}

double defaultNormalizedForVersion (uint32_t id, int version)
{
    if (isLabParam (id))
        return defaultNormalized (id); // (before 0.30 there was no LAB: every slot Empty, every chain at 0 dB)
    if (id == kSubGuard)
        return version < kStateSubGuard ? 0.0 : defaultNormalized (id); // (on in a new instance; off before: the sound kept)
    if (isGestureParam (id))
        return version < 6 ? gestureOffNormalized (id) : defaultNormalized (id);
    return version < 3 ? legacyDefaultNormalized (id) : version < 5 ? defaultNormalized025 (id) : defaultNormalized (id);
}

double legacyDefaultNormalized (uint32_t id)
{
    switch (id)
    {
        case kDrive: return toNormalized (kDrive, 0.1);
        case kMovement: return toNormalized (kMovement, 0.5);
        case kGlue: return toNormalized (kGlue, 0.4);
        case kGrit: return toNormalized (kGrit, 0.2);
        case kSweep: return 0.0; // (off: the sound before 0.24)
        case kSubGuard: return 0.0; // (off: the sound before 0.30)
        default: return defaultNormalized025 (id);
    }
}

} // namespace moistr

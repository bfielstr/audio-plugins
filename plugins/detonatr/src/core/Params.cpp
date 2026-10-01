#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <string>
#include <vector>

namespace detonatr {

using namespace pk;
using namespace pk::make;

// =====================================================================================================
// THE STAGE DEFAULTS: every stage's default settings, in one place (plain values, in the units the
// parameters show). They are the user's settings from their REAPER chain where they sent them; the
// ones marked ESTIMATED were read off knob positions (or not shown) and are a best guess.
// =====================================================================================================
namespace defaults {

struct VocoderDefaults // MVocoder, Vocoder mode, the signal on its own side-chain
{
    bool on;
    double bands, lowHz, highHz;
    int order; // 0 Gentle (one section), 1 Medium (two), 2 Steep (three)
    double attackMs, releaseMs, ratio;
};
constexpr VocoderDefaults kVocoder {.on = true, .bands = 32, .lowHz = 40.0, .highHz = 16000.0, .order = 1,
                                    .attackMs = 2.0 /* ESTIMATED */, .releaseMs = 60.0 /* ESTIMATED */, .ratio = 0.46};

struct SpikeDefaults // oeksound Spiff
{
    bool on;
    int mode; // 0 Cut, 1 Boost
    double depth, sensitivity, decay, sharpness, decayTilt, link, lowHz, highHz, mix, trimDb;
};
constexpr SpikeDefaults kSpike {.on = true, .mode = 1, .depth = 5.1, .sensitivity = 3.7, .decay = 7.1, .sharpness = 1.3,
                                .decayTilt = 0.0, .link = 1.0, .lowHz = 20.0, .highHz = 20000.0, .mix = 1.0, .trimDb = 0.0};

struct MotionDefaults // Tonsturm SpinTracer, "Liquid Debris" (ESTIMATED: the user did not send this one)
{
    bool on;
    double orbs;
    int pattern; // 0 Orbit, 1 Swarm
    double speed, distance, radius, spread, randomness;
    bool floor;
    double mix;
};
constexpr MotionDefaults kMotion {.on = true, .orbs = 6, .pattern = 1, .speed = 18.0, .distance = 3.0, .radius = 2.0, .spread = 0.8,
                                  .randomness = 0.6, .floor = true, .mix = 0.5};

struct TransientDefaults // Sonnox Oxford TransMod
{
    bool on;
    double gainDb, thresholdDb, deadbandDb, ratio, overshootMs, riseMs, recoveryMs, overdrive, outputDb, mix;
};
constexpr TransientDefaults kTransient1 {.on = true, .gainDb = -8.49, .thresholdDb = -80.0, .deadbandDb = 0.0, .ratio = 0.27,
                                         .overshootMs = 25.94, .riseMs = 0.10, .recoveryMs = 77.46, .overdrive = 0.6088,
                                         .outputDb = 0.0, .mix = 1.0};
constexpr TransientDefaults kTransient2 {.on = true, .gainDb = -1.47, .thresholdDb = -14.8, .deadbandDb = 0.0, .ratio = 0.58,
                                         .overshootMs = 6.42, .riseMs = 0.10, .recoveryMs = 77.46, .overdrive = 0.0,
                                         .outputDb = 0.0, .mix = 1.0};

struct LimiterDefaults // FabFilter Pro-L2, Transparent, True Peak on, 4x
{
    bool on;
    double gainDb, ceilingDb, lookaheadMs, attackMs, releaseMs, link;
    bool truePeak;
};
constexpr LimiterDefaults kLimiter1 {.on = true, .gainDb = 4.9, .ceilingDb = 0.0, .lookaheadMs = 1.0 /* ESTIMATED */,
                                     .attackMs = 3.0 /* ESTIMATED */, .releaseMs = 50.0 /* ESTIMATED */, .link = 0.0 /* ESTIMATED */,
                                     .truePeak = true};
constexpr LimiterDefaults kLimiter2 {.on = true, .gainDb = 0.0, .ceilingDb = 0.0, .lookaheadMs = 1.0 /* ESTIMATED */,
                                     .attackMs = 3.0 /* ESTIMATED */, .releaseMs = 50.0 /* ESTIMATED */, .link = 0.0 /* ESTIMATED */,
                                     .truePeak = true};

struct CompDefaults // FabFilter Pro-C 3, TTM style, factory preset "BPF - Explosive" (both the same)
{
    bool on;
    double thresholdDb;
    bool autoThreshold;
    double ratio, attackMs, releaseMs;
    bool autoRelease;
    double kneeDb, rangeDb, holdMs;
    bool autoGain;
    double dryDb, xoverLowHz, xoverHighHz, outputDb;
};
constexpr CompDefaults kComp {.on = true, .thresholdDb = -20.0, .autoThreshold = true, .ratio = 20.0 /* ESTIMATED */,
                              .attackMs = 200.0 /* ESTIMATED */, .releaseMs = 400.0 /* ESTIMATED */, .autoRelease = true,
                              .kneeDb = 3.0 /* ESTIMATED */, .rangeDb = 24.0 /* ESTIMATED (two-thirds of the knob) */, .holdMs = 330.0 /* ESTIMATED */,
                              .autoGain = true, .dryDb = -60.0, .xoverLowHz = 150.0 /* ESTIMATED */,
                              .xoverHighHz = 2500.0 /* ESTIMATED */, .outputDb = 0.0};
constexpr CompDefaults kComp1 = kComp, kComp2 = kComp;

struct TapeDefaults // FabFilter Saturn 2, Warm Tape, two bands, Linear Phase, High Quality
{
    bool on;
    double splitHz;
    double lowDriveDb, lowMix, lowDynamics, lowLevelDb;
    double highDriveDb, highMix, highDynamics, highLevelDb;
};
constexpr TapeDefaults kTape {.on = true, .splitHz = 200.0, .lowDriveDb = 6.0 /* ESTIMATED */, .lowMix = 1.0, .lowDynamics = 0.0,
                              .lowLevelDb = 1.0 /* ESTIMATED */, .highDriveDb = 6.0 /* ESTIMATED */, .highMix = 1.0 /* ESTIMATED */,
                              .highDynamics = 0.0 /* ESTIMATED */, .highLevelDb = 0.0 /* ESTIMATED */};

} // namespace defaults
// =====================================================================================================

namespace {
const char* const kStageNames[kNumStages] = {"Vocoder", "Spike", "Motion", "Transient 1", "Limiter 1",
                                             "Transient 2", "Comp 1", "Comp 2", "Tape", "Limiter 2"};
const char* const kStageShort[kNumStages] = {"Vocoder", "Spike", "Motion", "Trans 1", "Limit 1",
                                             "Trans 2", "Comp 1", "Comp 2", "Tape", "Limit 2"};
} // namespace

const char* stageName (int stage) { return stage >= 0 && stage < kNumStages ? kStageNames[stage] : ""; }
const char* stageShortName (int stage) { return stage >= 0 && stage < kNumStages ? kStageShort[stage] : ""; }

uint32_t stageOnParam (int stage)
{
    switch (stage)
    {
        case kStageVocoder: return kVocOn;
        case kStageSpike: return kSpkOn;
        case kStageMotion: return kMotOn;
        case kStageTransient1: return kTr1Base + kTrOn;
        case kStageLimiter1: return kLim1Base + kLimOn;
        case kStageTransient2: return kTr2Base + kTrOn;
        case kStageComp1: return kComp1Base + kCompOn;
        case kStageComp2: return kComp2Base + kCompOn;
        case kStageTape: return kTapeOn;
        case kStageLimiter2: return kLim2Base + kLimOn;
        default: return kVocOn;
    }
}

int stageOfParam (uint32_t id)
{
    if (id >= kVocOn && id < kSpkOn)
        return kStageVocoder;
    if (id >= kSpkOn && id < kMotOn)
        return kStageSpike;
    if (id >= kMotOn && id < kTr1Base)
        return kStageMotion;
    if (id >= kTr1Base && id < kLim1Base)
        return kStageTransient1;
    if (id >= kLim1Base && id < kTr2Base)
        return kStageLimiter1;
    if (id >= kTr2Base && id < kComp1Base)
        return kStageTransient2;
    if (id >= kComp1Base && id < kComp2Base)
        return kStageComp1;
    if (id >= kComp2Base && id < kTapeOn)
        return kStageComp2;
    if (id >= kTapeOn && id < kLim2Base)
        return kStageTape;
    if (id >= kLim2Base && id < kTailBase)
        return kStageLimiter2;
    return -1;
}

Order resolveOrder (const int chosen[kNumStages])
{
    Order o {};
    bool used[kNumStages] {};
    int k = 0;
    for (int i = 0; i < kNumStages; ++i)
    {
        const int s = chosen[i];
        if (s >= 0 && s < kNumStages && !used[s])
        {
            used[s] = true;
            o.stage[k++] = s;
        }
    }
    for (int s = 0; s < kNumStages; ++s)
        if (!used[s])
            o.stage[k++] = s;
    return o;
}

namespace {

std::string nm (const char* stage, const char* what) { return std::string (stage) + " " + what; }

void addTransient (std::vector<ParamInfo>& v, uint32_t base, const char* stage, const defaults::TransientDefaults& d)
{
    v.push_back (toggle (base + kTrOn, keep (stage), keep (stage), d.on));
    v.push_back (real (base + kTrGain, keep (nm (stage, "Gain")), "Gain", -24.0, 24.0, d.gainDb, Curve::Linear, Disp::Db));
    v.push_back (real (base + kTrThreshold, keep (nm (stage, "Threshold")), "Threshold", -80.0, 0.0, d.thresholdDb, Curve::Linear, Disp::Db));
    v.push_back (real (base + kTrDeadband, keep (nm (stage, "Deadband")), "Deadband", 0.0, 20.0, d.deadbandDb, Curve::Linear, Disp::Db));
    v.push_back (real (base + kTrRatio, keep (nm (stage, "Ratio")), "Ratio", -1.0, 1.0, d.ratio, Curve::Linear, Disp::Number));
    v.push_back (real (base + kTrOvershoot, keep (nm (stage, "Overshoot")), "Overshoot", 0.1, 200.0, d.overshootMs, Curve::Log, Disp::Ms));
    v.push_back (real (base + kTrRise, keep (nm (stage, "Rise Time")), "Rise", 0.01, 50.0, d.riseMs, Curve::Log, Disp::Ms));
    v.push_back (real (base + kTrRecovery, keep (nm (stage, "Recovery")), "Recovery", 1.0, 1000.0, d.recoveryMs, Curve::Log, Disp::Ms));
    v.push_back (percent (base + kTrOverdrive, keep (nm (stage, "Overdrive")), "Overdrive", d.overdrive));
    v.push_back (real (base + kTrOutput, keep (nm (stage, "Output")), "Output", -24.0, 24.0, d.outputDb, Curve::Linear, Disp::Db));
    v.push_back (percent (base + kTrMix, keep (nm (stage, "Mix")), "Mix", d.mix));
}

void addLimiter (std::vector<ParamInfo>& v, uint32_t base, const char* stage, const defaults::LimiterDefaults& d)
{
    v.push_back (toggle (base + kLimOn, keep (stage), keep (stage), d.on));
    v.push_back (real (base + kLimGain, keep (nm (stage, "Gain")), "Gain", 0.0, 30.0, d.gainDb, Curve::Linear, Disp::Db));
    v.push_back (real (base + kLimCeiling, keep (nm (stage, "Ceiling")), "Ceiling", -30.0, 0.0, d.ceilingDb, Curve::Linear, Disp::Db));
    v.push_back (real (base + kLimLookahead, keep (nm (stage, "Lookahead")), "Lookahead", 0.1, 5.0, d.lookaheadMs, Curve::Log, Disp::Ms));
    v.push_back (real (base + kLimAttack, keep (nm (stage, "Attack")), "Attack", 0.1, 100.0, d.attackMs, Curve::Log, Disp::Ms));
    v.push_back (real (base + kLimRelease, keep (nm (stage, "Release")), "Release", 1.0, 2000.0, d.releaseMs, Curve::Log, Disp::Ms));
    v.push_back (percent (base + kLimLink, keep (nm (stage, "Link")), "Link", d.link));
    v.push_back (toggle (base + kLimTruePeak, keep (nm (stage, "True Peak")), "True Peak", d.truePeak));
}

void addComp (std::vector<ParamInfo>& v, uint32_t base, const char* stage, const defaults::CompDefaults& d)
{
    v.push_back (toggle (base + kCompOn, keep (stage), keep (stage), d.on));
    v.push_back (real (base + kCompThreshold, keep (nm (stage, "Threshold")), "Threshold", -60.0, 0.0, d.thresholdDb, Curve::Linear, Disp::Db));
    v.push_back (toggle (base + kCompAutoThreshold, keep (nm (stage, "Auto Threshold")), "Auto Thr", d.autoThreshold));
    v.push_back (real (base + kCompRatio, keep (nm (stage, "Ratio")), "Ratio", 1.0, 50.0, d.ratio, Curve::Log, Disp::Number));
    v.push_back (real (base + kCompAttack, keep (nm (stage, "Attack")), "Attack", 0.01, 250.0, d.attackMs, Curve::Log, Disp::Ms));
    v.push_back (real (base + kCompRelease, keep (nm (stage, "Release")), "Release", 10.0, 2500.0, d.releaseMs, Curve::Log, Disp::Ms));
    v.push_back (toggle (base + kCompAutoRelease, keep (nm (stage, "Auto Release")), "Auto Rel", d.autoRelease));
    v.push_back (real (base + kCompKnee, keep (nm (stage, "Knee")), "Knee", 0.0, 48.0, d.kneeDb, Curve::Linear, Disp::Db));
    v.push_back (real (base + kCompRange, keep (nm (stage, "Range")), "Range", 0.0, 60.0, d.rangeDb, Curve::Linear, Disp::Db));
    v.push_back (real (base + kCompHold, keep (nm (stage, "Hold")), "Hold", 0.0, 500.0, d.holdMs, Curve::Linear, Disp::Ms));
    v.push_back (toggle (base + kCompAutoGain, keep (nm (stage, "Auto Gain")), "Auto Gain", d.autoGain));
    v.push_back (real (base + kCompDry, keep (nm (stage, "Dry")), "Dry", -60.0, 6.0, d.dryDb, Curve::Linear, Disp::DbGain));
    v.push_back (real (base + kCompXoverLow, keep (nm (stage, "Low Split")), "Low Split", 40.0, 1000.0, d.xoverLowHz, Curve::Log, Disp::Hz));
    v.push_back (real (base + kCompXoverHigh, keep (nm (stage, "High Split")), "High Split", 500.0, 12000.0, d.xoverHighHz, Curve::Log, Disp::Hz));
    v.push_back (real (base + kCompOutput, keep (nm (stage, "Output")), "Output", -24.0, 24.0, d.outputDb, Curve::Linear, Disp::Db));
}

std::vector<ParamInfo> buildTable ()
{
    using namespace defaults;
    std::vector<ParamInfo> v;
    v.push_back (real (kOutput, "Output", "Output", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
    v.push_back (percent (kDryWet, "Dry/Wet", "Dry/Wet", 1.0));
    const std::vector<const char*> stages (kStageNames, kStageNames + kNumStages);
    for (int i = 0; i < kNumStages; ++i)
        v.push_back (choice (kOrderBase + (uint32_t)i, keep ("Stage " + std::to_string (i + 1)), keep ("Stage " + std::to_string (i + 1)),
                             stages, i));

    v.push_back (toggle (kVocOn, "Vocoder", "Vocoder", kVocoder.on));
    v.push_back (integer (kVocBands, "Vocoder Bands", "Bands", 8, 100, kVocoder.bands, Disp::Plain));
    v.push_back (real (kVocLow, "Vocoder Low", "Low", 20.0, 1000.0, kVocoder.lowHz, Curve::Log, Disp::Hz));
    v.push_back (real (kVocHigh, "Vocoder High", "High", 1000.0, 20000.0, kVocoder.highHz, Curve::Log, Disp::Hz));
    v.push_back (choice (kVocOrder, "Vocoder Filter Order", "Order", {"Gentle", "Medium", "Steep"}, kVocoder.order));
    v.push_back (real (kVocAttack, "Vocoder Attack", "Attack", 0.1, 100.0, kVocoder.attackMs, Curve::Log, Disp::Ms));
    v.push_back (real (kVocRelease, "Vocoder Release", "Release", 5.0, 1000.0, kVocoder.releaseMs, Curve::Log, Disp::Ms));
    v.push_back (percent (kVocRatio, "Vocoder Ratio", "Ratio", kVocoder.ratio));

    v.push_back (toggle (kSpkOn, "Spike", "Spike", kSpike.on));
    v.push_back (choice (kSpkMode, "Spike Mode", "Mode", {"Cut", "Boost"}, kSpike.mode));
    v.push_back (real (kSpkDepth, "Spike Depth", "Depth", 0.0, 10.0, kSpike.depth, Curve::Linear, Disp::Number));
    v.push_back (real (kSpkSensitivity, "Spike Sensitivity", "Sensitivity", 0.0, 10.0, kSpike.sensitivity, Curve::Linear, Disp::Number));
    v.push_back (real (kSpkDecay, "Spike Decay", "Decay", 0.0, 10.0, kSpike.decay, Curve::Linear, Disp::Number));
    v.push_back (real (kSpkSharpness, "Spike Sharpness", "Sharpness", 0.0, 10.0, kSpike.sharpness, Curve::Linear, Disp::Number));
    v.push_back (real (kSpkDecayTilt, "Spike Decay LF/HF", "Decay LF/HF", -10.0, 10.0, kSpike.decayTilt, Curve::Linear, Disp::Number));
    v.push_back (percent (kSpkLink, "Spike Stereo Link", "Link", kSpike.link));
    v.push_back (real (kSpkLow, "Spike Low", "Low", 20.0, 2000.0, kSpike.lowHz, Curve::Log, Disp::Hz));
    v.push_back (real (kSpkHigh, "Spike High", "High", 1000.0, 20000.0, kSpike.highHz, Curve::Log, Disp::Hz));
    v.push_back (percent (kSpkMix, "Spike Mix", "Mix", kSpike.mix));
    v.push_back (real (kSpkTrim, "Spike Trim", "Trim", -12.0, 12.0, kSpike.trimDb, Curve::Linear, Disp::Db));

    v.push_back (toggle (kMotOn, "Motion", "Motion", kMotion.on));
    v.push_back (integer (kMotOrbs, "Motion Orbs", "Orbs", 1, 16, kMotion.orbs, Disp::Plain));
    v.push_back (choice (kMotPattern, "Motion Pattern", "Pattern", {"Orbit", "Swarm"}, kMotion.pattern));
    v.push_back (real (kMotSpeed, "Motion Speed", "Speed", 0.0, 80.0, kMotion.speed, Curve::Power3, Disp::Number));
    v.push_back (real (kMotDistance, "Motion Distance", "Distance", 0.5, 20.0, kMotion.distance, Curve::Log, Disp::Number));
    v.push_back (real (kMotRadius, "Motion Radius", "Radius", 0.1, 3.0, kMotion.radius, Curve::Log, Disp::Number));
    v.push_back (percent (kMotSpread, "Motion Spread", "Spread", kMotion.spread));
    v.push_back (percent (kMotRandom, "Motion Randomness", "Random", kMotion.randomness));
    v.push_back (toggle (kMotFloor, "Motion Floor", "Floor", kMotion.floor));
    v.push_back (percent (kMotMix, "Motion Mix", "Mix", kMotion.mix));

    addTransient (v, kTr1Base, "Transient 1", kTransient1);
    addLimiter (v, kLim1Base, "Limiter 1", kLimiter1);
    addTransient (v, kTr2Base, "Transient 2", kTransient2);
    addComp (v, kComp1Base, "Comp 1", kComp1);
    addComp (v, kComp2Base, "Comp 2", kComp2);

    v.push_back (toggle (kTapeOn, "Tape", "Tape", kTape.on));
    v.push_back (real (kTapeSplit, "Tape Split", "Split", 80.0, 1000.0, kTape.splitHz, Curve::Log, Disp::Hz));
    v.push_back (real (kTapeLowDrive, "Tape Low Drive", "Drive", 0.0, 36.0, kTape.lowDriveDb, Curve::Linear, Disp::Db));
    v.push_back (percent (kTapeLowMix, "Tape Low Mix", "Mix", kTape.lowMix));
    v.push_back (real (kTapeLowDyn, "Tape Low Dynamics", "Dynamics", -1.0, 1.0, kTape.lowDynamics, Curve::Linear, Disp::Percent));
    v.push_back (real (kTapeLowLevel, "Tape Low Level", "Level", -24.0, 24.0, kTape.lowLevelDb, Curve::Linear, Disp::Db));
    v.push_back (real (kTapeHighDrive, "Tape High Drive", "Drive", 0.0, 36.0, kTape.highDriveDb, Curve::Linear, Disp::Db));
    v.push_back (percent (kTapeHighMix, "Tape High Mix", "Mix", kTape.highMix));
    v.push_back (real (kTapeHighDyn, "Tape High Dynamics", "Dynamics", -1.0, 1.0, kTape.highDynamics, Curve::Linear, Disp::Percent));
    v.push_back (real (kTapeHighLevel, "Tape High Level", "Level", -24.0, 24.0, kTape.highLevelDb, Curve::Linear, Disp::Db));

    addLimiter (v, kLim2Base, "Limiter 2", kLimiter2);

    // the Smacheratr at the end of the chain (off by default, as in every plug-in of the suite)
    pk::addTailParams (v, kTailBase);
    smacheratr::addTailExtParams (v, kTailExtBase);
    smacheratr::addTailExt2Params (v, kTailExt2Base);
    return v;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

} // namespace detonatr

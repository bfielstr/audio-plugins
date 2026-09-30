#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <string>
#include <vector>

namespace detonatr {

using namespace pk;
using namespace pk::make;

const char* stageName (int stage)
{
    static const char* names[kNumStages] = {"Clean", "Tone", "Multiband", "Transient", "Saturator"};
    return stage >= 0 && stage < kNumStages ? names[stage] : "";
}

int64_t mbIdAt (uint32_t block)
{
    if (block < multidyn::kSatOn)
        return block;
    if (block == multidyn::kSatOn)
        return multidyn::kRmsWindow;
    if (block == multidyn::kSatOn + 1)
        return multidyn::kSoften;
    return -1;
}

int64_t mbBlockOf (uint32_t id)
{
    if (id < multidyn::kSatOn)
        return id;
    if (id == multidyn::kRmsWindow)
        return multidyn::kSatOn;
    if (id == multidyn::kSoften)
        return multidyn::kSatOn + 1;
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

std::vector<ParamInfo> buildTable ()
{
    std::vector<ParamInfo> v;
    v.push_back (real (kOutput, "Output", "Output", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
    v.push_back (percent (kDryWet, "Dry/Wet", "Dry/Wet", 1.0));
    const std::vector<const char*> stages = {"Clean", "Tone", "Multiband", "Transient", "Saturator"};
    for (int i = 0; i < kNumStages; ++i)
        v.push_back (choice (kOrderBase + (uint32_t)i, keep ("Stage " + std::to_string (i + 1)), keep ("Stage " + std::to_string (i + 1)),
                             stages, i));

    v.push_back (toggle (kCleanOn, "Clean", "Clean", true));
    v.push_back (percent (kDenoise, "Denoise", "Denoise", 0.3));
    v.push_back (percent (kDereverb, "Dereverb", "Dereverb", 0.3));

    v.push_back (toggle (kToneOn, "Tone", "Tone", true));
    v.push_back (real (kRoot, "Root", "Root", 30.0, 400.0, 82.4, Curve::Log, Disp::Hz)); // E1, where most designed impacts sit
    v.push_back (choice (kMaterial, "Material", "Material", {"Glass", "Metal Pot", "Pipe", "Wood", "Bell", "Bottle"}, 1));
    v.push_back (real (kDecay, "Decay", "Decay", 50.0, 4000.0, 800.0, Curve::Log, Disp::Ms));
    v.push_back (percent (kResonators, "Resonators", "Resonators", 0.5));
    v.push_back (percent (kCarriers, "Recordings", "Recordings", 0.5));
    for (int i = 0; i < 4; ++i)
        v.push_back (percent (kCarrierLevel1 + (uint32_t)i, keep ("Recording " + std::to_string (i + 1) + " Level"),
                              keep ("Rec " + std::to_string (i + 1)), 1.0));
    v.push_back (percent (kToneDry, "Tone Dry", "Dry", 0.0)); // tonal: the input through as it is brings its noise back
    v.push_back (percent (kDisperse, "Disperse", "Disperse", 0.3));
    v.push_back (real (kDisperseFreq, "Disperse Frequency", "Disp Freq", 50.0, 5000.0, 400.0, Curve::Log, Disp::Hz));

    v.push_back (toggle (kMultibandOn, "Multiband", "Multiband", true));

    v.push_back (toggle (kTransientOn, "Transient", "Transient", true));
    v.push_back (real (kSpike, "Spike", "Spike", 0.1, 20.0, 3.0, Curve::Log, Disp::Ms));
    v.push_back (real (kDrop, "Drop", "Drop", 0.0, 48.0, 18.0, Curve::Linear, Disp::Db));
    v.push_back (real (kFall, "Fall", "Fall", 0.1, 50.0, 5.0, Curve::Log, Disp::Ms));
    v.push_back (real (kSensitivity, "Transient Sensitivity", "Sens", 3.0, 24.0, 9.0, Curve::Linear, Disp::Db)); // lower: rumble swells count as hits

    // the Saturator stage: on, raising what the Transient stage dropped back up (soft clipped)
    pk::addTailParams (v, kTailBase, true);
    v[kTailBase + pk::kTailDrive].def = 18.0;
    v[kTailBase + pk::kTailPostClip].def = 1.0;

    // the Multiband stage: Multidyn's own parameters, names and defaults
    const auto& md = multidyn::paramTable ();
    for (uint32_t j = 0; j < kMbBlock; ++j)
    {
        ParamInfo pi = md.info ((uint32_t)mbIdAt (j));
        pi.id = kMbBase + j;
        pi.name = keep (std::string ("Multiband ") + pi.name);
        v.push_back (pi);
    }

    smacheratr::addTailExtParams (v, kTailExtBase);
    smacheratr::addTailExt2Params (v, kTailExt2Base);
    static_assert (kNumParams == kTailExt2Base + pk::kTailExt2Fields, "Gently's Advanced block is the last");
    return v;
}

} // namespace

const ParamTable& paramTable ()
{
    static const ParamTable t (buildTable ());
    return t;
}

} // namespace detonatr

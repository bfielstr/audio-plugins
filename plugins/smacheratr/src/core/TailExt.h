// The rest of Smacheratr's controls for the saturator at the end of a plug-in's chain (smacheratr::Tail).
// The first six (pk::TailField) came first and sit in a block in the middle of each plug-in's IDs;
// these came later, so each plug-in appends them as a second block (pk::kTailExtFields entries).
// Field i is Smacheratr parameter kTailExtIds[i], with Smacheratr's name, range and default (the
// colour filters start off, and Mid/Side where the plug-in asks for it). Gentlr's Advanced mode and its Sub band came
// after that block was closed in (plug-ins have parameters right after it): a third block
// (pk::kTailExt2Fields entries, kTailExt2Ids) at the end of each plug-in's IDs. Some plug-ins have
// parameters after that one too, so Gentlr's High band and No Overlap are a fourth block
// (pk::kTailExt3Fields entries, kTailExt3Ids), again at the very end of each plug-in's IDs. Gentlr has
// its own Slope after that one, so Gentlr's glue is a fifth block (pk::kTailExt4Fields entries,
// kTailExt4Ids), at the very end of each plug-in's IDs.
#pragma once

#include "Params.h"

#include "pluginkit/TailParams.h"

#include <string>
#include <vector>

namespace smacheratr {

inline constexpr uint32_t kTailExtIds[pk::kTailExtFields] = {kOutput,    kColorOn,   kColorLo, kColorHi,
                                                            kColorFreq, kColorWidth, kOversampling, kDcFilter,
                                                            kMidSide,   kClarity,    kClarityFreq, kClarityWidth,
                                                            kClarityRange, kClarity2,  kClarity2Freq, kClarity2Width,
                                                            kClarity2Range};
static_assert (pk::kTailExtFields == 17, "one Smacheratr parameter per extended tail field");
inline constexpr uint32_t kTailExt2Ids[pk::kTailExt2Fields] = {kClarityAdvanced, kClarityThreshold, kClarity2Threshold,
                                                              kClarityDrive, kClarityDriveAmount,
                                                              kClaritySub, kClaritySubFreq, kClaritySubRange, kClaritySubThreshold};
static_assert (pk::kTailExt2Fields == 9, "one Smacheratr parameter per field of the tail's third block");
inline constexpr uint32_t kTailExt3Ids[pk::kTailExt3Fields] = {kClarityHigh, kClarityHighFreq, kClarityHighRange, kClarityHighThreshold,
                                                              kClarityNoOverlap, kClaritySlope};
static_assert (pk::kTailExt3Fields == 6, "one Smacheratr parameter per field of the tail's fourth block");
inline constexpr uint32_t kTailExt4Ids[pk::kTailExt4Fields] = {kClarityGlue12, kClarityGlueSub1, kClarityGlueSub2, kClarityGlue1High,
                                                              kClarityGlue2High};
static_assert (pk::kTailExt4Fields == kGluePairs && pk::kTailExt4Glue12 == 0 && pk::kTailExt4Glue2High == kGluePairs - 1,
               "one Smacheratr parameter per field of the tail's fifth block, in GluePair's order");

inline void addTailExtParams (std::vector<pk::ParamInfo>& t, uint32_t base, bool midSide = false)
{
    for (uint32_t i = 0; i < pk::kTailExtFields; ++i)
    {
        pk::ParamInfo pi = paramTable ().info (kTailExtIds[i]);
        pi.id = base + i;
        pi.name = pk::make::keep (std::string ("Saturator ") + pi.name);
        if (kTailExtIds[i] == kColorOn)
            pi.def = 0.0;
        if (kTailExtIds[i] == kMidSide)
            pi.def = midSide ? 1.0 : 0.0;
        t.push_back (pi);
    }
}

// The third block (Gentlr's Advanced mode), at `base`.
inline void addTailExt2Params (std::vector<pk::ParamInfo>& t, uint32_t base)
{
    for (uint32_t i = 0; i < pk::kTailExt2Fields; ++i)
    {
        pk::ParamInfo pi = paramTable ().info (kTailExt2Ids[i]);
        pi.id = base + i;
        pi.name = pk::make::keep (std::string ("Saturator ") + pi.name);
        t.push_back (pi);
    }
}

// The fourth block (Gentlr's High band and No Overlap), at `base`.
inline void addTailExt3Params (std::vector<pk::ParamInfo>& t, uint32_t base)
{
    for (uint32_t i = 0; i < pk::kTailExt3Fields; ++i)
    {
        pk::ParamInfo pi = paramTable ().info (kTailExt3Ids[i]);
        pi.id = base + i;
        pi.name = pk::make::keep (std::string ("Saturator ") + pi.name);
        t.push_back (pi);
    }
}

// The fifth block (Gentlr's glue), at `base`.
inline void addTailExt4Params (std::vector<pk::ParamInfo>& t, uint32_t base)
{
    for (uint32_t i = 0; i < pk::kTailExt4Fields; ++i)
    {
        pk::ParamInfo pi = paramTable ().info (kTailExt4Ids[i]);
        pi.id = base + i;
        pi.name = pk::make::keep (std::string ("Saturator ") + pi.name);
        t.push_back (pi);
    }
}

// where each block's fields start among the tail's fields
constexpr uint32_t kTailExt2First = pk::kTailFields + pk::kTailExtFields;
constexpr uint32_t kTailExt3First = kTailExt2First + pk::kTailExt2Fields;
constexpr uint32_t kTailExt4First = kTailExt3First + pk::kTailExt3Fields;

// The tail field (a pk::TailField, pk::kTailFields + i for extended field i, kTailExt2First + i for
// field i of the third block, kTailExt3First + i for field i of the fourth, kTailExt4First + i for
// field i of the fifth) that holds Smacheratr
// parameter id, or -1 (Smacheratr's own Dry/Wet is the tail's Mix).
inline int tailFieldOf (uint32_t id)
{
    switch (id)
    {
        case kPreLimit: return pk::kTailPreLimit;
        case kDrive: return pk::kTailDrive;
        case kPostClip: return pk::kTailPostClip;
        case kDryWet: return pk::kTailMix;
        case kPreLimitThreshold: return pk::kTailThreshold;
        default: break;
    }
    for (uint32_t i = 0; i < pk::kTailExtFields; ++i)
        if (kTailExtIds[i] == id)
            return (int)(pk::kTailFields + i);
    for (uint32_t i = 0; i < pk::kTailExt2Fields; ++i)
        if (kTailExt2Ids[i] == id)
            return (int)(kTailExt2First + i);
    for (uint32_t i = 0; i < pk::kTailExt3Fields; ++i)
        if (kTailExt3Ids[i] == id)
            return (int)(kTailExt3First + i);
    for (uint32_t i = 0; i < pk::kTailExt4Fields; ++i)
        if (kTailExt4Ids[i] == id)
            return (int)(kTailExt4First + i);
    return -1;
}

// Where a plug-in's five blocks of tail parameters start (pk::addTailParams, addTailExtParams,
// addTailExt2Params, addTailExt3Params, addTailExt4Params).
struct TailBases
{
    uint32_t base, extBase, ext2Base, ext3Base, ext4Base;
};

// A plug-in's parameter for tail field f (as tailFieldOf gives it): its blocks at b.
constexpr uint32_t tailParamOf (uint32_t f, const TailBases& b)
{
    return f < pk::kTailFields  ? b.base + f
           : f < kTailExt2First ? b.extBase + (f - pk::kTailFields)
           : f < kTailExt3First ? b.ext2Base + (f - kTailExt2First)
           : f < kTailExt4First ? b.ext3Base + (f - kTailExt3First)
                                : b.ext4Base + (f - kTailExt4First);
}

// The tail field of a plug-in's parameter id (its blocks at b), or -1 when it is none of them.
constexpr int tailFieldIn (uint32_t id, const TailBases& b)
{
    return id >= b.base && id < b.base + pk::kTailFields               ? (int)(id - b.base)
           : id >= b.extBase && id < b.extBase + pk::kTailExtFields    ? (int)(pk::kTailFields + (id - b.extBase))
           : id >= b.ext2Base && id < b.ext2Base + pk::kTailExt2Fields ? (int)(kTailExt2First + (id - b.ext2Base))
           : id >= b.ext3Base && id < b.ext3Base + pk::kTailExt3Fields ? (int)(kTailExt3First + (id - b.ext3Base))
           : id >= b.ext4Base && id < b.ext4Base + pk::kTailExt4Fields ? (int)(kTailExt4First + (id - b.ext4Base))
                                                                       : -1;
}

// subHighStateToRange for a plug-in's end saturator: a state (norm and has, by the plug-in's IDs) saved
// before the Sub and High bands lost their buttons, its third block at ext2Base and fourth at ext3Base.
// The two Ranges are marked as present.
template <class Norm, class Has>
inline void tailSubHighToRange (Norm& norm, Has& has, uint32_t ext2Base, uint32_t ext3Base)
{
    const uint32_t subRange = ext2Base + pk::kTailExt2SubRange, highRange = ext3Base + pk::kTailExt3HighRange;
    subHighStateToRange (norm, has, ext2Base + pk::kTailExt2Sub, subRange, ext3Base + pk::kTailExt3High, highRange);
    has[subRange] = has[highRange] = true;
}

// Gentlr's band Slope for a state saved before it: Classic (12 dB/oct below, 6 above), the only shape
// there was, so the state sounds as it did (a new instance gets 12 / 12), normalized.
inline double classicSlopeNorm () { return toNormalized (kClaritySlope, kSlopeClassic); }
// The same for a plug-in's end saturator (its fourth block at ext3Base), marked as present.
template <class Norm, class Has>
inline void tailSlopeToClassic (Norm& norm, Has& has, uint32_t ext3Base)
{
    norm[ext3Base + pk::kTailExt3Slope] = classicSlopeNorm ();
    has[ext3Base + pk::kTailExt3Slope] = true;
}

// The end saturator's Oversampling for a state saved while it was the Hi-Quality switch (its second block
// at extBase): on -> 4x, off -> Off (oversamplingFromHiQuality). A state without it keeps the default (4x,
// as Hi-Quality's was on).
template <class Norm, class Has>
inline void tailOversamplingFromHiQuality (Norm& norm, const Has& has, uint32_t extBase)
{
    const uint32_t id = extBase + pk::kTailExtOversampling;
    if (has[id])
        norm[id] = oversamplingFromHiQuality (norm[id]);
}

} // namespace smacheratr

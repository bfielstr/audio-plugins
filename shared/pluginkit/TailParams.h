// The optional Smacheratr at the end of every plug-in's chain: a block of parameters each plug-in
// appends to its table at some base ID. The processing is smacheratr::Tail, the editor panel
// smacheratr::TailPanel. On by default (off up to 0.24, and states saved by those keep it off:
// smacheratr::tailOldDefaults), Drive 0 dB.
#pragma once

#include "pluginkit/ParamTable.h"

#include <cstdint>
#include <vector>

namespace pk {

enum TailField : uint32_t
{
    kTailOn = 0,
    kTailPreLimit,  // look-ahead limiter before the drive
    kTailDrive,     // dB
    kTailPostClip,  // No Clip / Soft Clip / Hard Clip
    kTailMix,       // dry/wet
    kTailThreshold, // dB, the pre-limiter's threshold
    kTailFields
};

void addTailParams (std::vector<ParamInfo>& table, uint32_t base, bool onByDefault = true);

// The rest of Smacheratr's controls, a second block each plug-in appends to its IDs (the parameters
// themselves: smacheratr/src/core/TailExt.h, addTailExtParams). This block is full: some plug-ins
// have their own parameters right after it. Clarity is called Gentlr now (the names keep the old
// word, the parameters' names say Gentlr).
enum TailExtField : uint32_t
{
    kTailExtOutput = 0,
    kTailExtColorOn,
    kTailExtColorLo,
    kTailExtColorHi,
    kTailExtColorFreq,
    kTailExtColorWidth,
    kTailExtOversampling, // Off, 2x, 4x (Hi-Quality, a switch for 4x, before: its normalized 0 / 1 mean the same)
    kTailExtDcFilter,
    kTailExtMidSide,
    kTailExtClarity,
    kTailExtClarityFreq,
    kTailExtClarityWidth,
    kTailExtClarityRange,
    kTailExtClarity2,
    kTailExtClarity2Freq,
    kTailExtClarity2Width,
    kTailExtClarity2Range,
    kTailExtFields
};

// Gentlr's Advanced mode and its Sub band, a third block each plug-in appended at the very end of its IDs (the
// parameters: smacheratr/src/core/TailExt.h, addTailExt2Params). In smacheratr::Tail::setParam field
// kTailFields + kTailExtFields + i is this block's field i.
enum TailExt2Field : uint32_t
{
    kTailExt2Advanced = 0, // the bands' Thresholds and the region Drive work
    kTailExt2Threshold,    // dB, band 1
    kTailExt2Threshold2,   // dB, band 2
    kTailExt2Drive,        // drive the band region Gentlr works on
    kTailExt2DriveAmount,  // dB
    kTailExt2Sub,          // unused: Gentlr's Sub band works while its Range is above 0 dB
    kTailExt2SubFreq,      // Hz, where the Sub band starts to taper off
    kTailExt2SubRange,     // dB (0 by default: the band does nothing)
    kTailExt2SubThreshold, // dB
    kTailExt2Fields
};

// Gentlr's High band and No Overlap, a fourth block each plug-in appends at the very end of its IDs
// (the third is closed in: some plug-ins have parameters right after it; the parameters:
// smacheratr/src/core/TailExt.h, addTailExt3Params). In smacheratr::Tail::setParam field
// kTailFields + kTailExtFields + kTailExt2Fields + i is this block's field i. It grew by the Slope while
// it was the last block everywhere; Gentlr's own Slope now comes after it, so it is closed in too.
enum TailExt3Field : uint32_t
{
    kTailExt3High = 0,      // unused: Gentlr's High band works while its Range is above 0 dB
    kTailExt3HighFreq,      // Hz, where the High band starts to taper off
    kTailExt3HighRange,     // dB (0 by default: the band does nothing)
    kTailExt3HighThreshold, // dB
    kTailExt3NoOverlap,     // Gentlr's bands never cover the same frequencies
    kTailExt3Slope,         // the shape of Gentlr's two bands (smacheratr::ClaritySlope; Classic in states from before it)
    kTailExt3Fields
};

// Gentlr's glue, a fifth block each plug-in appends at the very end of its IDs (the fourth is closed in:
// Gentlr has its own Slope right after it; the parameters: smacheratr/src/core/TailExt.h,
// addTailExt4Params). One switch per pair of bands that can be glued at a shared border (smacheratr's
// GluePair order), all off by default: states from before it load them off and sound as they did. In
// smacheratr::Tail::setParam field kTailFields + kTailExtFields + kTailExt2Fields + kTailExt3Fields + i
// is this block's field i. Levlr has its drives' Oversampling right after it (closed in there: a new tail
// field needs a sixth block). Oversampling (kTailExtOversampling) was the Hi-Quality switch and became a
// choice in its place: its ID and the meaning of its two ends stayed, so no block grew.
enum TailExt4Field : uint32_t
{
    kTailExt4Glue12 = 0, // band 1 and band 2
    kTailExt4GlueSub1,   // the Sub band and band 1
    kTailExt4GlueSub2,   // the Sub band and band 2
    kTailExt4Glue1High,  // band 1 and the High band
    kTailExt4Glue2High,  // band 2 and the High band
    kTailExt4Fields
};

// The parameters Menu > Defaults sets in a new instance (GentlrDefaults.h): the end saturator's
// on switch (kTailOn), its Gentlr (kTailExtClarity) and Gentlr's Advanced mode (kTailExt2Advanced); -1
// where a plug-in has none (gentlr's own Advanced has no saturator or Gentlr switch to go with it,
// smemplr has none of them: its menu shows no Defaults). ControllerBase::setGentlrIds and
// presets::applyDefault take them.
struct GentlrIds
{
    int32_t saturator = -1, gentlr = -1, advanced = -1;
};
constexpr GentlrIds tailGentlrIds (uint32_t base, uint32_t extBase, uint32_t ext2Base)
{
    return {(int32_t)(base + kTailOn), (int32_t)(extBase + kTailExtClarity), (int32_t)(ext2Base + kTailExt2Advanced)};
}

} // namespace pk

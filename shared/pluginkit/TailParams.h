// The optional Smacheratr at the end of every plug-in's chain: a block of parameters each plug-in
// appends to its table at some base ID. The processing is smacheratr::Tail, the editor panel
// pk::EditorBase::addTailPanel. Off by default, Drive 0 dB.
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

void addTailParams (std::vector<ParamInfo>& table, uint32_t base, bool onByDefault = false);

// The rest of Smacheratr's controls, a second block each plug-in appends to its IDs (the parameters
// themselves: smacheratr/src/core/TailExt.h, addTailExtParams). This block is full: some plug-ins
// have their own parameters right after it. Clarity is called Gently now (the names keep the old
// word, the parameters' names say Gently).
enum TailExtField : uint32_t
{
    kTailExtOutput = 0,
    kTailExtColorOn,
    kTailExtColorLo,
    kTailExtColorHi,
    kTailExtColorFreq,
    kTailExtColorWidth,
    kTailExtHiQuality,
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

// Gently's Advanced mode and its Sub band, a third block each plug-in appended at the very end of its IDs (the
// parameters: smacheratr/src/core/TailExt.h, addTailExt2Params). In smacheratr::Tail::setParam field
// kTailFields + kTailExtFields + i is this block's field i.
enum TailExt2Field : uint32_t
{
    kTailExt2Advanced = 0, // the bands' Thresholds and the region Drive work
    kTailExt2Threshold,    // dB, band 1
    kTailExt2Threshold2,   // dB, band 2
    kTailExt2Drive,        // drive the band region Gently works on
    kTailExt2DriveAmount,  // dB
    kTailExt2Sub,          // unused: Gently's Sub band works while its Range is above 0 dB
    kTailExt2SubFreq,      // Hz, where the Sub band starts to taper off
    kTailExt2SubRange,     // dB (0 by default: the band does nothing)
    kTailExt2SubThreshold, // dB
    kTailExt2Fields
};

// Gently's High band and No Overlap, a fourth block each plug-in appends at the very end of its IDs
// (the third is closed in: some plug-ins have parameters right after it; the parameters:
// smacheratr/src/core/TailExt.h, addTailExt3Params). In smacheratr::Tail::setParam field
// kTailFields + kTailExtFields + kTailExt2Fields + i is this block's field i.
enum TailExt3Field : uint32_t
{
    kTailExt3High = 0,      // unused: Gently's High band works while its Range is above 0 dB
    kTailExt3HighFreq,      // Hz, where the High band starts to taper off
    kTailExt3HighRange,     // dB (0 by default: the band does nothing)
    kTailExt3HighThreshold, // dB
    kTailExt3NoOverlap,     // Gently's bands never cover the same frequencies
    kTailExt3Fields
};

} // namespace pk

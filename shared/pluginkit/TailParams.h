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
// themselves: smacheratr/src/core/TailExt.h, addTailExtParams).
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

} // namespace pk

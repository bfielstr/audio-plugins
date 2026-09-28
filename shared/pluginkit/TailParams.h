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

} // namespace pk

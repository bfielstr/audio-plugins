// The rest of Smacheratr's controls for the saturator at the end of a plug-in's chain (smacheratr::Tail).
// The first six (pk::TailField) came first and sit in a block in the middle of each plug-in's IDs;
// these came later, so each plug-in appends them as a second block (pk::kTailExtFields entries).
// Field i is Smacheratr parameter kTailExtIds[i], with Smacheratr's name, range and default (the
// colour filters start off, and Mid/Side where the plug-in asks for it).
#pragma once

#include "Params.h"

#include "pluginkit/TailParams.h"

#include <string>
#include <vector>

namespace smacheratr {

inline constexpr uint32_t kTailExtIds[pk::kTailExtFields] = {kOutput,    kColorOn,   kColorLo, kColorHi,
                                                            kColorFreq, kColorWidth, kHiQuality, kDcFilter,
                                                            kMidSide,   kClarity,    kClarityFreq, kClarityWidth,
                                                            kClarityRange, kClarity2,  kClarity2Freq, kClarity2Width,
                                                            kClarity2Range};
static_assert (pk::kTailExtFields == 17, "one Smacheratr parameter per extended tail field");

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

// The tail field (a pk::TailField, or pk::kTailFields + i for extended field i) that holds
// Smacheratr parameter id, or -1 (Smacheratr's own Dry/Wet is the tail's Mix).
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
    return -1;
}

} // namespace smacheratr

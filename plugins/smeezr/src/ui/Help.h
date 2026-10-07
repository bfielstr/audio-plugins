#pragma once

#include "Params.h"

namespace smeezr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kSqueeze:
            return "The one knob. 0 leaves the sound untouched. Up to 50 % it squeezes the spectrum towards the balance "
                   "of pink noise (every octave as loud as the next, loudness kept); past 50 % it adds an OTT-style "
                   "boost on top (quiet details up, peaks down), loudest and most squashed at 100 %.";
        case kSpeed:
            return "How quickly the pink stage follows the music. Fast rides each band like a compressor; Slow (four "
                   "times as long) acts more like a moving EQ. The OTT boost keeps its own timing.";
        case kMix: return "Blend of the effect and the untouched signal (kept in phase with the bands, so no comb filtering).";
        case kOutput: return "Overall output level.";
        default: return nullptr;
    }
}

constexpr const char* kPinkView =
    "The ten one-octave bands: each band's level (copper bars), the pink target every band is pulled towards (the "
    "cinnabar line: an equal share of the power per octave) and where the gains put each band (pale marks). The GAIN "
    "lane shows the pink stage's gain per band, and past 50 % the total with the OTT boost (cinnabar marks).";

constexpr const char* kStage =
    "Where Squeeze is: how far towards pink (0 to 50 %) and how much OTT boost on top (50 to 100 %).";

} // namespace smeezr::help

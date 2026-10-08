// The suite's effects as their own plug-ins save them, for smemplr's rack slots (their presets): per
// effect, its plug-in's state format (that plug-in's State.cpp with its migrations, compiled into smemplr
// once per effect with HostedState.cpp: plugins/smemplr/CMakeLists.txt), its processor's class ID (a
// .vstpreset names it) and its factory presets (registered with pk::presets::registerFactoryOf under the
// plug-in's name when its codec is first asked for).
//
// No smemplr header is included here: the effects' own headers (their Params.h) are only seen by
// HostedState.cpp.
#pragma once

#include "pluginterfaces/base/funknown.h"

#include <cstddef>
#include <vector>

namespace smemplr::hosted {

struct Codec
{
    Steinberg::FUID classId; // the plug-in's processor (a .vstpreset's class ID)
    size_t numParams;        // the plug-in's parameters (its State's)
    // A component state (the processor's getState, a .vstpreset's component chunk) as every parameter of
    // the plug-in, normalized by its own ID: the ones the state lacks at their defaults, the plug-in's
    // migrations of older states applied, IDs it does not know (a newer version's) ignored. False when it
    // is not the plug-in's state.
    bool (*read) (const std::vector<char>& component, std::vector<double>& norm);
    // Every parameter (a shorter vector: the missing ones are left out, so they read as defaults) as the
    // plug-in's component state, the current version.
    bool (*write) (const std::vector<double>& norm, std::vector<char>& component);
};

const Codec& codecPara ();
const Codec& codecMultidyn ();
const Codec& codecSmacheratr ();
const Codec& codecWidr ();
const Codec& codecWubr ();
const Codec& codecLevlr ();
const Codec& codecGentlr ();
const Codec& codecSmoothr ();

} // namespace smemplr::hosted

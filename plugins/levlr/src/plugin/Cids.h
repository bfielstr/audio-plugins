#pragma once

#include "pluginterfaces/base/funknown.h"

namespace levlr {

static const Steinberg::FUID kProcessorUID (0x1EAABBBC, 0xC92B45A6, 0xACC92F9D, 0x32BBEDB9);
static const Steinberg::FUID kControllerUID (0x76DB774D, 0x6C2D4986, 0x850A14E3, 0xC5A79EAA);

constexpr const char* kMetersMessageId = "LevlrMeters";
constexpr const char* kMetersAttr = "ptr";

} // namespace levlr

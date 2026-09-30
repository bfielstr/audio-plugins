#pragma once

#include "pluginterfaces/base/funknown.h"

namespace gently {

static const Steinberg::FUID kProcessorUID (0xE1AE9676, 0x356C1174, 0x7710C871, 0xB7F12963);
static const Steinberg::FUID kControllerUID (0xFC428459, 0x9D27C9B9, 0x053582B5, 0x4E31CC7C);

constexpr const char* kMetersMessageId = "GentlyMeters";
constexpr const char* kMetersAttr = "ptr";

} // namespace gently

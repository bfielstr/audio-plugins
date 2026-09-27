#pragma once

#include "pluginterfaces/base/funknown.h"

namespace lowfocus {

static const Steinberg::FUID kProcessorUID (0x55295E03, 0x20D049E8, 0x8E200FAD, 0x536BEB92);
static const Steinberg::FUID kControllerUID (0x6A05C49F, 0x3D4245FB, 0x986F57AE, 0x64FB8233);

constexpr const char* kSpectrumMessageId = "LowfocusSpectrum";
constexpr const char* kSpectrumAttr = "ptr";

} // namespace lowfocus

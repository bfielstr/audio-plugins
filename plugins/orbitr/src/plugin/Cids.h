#pragma once

#include "pluginterfaces/base/funknown.h"

namespace orbitr {

static const Steinberg::FUID kProcessorUID (0xDAF201AE, 0xC689409D, 0x896A2AE9, 0x2A39C123);
static const Steinberg::FUID kControllerUID (0x1FB4D863, 0x84254438, 0x8BB02CD1, 0x070E18FA);

constexpr const char* kSharedMessageId = "OrbitrShared";
constexpr const char* kSharedAttr = "ptr";

} // namespace orbitr

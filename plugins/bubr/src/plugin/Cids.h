#pragma once

#include "pluginterfaces/base/funknown.h"

namespace bubr {

static const Steinberg::FUID kProcessorUID (0x2934E089, 0xCBDC438B, 0xA7AA2D6C, 0xFCF66335);
static const Steinberg::FUID kControllerUID (0x030C9B45, 0x02D841D4, 0x9548C9F9, 0x4C4C89D6);

constexpr const char* kSharedMessageId = "BubrShared";
constexpr const char* kSharedAttr = "ptr";

} // namespace bubr

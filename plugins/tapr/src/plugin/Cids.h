#pragma once

#include "pluginterfaces/base/funknown.h"

namespace tapr {

static const Steinberg::FUID kProcessorUID (0x20229EBB, 0xD42F41EA, 0xA0A6C429, 0x4A031B7F);
static const Steinberg::FUID kControllerUID (0xBED5106A, 0x124C4458, 0xAA561A3E, 0x98C69E76);

constexpr const char* kSharedMessageId = "TaprShared";
constexpr const char* kSharedAttr = "ptr";

} // namespace tapr

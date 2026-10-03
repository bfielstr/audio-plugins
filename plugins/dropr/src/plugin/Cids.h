#pragma once

#include "pluginterfaces/base/funknown.h"

namespace dropr {

static const Steinberg::FUID kProcessorUID (0x52CE00FA, 0x50FD4088, 0x86B5D773, 0x23E0719E);
static const Steinberg::FUID kControllerUID (0x81EB1979, 0x8C48428C, 0xAF216193, 0x4F935194);

constexpr const char* kSharedMessageId = "DroprShared";
constexpr const char* kSharedAttr = "ptr";

} // namespace dropr

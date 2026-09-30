#pragma once

#include "pluginterfaces/base/funknown.h"

namespace deepr {

static const Steinberg::FUID kProcessorUID (0xD28213F3, 0xA12846CD, 0xBA371FA6, 0x2F36856C);
static const Steinberg::FUID kControllerUID (0x88783D31, 0xBBBF420C, 0x9FEAE072, 0x634AD951);

constexpr const char* kSharedMessageId = "DeeprShared";
constexpr const char* kSharedAttr = "ptr";

} // namespace deepr

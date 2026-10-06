#pragma once

#include "pluginterfaces/base/funknown.h"

namespace ciphr {

static const Steinberg::FUID kProcessorUID (0xE57D5DB2, 0xC8EB4382, 0x9F9FF1B1, 0x5B926A18);
static const Steinberg::FUID kControllerUID (0xC45CDB68, 0xC3F44CF3, 0xBAB650BE, 0x1F9DEC38);

constexpr const char* kSharedMessageId = "CiphrShared";
constexpr const char* kSharedAttr = "ptr";

} // namespace ciphr

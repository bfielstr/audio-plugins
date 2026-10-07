#pragma once

#include "pluginterfaces/base/funknown.h"

namespace smeezr {

static const Steinberg::FUID kProcessorUID (0xB96797A1, 0x76A84507, 0x9BB38861, 0xAD542A99);
static const Steinberg::FUID kControllerUID (0x7AAC0D32, 0xF9554E38, 0x919A4FFA, 0xDCB498EA);

constexpr const char* kSharedMessageId = "SmeezrShared";
constexpr const char* kSharedAttr = "ptr";

} // namespace smeezr

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace perrera {

static const Steinberg::FUID kProcessorUID (0x07285B76, 0x2A94C083, 0x04F2FC7F, 0x18D4D971);
static const Steinberg::FUID kControllerUID (0x06BD89D8, 0xB25E5275, 0x4D8F26A4, 0x421EE155);

constexpr const char* kMetersMessageId = "PerreraMeters";
constexpr const char* kMetersAttr = "ptr";

} // namespace perrera

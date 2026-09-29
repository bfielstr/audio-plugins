#pragma once

#include "pluginterfaces/base/funknown.h"

namespace wubr {

static const Steinberg::FUID kProcessorUID (0x5EB03F0B, 0x3EA64405, 0xA75C3C04, 0xCD3ADE4F);
static const Steinberg::FUID kControllerUID (0xFD0FA85F, 0xD4224D73, 0xB3A6C50C, 0xBE64C093);

constexpr const char* kMetersMessageId = "WubrMeters";
constexpr const char* kMetersAttr = "ptr";

} // namespace wubr

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace smoothr {

static const Steinberg::FUID kProcessorUID (0xA350E6AE, 0x73E6406C, 0xA6B3AAF4, 0xC2AA76BD);
static const Steinberg::FUID kControllerUID (0x126CFE8F, 0x277C48D5, 0x875DCEE0, 0xEDF50D0F);

constexpr const char* kMetersMessageId = "SmoothrMeters";
constexpr const char* kMetersAttr = "ptr";

} // namespace smoothr

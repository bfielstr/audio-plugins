#pragma once

#include "pluginterfaces/base/funknown.h"

namespace multidyn {

static const Steinberg::FUID kProcessorUID (0x62F75232, 0x99DD4CF6, 0x9F5E852C, 0xD652A633);
static const Steinberg::FUID kControllerUID (0xABB730DF, 0x295F48C7, 0x83AC108A, 0x94714E9D);

constexpr const char* kMeterMessageId = "MultidynMeters";
constexpr const char* kMeterAttr = "ptr";

} // namespace multidyn

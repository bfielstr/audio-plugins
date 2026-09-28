#pragma once

#include "pluginterfaces/base/funknown.h"

namespace widr {

static const Steinberg::FUID kProcessorUID (0xE8F37B9D, 0xD0044B61, 0xB00769FC, 0x319073B2);
static const Steinberg::FUID kControllerUID (0x96C1F964, 0x1DD04B10, 0x93D0B691, 0x505460DD);

constexpr const char* kMetersMessageId = "WidrMeters";
constexpr const char* kMetersAttr = "ptr";

} // namespace widr

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace smatcheratr {

static const Steinberg::FUID kProcessorUID (0x4E488297, 0xFCC37BA6, 0x5FE4F4F3, 0x50B9C2C5);
static const Steinberg::FUID kControllerUID (0xBDAF620B, 0x0CBA6E07, 0xFA7814D1, 0x1057E65C);

constexpr const char* kMetersMessageId = "SmatcheratrMeters";
constexpr const char* kMetersAttr = "ptr";

} // namespace smatcheratr

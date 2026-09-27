#pragma once

#include "pluginterfaces/base/funknown.h"

namespace stretchr {

static const Steinberg::FUID kProcessorUID (0x340E91BD, 0xCE394511, 0x83D7357C, 0x4848965D);
static const Steinberg::FUID kControllerUID (0xE61DF584, 0x44F9410D, 0xB30B0E0A, 0x6170C8A7);

constexpr const char* kSessionMessageId = "StretchrSession";
constexpr const char* kSessionAttr = "ptr";

} // namespace stretchr

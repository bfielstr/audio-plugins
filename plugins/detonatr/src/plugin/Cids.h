#pragma once

#include "pluginterfaces/base/funknown.h"

namespace detonatr {

static const Steinberg::FUID kProcessorUID (0x1AB3E21A, 0xEE444A8C, 0xA3B3B862, 0x700023F3);
static const Steinberg::FUID kControllerUID (0xC78363E1, 0x685849E4, 0xB6EDF6CB, 0x065FD477);

constexpr const char* kBridgeMessageId = "DetonatrBridge";
constexpr const char* kBridgeAttr = "ptr";

} // namespace detonatr

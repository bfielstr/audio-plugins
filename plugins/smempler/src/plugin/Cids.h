#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace smempler {

static const Steinberg::FUID kProcessorUID (0x9ABDE5DB, 0xA2B34834, 0xBA76C205, 0x9F7E6D97);
static const Steinberg::FUID kControllerUID (0x2D1E8600, 0xE1AD417E, 0xA171965A, 0x2106FB25);

// Message used to hand the shared Bridge from the processor to the controller.
constexpr const char* kBridgeMessageId = "SmemplerBridge";
constexpr const char* kBridgeAttr = "ptr";

} // namespace smempler

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace pinchr {

static const Steinberg::FUID kProcessorUID (0x324EDAF8, 0x3E234179, 0x8E2DAECA, 0x0763820B);
static const Steinberg::FUID kControllerUID (0xA633C1A4, 0xB61A477A, 0x9B24E57F, 0x1045AF29);

constexpr const char* kSharedMessageId = "PinchrShared";
constexpr const char* kSharedAttr = "ptr";

} // namespace pinchr

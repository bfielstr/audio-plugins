#pragma once

#include "pluginterfaces/base/funknown.h"

namespace sweepr {

static const Steinberg::FUID kProcessorUID (0xC29CE2DC, 0x9E5844FE, 0x8AABB8D5, 0x3D2C6B71);
static const Steinberg::FUID kControllerUID (0xADE8CE0E, 0x92814263, 0xBDFE6633, 0xEF854BD1);

constexpr const char* kSharedMessageId = "SweeprShared";
constexpr const char* kSharedAttr = "ptr";

} // namespace sweepr

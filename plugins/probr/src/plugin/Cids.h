#pragma once

#include "pluginterfaces/base/funknown.h"

namespace probr {

static const Steinberg::FUID kProcessorUID (0x1C8B85E9, 0x6F6C4FF1, 0x9BBFA00F, 0x72E662C1);
static const Steinberg::FUID kControllerUID (0x300F98FB, 0xA0514F2A, 0x9DC911F6, 0xD25E89B1);

constexpr const char* kSharedMessageId = "ProbrShared";
constexpr const char* kSharedAttr = "ptr";
// controller -> processor: the Label and the Folder ("label", "folder": binary, UTF-8)
constexpr const char* kTextMessageId = "ProbrText";
constexpr const char* kLabelAttr = "label";
constexpr const char* kFolderAttr = "folder";

} // namespace probr

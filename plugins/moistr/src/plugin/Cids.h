#pragma once

#include "pluginterfaces/base/funknown.h"

namespace moistr {

static const Steinberg::FUID kProcessorUID (0x146500E9, 0x65024ACD, 0xB5496781, 0x09A45209);
static const Steinberg::FUID kControllerUID (0x311BDD63, 0x9D334979, 0xBD2AD6DB, 0x70E85CE8);

constexpr const char* kSharedMessageId = "MoistrShared";
constexpr const char* kSharedAttr = "ptr";

} // namespace moistr

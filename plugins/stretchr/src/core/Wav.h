// Minimal 32-bit float WAV writer for exporting renders.
#pragma once

#include <string>

namespace stretchr {

struct Rendered;

// Writes `r` as a stereo 32-bit float WAV. `path` is UTF-8.
bool writeWav (const std::string& path, const Rendered& r, std::string& error);

} // namespace stretchr

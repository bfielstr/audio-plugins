// A small 32-bit float WAV writer (the capture band's audio and wavetables: pluginkit/ui/CaptureView.h).
// Little-endian, as every supported platform is. A wavetable carries a `clm ` chunk (Serum's convention,
// which Serum, Vital and Ableton's Wavetable read for the frame size): clmText.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pk::wav {

// The file's bytes: `channels` interleaved from the planar `data` (each `frames` long), at `sampleRate`,
// with a `clm ` chunk holding `clm` when it is not empty (before the data).
std::vector<char> encode (const std::vector<const float*>& data, size_t frames, double sampleRate, const std::string& clm = {});
// The bytes to a file (`path` UTF-8, its folder made); false (and `error`) when it cannot be written.
bool write (const std::string& path, const std::vector<char>& bytes, std::string& error);
// The `clm ` chunk's text for frames of `frameSize` samples: "<!>2048 01000000 wavetable (www.xferrecords.com)".
std::string clmText (int frameSize);

// Reading one back (the tests): the format, the channels interleaved, and the `clm ` chunk's text.
struct Info
{
    int format = 0, channels = 0, bits = 0;
    uint32_t sampleRate = 0;
    std::vector<float> samples;
    std::string clm;
};
bool parse (const std::vector<char>& bytes, Info& out);

} // namespace pk::wav

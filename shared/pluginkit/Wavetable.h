// A wavetable from a stretch of audio (the capture band's "Drag as Wavetable" and "Save Wavetable...",
// pluginkit/ui/CaptureView.h): the sound's fundamental found (or given), the audio cut into single cycles
// at its rising zero crossings, each cycle resampled to `frameSize` samples, and up to `maxFrames` of them,
// evenly spaced through the audio, one after another: Serum's layout (a mono WAV of frames of 2048 with a
// `clm ` chunk, WavFile.h), which Serum, Vital and Ableton's Wavetable read.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pk::wavetable {

constexpr int kFrameSize = 2048;
constexpr int kMaxFrames = 256;

// The period of the sound's fundamental in samples (a normalized difference function, as YIN does, over
// 30 Hz .. 2 kHz, on the loudest stretch of up to 8192 samples), refined between samples; 0 when no steady
// pitch is found.
double detectPeriod (const float* x, size_t n, double sampleRate);

struct Table
{
    std::vector<float> frames; // count * frameSize samples, frame after frame
    int count = 0;
    double period = 0;         // the cycle length used (samples)
    bool detected = false;     // found in the audio (else the hint's)
    std::string error;         // why there is none ("": there is one)
};
// mono: the audio (both channels' mean); hintHz: the pitch to use when none is detected (smemplr's last
// note; 0: none). The frames are scaled together so the loudest sample is at full scale.
Table make (const float* mono, size_t n, double sampleRate, double hintHz, int frameSize = kFrameSize, int maxFrames = kMaxFrames);

} // namespace pk::wavetable

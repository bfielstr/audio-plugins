// The edited clip: source audio, where it sits on the host timeline, stretch markers and the
// pitch envelope. Plain data, shared read-only once published.
#pragma once

#include "smempler/src/core/SampleData.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace stretchr {

using smempler::SampleData;
using SamplePtr = std::shared_ptr<const SampleData>;

// Pins a point of the source (seconds) to a point of the output (seconds, before Speed).
struct StretchMarker
{
    double src = 0.0;
    double dst = 0.0;
    bool operator== (const StretchMarker& o) const { return src == o.src && dst == o.dst; }
};

// Pitch offset in semitones at a point of the source (seconds), so edits stay on the notes
// they were drawn on when the timing changes.
struct PitchPoint
{
    double src = 0.0;
    double semis = 0.0;
    bool operator== (const PitchPoint& o) const { return src == o.src && semis == o.semis; }
};

// Local stretch limits between two markers (output seconds per source second).
constexpr double kMinSegmentStretch = 0.05;
constexpr double kMaxSegmentStretch = 20.0;
constexpr double kMaxPitchEnvelope = 24.0;

class TimeMap
{
public:
    // `markers` must be sanitized; scale = output seconds per dst second (1 / speed).
    TimeMap (const std::vector<StretchMarker>& markers, double scale);

    double outLength () const { return m.empty () ? 0.0 : m.back ().dst * k; }
    double srcAt (double out) const;
    double outAt (double src) const;
    // Output seconds per source second of the segment containing `out`.
    double stretchAt (double out) const;
    double scale () const { return k; }

private:
    std::vector<StretchMarker> m;
    double k = 1.0;
};

// Markers always start at (0, 0) and end at the source length. Sorts, removes duplicates and
// keeps every segment within the stretch limits.
std::vector<StretchMarker> identityMarkers (double srcLength);
void sanitizeMarkers (std::vector<StretchMarker>& markers, double srcLength);

double pitchAt (const std::vector<PitchPoint>& points, double src);
void sanitizePitch (std::vector<PitchPoint>& points, double srcLength);

struct Clip
{
    SamplePtr audio;
    std::string name;
    double start = 0.0; // host project time (seconds) where the clip begins
    std::vector<StretchMarker> markers;
    std::vector<PitchPoint> pitch;

    bool empty () const { return !audio || audio->length <= 0; }
    double srcLength () const { return empty () ? 0.0 : audio->seconds (); }
};

// Compact binary form stored in the host project (audio as 24-bit PCM).
void writeClip (const Clip& clip, std::vector<uint8_t>& out);
bool readClip (const uint8_t* data, size_t size, Clip& clip);

} // namespace stretchr

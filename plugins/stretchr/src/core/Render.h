// Offline rendering of a clip with one of the stretch algorithms. The whole clip is rendered
// ahead of playback (like REAPER applying an item's stretch), so the result is identical in
// realtime playback, freezing and bouncing.
#pragma once

#include "Clip.h"
#include "Params.h"

#include <functional>
#include <memory>
#include <vector>

namespace stretchr {

struct RenderSettings
{
    int algorithm = kPolyphonic;
    double semis = 0.0;        // pitch + fine
    double formantSemis = 0.0;
    bool preserveFormants = false;
    double speed = 1.0;        // resolved (Follow Tempo already applied)
    double windowMs = 60.0;
    int transients = kMixed;
    double smearMs = 500.0;
    double gainDb = 0.0;
    int stereo = kStereoWide; // Extreme and Alien

    bool operator== (const RenderSettings& o) const
    {
        return algorithm == o.algorithm && semis == o.semis && formantSemis == o.formantSemis &&
               preserveFormants == o.preserveFormants && speed == o.speed && windowMs == o.windowMs &&
               transients == o.transients && smearMs == o.smearMs && gainDb == o.gainDb && stereo == o.stereo;
    }
    bool operator!= (const RenderSettings& o) const { return !(*this == o); }
};

// Reads the settings from plain parameter values; hostBpm resolves Follow Tempo.
RenderSettings settingsFromParams (const double* plain, double hostBpm);

struct Rendered
{
    std::vector<float> l, r;
    double sampleRate = 48000.0;
    uint64_t request = 0;     // key of the request that produced it
    double srcSeconds = 0.0;  // length of the source clip
    long long length () const { return (long long)l.size (); }
};
using RenderedPtr = std::shared_ptr<const Rendered>;

// Analysis that only depends on the audio (pitch marks for Soloist), reused across renders.
struct AnalysisCache
{
    const SampleData* audio = nullptr;
    struct Mark
    {
        int pos;
        float period; // frames
        bool voiced;
    };
    std::vector<Mark> marks;
};

// Called about every 50 ms of work with progress 0..1; return false to cancel.
using Progress = std::function<bool (float)>;

// Renders `clip` at `outRate`. Returns false when cancelled (or the clip is empty).
bool renderClip (const Clip& clip, const RenderSettings& s, double outRate, Rendered& out, AnalysisCache& cache,
                 const Progress& progress = {});

// Pitch marks for the Soloist algorithm (exposed for tests).
void analysePitchMarks (const SampleData& s, AnalysisCache& cache);

} // namespace stretchr

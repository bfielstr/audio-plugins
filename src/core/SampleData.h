// An immutable, analysed sample. Created on a non-realtime thread and then shared
// (read-only) with the audio thread and the editor.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace simplr {

// Non-destructive edits applied on top of the source file (the file is never modified).
struct SampleOps
{
    double cropStart = 0.0; // normalized range of the original file that is kept
    double cropEnd = 1.0;
    bool reverse = false;
    bool normalize = false;

    bool operator== (const SampleOps& o) const
    {
        return cropStart == o.cropStart && cropEnd == o.cropEnd && reverse == o.reverse && normalize == o.normalize;
    }
};

struct Onset
{
    int pos;        // sample frame
    float strength; // 0..1, relative to the strongest onset in the file
};

struct PeakLevel
{
    int blockSize = 0;
    std::vector<float> mn[2], mx[2];
};

class SampleData
{
public:
    std::string path;
    std::string name;
    SampleOps ops;
    double sampleRate = 44100.0;
    int numChannels = 0;
    int length = 0;
    std::vector<float> ch[2];
    std::vector<Onset> onsets;
    PeakLevel peaks; // coarse min/max for fast waveform drawing
    float peakAbs = 0.0f;

    const float* data (int c) const { return ch[c < numChannels ? c : 0].data (); }
    double seconds () const { return length / sampleRate; }

    // Nearest zero crossing (left channel) within +/- maxDistance frames, or pos if none.
    int snapToZero (int pos, int maxDistance = 4096) const;

    // Builds the analysis data (onsets, peaks). Called by the loaders.
    void analyse ();

    // Loads wav/aiff/flac/mp3, applies ops and analyses. Returns nullptr and sets error on failure.
    static std::shared_ptr<SampleData> load (const std::string& path, const SampleOps& ops, std::string& error);
    // Builds a sample from memory (used by tests).
    static std::shared_ptr<SampleData> fromBuffers (std::vector<float> left, std::vector<float> right,
                                                    double sampleRate, const std::string& name = "memory");
};

using SamplePtr = std::shared_ptr<const SampleData>;

// Loads raw interleaved audio from disk. Exposed for tests.
bool decodeAudioFile (const std::string& path, std::vector<float>& left, std::vector<float>& right,
                      int& numChannels, double& sampleRate, std::string& error);

bool isSupportedAudioFile (const std::string& path);

} // namespace simplr

// An immutable, analysed sample. Created on a non-realtime thread and then shared
// (read-only) with the audio thread and the editor.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace smemplr {

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

// A band-limited, decimated copy of the sample for reading it fast (far transposed up): level k
// is low-passed below its own Nyquist and keeps every 2^k-th frame, so its frame j is at frame
// j * 2^k of the sample (the filters are centred: no delay). See Interp.h: SampleReader.
struct MipLevel
{
    int length = 0;
    std::vector<float> ch[2];
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
    // Levels 1 .. kMipLevels (1/2 .. 1/64 of the rate): mips[k - 1] is level k. A read 2^k times
    // faster than real time takes level k, so a sample transposed far up (+48 semitones, and
    // bent or modulated further) never aliases. About as much memory again as the sample.
    static constexpr int kMipLevels = 6;
    std::vector<MipLevel> mips;

    const float* data (int c) const { return ch[c < numChannels ? c : 0].data (); }
    double seconds () const { return length / sampleRate; }
    // Level 0 is the sample itself.
    int levels () const { return 1 + (int)mips.size (); }
    const float* levelData (int level, int c) const
    {
        if (level <= 0)
            return data (c);
        const auto& m = mips[(size_t)level - 1];
        return m.ch[c < numChannels ? c : 0].data ();
    }
    int levelLength (int level) const { return level <= 0 ? length : mips[(size_t)level - 1].length; }

    // Builds the band-limited levels (loaders call it; off the audio thread).
    void buildMips ();

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

bool isSupportedAudioFile (const std::string& path); // by extension, or by content
std::string sniffAudioFormat (const std::string& path);  // "wav", "flac", "mp3" or "" from the first bytes

// UTF-8 <-> std::filesystem::path, portable across C++20 (char8_t) and Windows wide paths.
std::filesystem::path pathFromUtf8 (const std::string& s);
std::string utf8FromPath (const std::filesystem::path& p);

} // namespace smemplr

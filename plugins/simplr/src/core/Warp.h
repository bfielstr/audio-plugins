// Tempo-synchronised playback engines used when Warp is on.
//
// All engines follow a "virtual" timeline position v (source frames) that advances by
// srcPerOut per output sample, so the sample stays locked to the host tempo, while the pitch
// is set independently by pitchRatio.
//   Beats   : plays transient-bounded segments at the pitch rate, looping/gating segment tails
//   Tones   : overlap-add grains aligned by waveform similarity (WSOLA)
//   Texture : overlap-add grains with random position scatter ("flux")
//   Complex : phase-locked phase vocoder + resampling; Complex Pro adds formant correction
#pragma once

#include "Fft.h"
#include "SampleData.h"

#include <cstdint>
#include <vector>

namespace simplr {

struct PlayRegion
{
    double start = 0.0, end = 0.0; // source frames, playback region
    bool loop = false;
    double loopStart = 0.0, loopEnd = 0.0;

    double map (double v) const
    {
        if (!loop || v < loopEnd)
            return v;
        const double len = loopEnd - loopStart;
        if (len < 1.0)
            return loopStart;
        double x = std::fmod (v - loopStart, len);
        return loopStart + x;
    }
    bool pastEnd (double v) const { return !loop && v >= end; }
};

struct WarpRates
{
    double srcPerOut = 1.0;  // timeline source frames per output sample (tempo sync)
    double pitchRatio = 1.0; // 2^(semitones/12)
    double srcRate = 1.0;    // source sample rate / host sample rate
    double formantShift = 1.0; // PvWarp: moves the spectral envelope by this ratio (1 = off)
};

//==============================================================================
class BeatsWarp
{
public:
    void prepare (double hostSr);
    void start (const SampleData& s, const PlayRegion& r, const std::vector<int>& boundaries, int loopMode,
                float envelope01);
    void render (const SampleData& s, float* L, float* R, int n, const WarpRates& w);

    double virtualPos () const { return v; }
    double displayPos () const { return cur.pos; }
    bool finished = false;

private:
    struct Reader
    {
        double pos = 0, segStart = 0, segEnd = 0, tailStart = 0;
        int dir = 1;
        bool active = false;
    };
    void readerTick (const SampleData& s, Reader& rd, double rate, float cutoff, float& l, float& r) const;
    void beginSegment (int idx, double p, const WarpRates& w);

    PlayRegion region;
    std::vector<double> seg;
    double v = 0.0, lastP = 0.0, segOutElapsed = 0.0, segOutLen = 1.0;
    int curIdx = -1, loopMode = 1;
    float envelope = 1.0f;
    Reader cur, old;
    int oldFade = 0, fadeLen = 128;
    double hostSr = 44100.0;
};

//==============================================================================
class GrainWarp
{
public:
    void prepare (double hostSr);
    void start (const PlayRegion& r, bool tones, float grainMs, float flux01, uint32_t seed);
    void render (const SampleData& s, float* L, float* R, int n, const WarpRates& w);

    double virtualPos () const { return v; }
    double displayPos () const { return region.map (v); }
    bool finished = false;

private:
    struct Grain
    {
        double src = 0.0;
        int age = 0, len = 1;
        bool active = false, flatStart = false;
    };
    double align (const SampleData& s, double natural, double target, double rate) const;

    PlayRegion region;
    Grain grains[4];
    double v = 0.0, lastGrainSrc = 0.0, lastGrainRate = 1.0;
    int hopCounter = 0, grainLen = 1024, lastGrainHop = 512;
    bool tones = true, first = true;
    float flux = 0.0f;
    uint32_t seed = 1;
    double hostSr = 44100.0;
};

//==============================================================================
class PvWarp
{
public:
    void prepare (double hostSr);
    // frameSize: 1024, 2048 or 4096; 0 picks 2048 (4096 above 50 kHz).
    void start (const SampleData& s, const PlayRegion& r, bool formantMode, float formants01, int envelopeOrder,
                int frameSize = 0);
    void render (const SampleData& s, float* L, float* R, int n, const WarpRates& w);

    double virtualPos () const { return v; }
    double displayPos () const { return region.map (v); }
    bool finished = false;

private:
    void synthesiseFrame (const SampleData& s, const WarpRates& w);
    void readFrame (const SampleData& s, double centre, float* mid, float* l, float* r) const;

    static constexpr int kMaxN = 4096;
    static constexpr int kFifo = 16384;
    PlayRegion region;
    Fft fft1k {1024}, fft2k {2048}, fft4k {4096};
    Fft* fft = &fft2k;
    int N = 2048, hs = 512;
    bool stereo = false, formantMode = false, firstFrame = true;
    float formants = 1.0f;
    int envOrder = 128;
    double apos = 0.0, v = 0.0;
    double readPos = 0.0;      // fifo read position (absolute)
    long long written = 0;     // fifo frames written (absolute)
    long long endWritten = -1; // fifo index where the region ended
    std::vector<float> window, fa, fb, fl, fr, olaL, olaR, olaW, fifoL, fifoR, synthPhase, corr, cep, logEnv;
    std::vector<Fft::cf> sa, sb, sl, sr, stmp;
    std::vector<int> peakOf;
    double hostSr = 44100.0;
};

} // namespace simplr

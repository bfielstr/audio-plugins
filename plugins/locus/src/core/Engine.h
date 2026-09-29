// Low-end contrast processor in the spirit of Ozone's Low End Focus.
//
// A phase-coherent STFT splits the signal into narrow bands (~12 Hz each at 48 kHz). For every
// bin inside the focus range the processor compares the bin's level with its neighbourhood (the
// level of nearby bins). Positive contrast pushes bins that sit below their neighbourhood further
// down and lets the dominant events stand out (clarity); negative contrast pulls everything
// towards the neighbourhood level (density / weight).
// The bin level and the neighbourhood level are smoothed over time with the same time constant,
// so a change of level (a hit, a gate or expander opening) leaves their ratio alone: contrast
// follows the shape of the spectrum, never its level. The loudness inside the range is kept
// steady so Contrast changes focus, not level; Gain then sets the level of the range. Punchy and
// Smooth differ in how fast the contrast follows the spectrum and in the attack / release of the
// gains. Bins outside the range pass untouched (perfect reconstruction), delayed by latency().
#pragma once

#include "Fft.h"
#include "Params.h"

#include "smacheratr/src/core/Tail.h"

#include <array>
#include <atomic>
#include <vector>

namespace locus {

using ParamArray = std::array<double, kNumParams>;
ParamArray defaultParams ();

// Spectrum snapshot for the editor (written by the audio thread once per frame).
struct Spectrum
{
    static constexpr int kMaxBins = 512;
    std::atomic<int> bins {0};
    std::atomic<float> binHz {0.0f};
    std::array<std::atomic<float>, kMaxBins> inputDb {};
    std::array<std::atomic<float>, kMaxBins> outputDb {};
    std::array<std::atomic<float>, kMaxBins> gainDb {};
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setParam (uint32_t id, double plain)
    {
        p[id] = plain;
        if (id >= kTailExtBase)
            tail.setParam (pk::kTailFields + (id - kTailExtBase), plain);
        else if (id >= kTailBase)
            tail.setParam (id - kTailBase, plain);
    }
    double param (uint32_t id) const { return p[id]; }
    int latency () const { return fftSize + tail.latency (); } // the STFT and the end-of-chain saturator
    void setTailMeters (smacheratr::Meters* m) { tail.setMeters (m); }

    // In-place capable.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // Optional destination for spectrum snapshots (may be null).
    void setSpectrum (Spectrum* s) { spectrum = s; }

private:
    void processFrame ();
    float rangeWeight (int bin) const;

    ParamArray p = defaultParams ();
    double sr = 48000.0;
    int fftSize = 4096, hop = 512;
    Fft fft {4096};
    std::vector<float> window, inL, inR, accL, accR, frame, lvl, ref, weight, gainDb, mag;
    std::vector<Fft::cf> specL, specR;
    int inPos = 0, hopCount = 0, outPos = 0;
    float norm = 1.0f, normDbSmoothed = 0.0f;
    float outGain = 1.0f, smooth = 0.0f;
    Spectrum* spectrum = nullptr;
    smacheratr::Tail tail;
};

} // namespace locus

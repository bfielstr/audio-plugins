// Ciphr's built-in waves: single-cycle tables generated when the module loads (no files), each kept as a
// set of band-limited copies, one per octave (mipmaps), so a high note never reads harmonics above
// Nyquist.
//
// A table holds kTableSize samples of one cycle (plus guard samples for the linear read). Level k of a
// wave holds its harmonics 1 .. kMaxHarmonics >> k: level 0 the full 512, level 9 the fundamental only.
// levelFor (inc) picks the fullest level whose highest harmonic still stays at or under Nyquist for a
// phase step of `inc` cycles per sample, so nothing folds back (aliasing). The tables are built by an
// inverse FFT from each wave's harmonic amplitudes and scaled so the wave's full table peaks at 1.
#pragma once

#include <cstdint>
#include <vector>

namespace ciphr {

constexpr int kTableSize = 2048; // samples per cycle (4 x the most harmonics: the linear read stays clean)
constexpr int kTableLevels = 10; // mipmaps: level k holds harmonics up to kMaxHarmonics >> k
constexpr int kMaxHarmonics = 512;
constexpr int kTableStride = kTableSize + 2;

enum Wave
{
    kWaveSine = 0,
    kWaveTriangle,
    kWaveSaw,
    kWaveSquare,
    kWavePulse,  // 25 % pulse
    kWaveOrgan,  // harmonics 1, 2, 3, 4, 6, 8
    kWaveVowelA, // a sloping spectrum with two formant-like bumps (harmonics ~5 and ~9)
    kWaveVowelO, // bumps at harmonics ~3 and ~6
    kWaveHollow, // odd harmonics falling as 1 / h^1.5
    kWaveBuzz,   // every harmonic at 1 / sqrt (h): very bright
    kWaveSoft,   // every harmonic at 1 / h^2: a rounded saw
    kWaveGlass,  // sparse harmonics 1, 3, 7, 11, 16
    kNumWaves
};

const char* waveName (int wave);
// Harmonic h's amplitude (h >= 1) in a wave's spectrum, before the table's scaling.
double waveHarmonic (int wave, int h);

class WaveBank
{
public:
    // The tables, built on first use (thread-safe: a function-local static).
    static const WaveBank& get ();

    // kTableStride samples: one cycle and its first two samples again (the read's guards: a phase that
    // rounds up to 1.0 still reads inside).
    const float* table (int wave, int level) const
    {
        return data.data () + ((size_t)wave * kTableLevels + (size_t)level) * kTableStride;
    }
    static constexpr int harmonicsAt (int level) { return kMaxHarmonics >> level; }
    // The fullest level whose top harmonic is at or under Nyquist for a phase step of `inc` cycles per
    // sample; -1 when even the fundamental is above it (silence).
    static int levelFor (double inc)
    {
        if (inc <= 0.0)
            return 0;
        const double maxH = 0.5 / inc;
        if (maxH < 1.0)
            return -1;
        int k = 0;
        while (k < kTableLevels - 1 && (double)harmonicsAt (k) > maxH)
            ++k;
        return k;
    }
    // A linear read at phase 0 .. 1.
    static inline float read (const float* t, double phase)
    {
        const double p = phase * kTableSize;
        const int i = (int)p;
        const float f = (float)(p - i);
        return t[i] + f * (t[i + 1] - t[i]);
    }

private:
    WaveBank ();
    std::vector<float> data;
};

} // namespace ciphr

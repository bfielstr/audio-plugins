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
    // ---- 0.18: appended (the numbers above are in Variant's draws and must never move)
    kWavePlate,   // sparse harmonics 2, 5, 8, 10, 13, 17, 20 over a weak fundamental: a hollow, struck plate
    kWaveMetal,   // clusters of three neighbouring harmonics (around 7, 14, 23, 34, 48) that beat: a clang
    kWaveScrape,  // a dense band of harmonics 12 .. 220, uneven levels and scattered phases: a bright scrape
    kWaveScreech, // a narrow, strong peak around harmonic 30 (and a weaker one near 47): a whistling howl
    kWaveDeepOO,  // vowel "oo" with the formants of a low voice (300, 870, 2240 Hz at C2)
    kWaveDeepAA,  // vowel "aa" (730, 1090, 2440 Hz at C2)
    kWaveDeepOH,  // vowel "oh" (570, 840, 2410 Hz at C2)
    kWaveThroat,  // a low formant and a very narrow peak on harmonic 12: throat singing's whistle over a drone
    kWaveChoir,   // a sung "ah" (650, 1080, 2650 Hz and the singer's peak near 3 kHz at C3), scattered phases
    kNumWaves
};
// The waves before 0.18: Variant draws from these alone with Wave Set at Classic, so every Variant number
// keeps the patch it always had.
constexpr int kNumClassicWaves = kWaveGlass + 1;
static_assert (kNumClassicWaves == 12, "the classic waves are fixed");

const char* waveName (int wave);
// Harmonic h's amplitude (h >= 1) in a wave's spectrum, before the table's scaling.
double waveHarmonic (int wave, int h);
// Harmonic h's phase (radians, added to a sine): 0 for every classic wave; the noisy and choral waves
// scatter theirs so the cycle has no single sharp peak.
double wavePhase (int wave, int h);

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

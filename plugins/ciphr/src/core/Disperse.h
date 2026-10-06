// Ciphr's Disperse: a bank of band-pass filters (4 to 32 bands, log-spaced from 80 Hz to 12 kHz, constant
// Q) that splits the voices' sum into bands, like the analysis half of a vocoder, and raises the bands one
// after another as its dial turns. Seed shuffles the order the bands are raised in (a SplitMix64 shuffle:
// the same Seed and Bands always give the same order).
//
//   dial 0          every band silent (-inf dB)
//   dial turning    the bands rise in the seeded order, each from silent to full (0 dB) over its own slice
//                   of the dial; the slices overlap (each 2 / Bands of the dial, at least), so the reveal
//                   is continuous: about two bands are on their way up at any moment
//   dial 100 %      every band full: the bands sum to about the input (flat within about 1 dB from 100 Hz
//                   to 10 kHz at Width 100 %; below 80 Hz and above 12 kHz they roll off)
//
// A band's gain on its slice is smoothstep (u)^2 (u: 0 .. 1 across the slice): silent at its start,
// -12 dB half way, 0 dB at its end, with no corner either side. The dial is smoothed (30 ms) and every
// band's gain ramps sample by sample, so a fast turn does not zipper.
//
// Width narrows the bands (Q x 1 at 100 %, x 8 at 0): narrow bands ring and leave gaps between them, the
// vocoder's comb. The bank's level is normalized for the Bands and the Width (the mean power of the sum
// from 100 Hz to 10 kHz is the input's), so changing them keeps the loudness.
//
// On and Mix: Mix crossfades the input (dry) and the bands. Switched off, the mix fades out over 20 ms
// and the bank then stops altogether: idle () is true and the engine skips it, so with Disperse off ciphr
// is bit for bit what it was without it. A change of Bands fades the bank out and back in (10 ms each
// way) around the new filters.
//
// Stereo: one bank per channel, shared by every voice (it runs once after the voices' sum).
#pragma once

#include <cstdint>

namespace ciphr {

class Disperse
{
public:
    static constexpr int kMaxBands = 32, kMinBands = 4;
    static constexpr double kLowHz = 80.0, kHighHz = 12000.0;
    static constexpr int kSlice = 32; // gains are worked out every kSlice samples and ramped between
    // Width 100 %: the bands' Q is this times the Q at which neighbours cross at -3 dB (the flattest sum)
    static constexpr double kFlatQ = 0.5;
    static constexpr double kNarrowQ = 8.0; // Width 0: this many times narrower

    void prepare (double sampleRate);
    void reset (); // silence, every smoothed setting at its target

    void setOn (bool on) { onT = on; }
    void setAmount (double a);
    void setBands (int bands);
    void setSeed (int seed);
    void setWidth (double w);
    void setMix (double m);

    // Off and faded out: the engine skips the bank (the signal passes untouched, bit for bit).
    bool idle () const { return !onT && mixNow == 0.0f && !active; }
    // In place.
    void process (float* l, float* r, int n);

    // ---- the bank's layout (for the display and the tests)
    int bands () const { return nBands; }
    // band b's centre (Hz) with `bands` bands
    static double centreHz (int b, int bands);
    // rankOf[b]: when band b is raised (0 first); bandAt[j]: the band raised j-th
    static void order (int bands, int seed, int* rankOf, int* bandAt = nullptr);
    // a band's gain (0 .. 1) at dial position `amount` (0 .. 1), from its rank
    static double revealGain (double amount, int rank, int bands);
    // where on the dial the band of rank `rank` starts to rise and where it is full
    static double revealStart (int rank, int bands);
    static double revealEnd (int rank, int bands);
    double bandGain (int b) const { return b >= 0 && b < nBands ? gain[b] : 0.0; } // now
    double amountNow () const { return amount; }

private:
    void configure (); // the filters for nBands at the current width
    void updateCoefs ();
    void targets (float* out) const; // every band's gain at the smoothed dial
    double normFor (int bands, double width) const;

    double sr = 48000.0;
    bool onT = false, active = false;
    double amountT = 0.5, amount = 0.5, widthT = 1.0, width = 1.0;
    int bandsT = 16, nBands = 16, seed = 1;
    int rankOf[kMaxBands] {};
    float mixT = 1.0f, mixNow = 0.0f, mixSmooth = 0.001f;
    double sliceSmooth = 0.1;
    // the Bands switch: fade the bank out, change, fade back in
    float fade = 1.0f, fadeStep = 0.002f;
    bool switching = false;
    // normalization: per band count, per width step (0 .. kWidthSteps)
    static constexpr int kWidthSteps = 16;
    float norm[kMaxBands - kMinBands + 1][kWidthSteps + 1] {};
    // the filters (structure of arrays: the band loop vectorizes), TPT state-variable band-passes
    alignas (32) float a1[kMaxBands] {}, a2[kMaxBands] {}, a3[kMaxBands] {}, k[kMaxBands] {};
    alignas (32) float s1[2][kMaxBands] {}, s2[2][kMaxBands] {};
    alignas (32) float gain[kMaxBands] {}, step[kMaxBands] {};
    float level = 1.0f; // the normalization now
    int pos = 0;        // samples into the slice
};

} // namespace ciphr

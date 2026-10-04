// 4x oversampling as two 2x stages, each a linear-phase Kaiser-windowed sinc half-band FIR used
// for both the up- and the down-sampling, or 2x with the first stage alone (the steep one: the second
// only has to clear the band the first leaves above 20 kHz, so it is short, and 2x's delay is most of
// 4x's). The delay is a whole number of input samples.
#pragma once

#include <vector>

namespace smacheratr {

class Halfband2x
{
public:
    // passEdge: end of the passband as a fraction of the input rate (the stopband starts at its
    // alias, 1 - passEdge). Taps are 4k + 1 so the up + down delay is a whole input sample count.
    void design (double passEdge, double attenDb, int maxTaps);
    void reset ();
    int latency () const { return ((int)h.size () - 1) / 2; } // input samples, up + down together
    void up (const float* in, float* out, int n);   // out: 2n samples
    void down (const float* in, float* out, int n); // in: 2n samples

private:
    // The FIR runs at the 2x rate, but up-sampling feeds it a zero every other sample and down-sampling
    // keeps every other output: each output is worked out only from the samples that count (the even
    // taps against the input for the up-sampler's even outputs, the odd taps for its odd ones; the down-
    // sampler's input split into its even and odd samples), four outputs at a time in one SIMD register.
    // Every output is the same sum, in the same order, of the same products as the plain FIR's (the
    // zeros it adds change nothing), so the result is that FIR's to the bit.
    static constexpr int kChunk = 64; // input samples per pass
    std::vector<float> h, hEven, hOdd;
    int hist = 0; // samples of history each buffer keeps (2k for 4k + 1 taps)
    std::vector<float> upBuf, evenBuf, oddBuf; // hist samples of history, then a chunk
    void upChunk (const float* in, float* out, int n);
    void downChunk (const float* in, float* out, int n);
};

class Oversampler
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    int latency () const { return s1.latency () + s2.latency () / 2; } // input samples
    void up (const float* in, float* out, int n);   // out: 4n samples
    void down (const float* in, float* out, int n); // in: 4n samples
    // 2x: the first stage alone (a reset () between switching from 4x to 2x and back)
    int latency2x () const { return s1.latency (); }
    void up2x (const float* in, float* out, int n) { s1.up (in, out, n); }     // out: 2n samples
    void down2x (const float* in, float* out, int n) { s1.down (in, out, n); } // in: 2n samples
    // by factor (1, 2 or 4; 1 is no oversampling: no delay)
    int latency (int factor) const { return factor >= 4 ? latency () : factor == 2 ? latency2x () : 0; }

private:
    Halfband2x s1, s2;
    std::vector<float> mid;
};

} // namespace smacheratr

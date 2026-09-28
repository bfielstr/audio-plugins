// 4x oversampling as two 2x stages, each a linear-phase Kaiser-windowed sinc half-band FIR used
// for both the up- and the down-sampling. The delay is a whole number of input samples.
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
    struct Fir
    {
        std::vector<float> hist;
        int pos = 0, mask = 0;
        void resize (int taps);
        void reset ();
        void push (float x)
        {
            hist[(size_t)pos] = x;
            pos = (pos + 1) & mask;
        }
        float run (const std::vector<float>& h) const;
    };
    std::vector<float> h;
    Fir upFir, downFir;
};

class Oversampler
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    int latency () const { return s1.latency () + s2.latency () / 2; } // input samples
    void up (const float* in, float* out, int n);   // out: 4n samples
    void down (const float* in, float* out, int n); // in: 4n samples

private:
    Halfband2x s1, s2;
    std::vector<float> mid;
};

} // namespace smacheratr

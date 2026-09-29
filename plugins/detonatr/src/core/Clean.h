// Detonatr's Clean stage: a spectral denoiser and dereverber (short-time Fourier transform), in the
// spirit of RX's De-noise and De-reverb but simpler. Denoise: a noise floor per frequency is tracked
// from the quiet parts, and bins that do not rise clearly above it are turned down. Dereverb: the part
// of each bin that is only a decaying tail of what came before is turned down, so a hit stays and its
// room goes.
#pragma once

#include <vector>

namespace detonatr {

class Clean
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    void setDenoise (double amount);  // 0 .. 1 (0: off)
    void setDereverb (double amount); // 0 .. 1 (0: off)
    int latency () const;             // samples; constant for a sample rate, whatever the settings
    void process (float* L, float* R, int n); // in place, stereo

private:
    void processFrame ();
    void computeGains ();
    void fft (float* re, float* im) const; // in place, forward, unnormalized (swap re/im for inverse)

    using Vec = std::vector<float>;

    double sr = 48000.0;
    int N = 2048, hop = 512, bins = 1025;
    int pos = 0, rd = 1; // write position in the input frame; read position in the finished hop
    float denoise = 0.0f, dereverb = 0.0f;

    // per-frame constants (they depend on the hop's length in seconds): one-pole smoothing coefficients,
    // the floor's rise per frame, the power a typical room's tail loses over histLen frames
    float aGain = 0.0f, aFloor = 0.0f, aStat = 0.0f, aReverb = 0.0f, release = 0.0f;
    float aTonalFall = 0.0f, aTonalRise = 0.0f, aTonalSlow = 0.0f;
    float rise = 1.0f, tailDecay = 0.0f;
    int subLen = 1, subCount = 0, subIdx = 0, histLen = 4, histPos = 0;

    // the short-time Fourier transform
    Vec win, cosT, sinT, re, im;
    std::vector<int> rev;
    Vec inL, inR, accL, accR, outL, outR;

    // per frequency bin (bins of them; sub-window and history stores are kSub or histLen times that)
    Vec pw, pg, pf, ps;              // power: raw; smoothed (across frequency, then time) for the gains, the floor, stationarity
    Vec noiseFloor, capped;          // slowly rising minimum of pf, and the same after the cross-frequency cap
    Vec curMinPf, curMinPs, curMaxPs; // the sub-window being filled
    Vec subMinPf, subMinPs, subMaxPs; // the last kSub finished sub-windows
    Vec hist;                        // pg of the last histLen frames (the reverb reference)
    Vec z1r, z1i, z2r, z2i;          // the last two frames' spectra (all N bins), for the tonality
    Vec unpred, unpredSlow, tonal;   // how unpredictable each bin is (smoothed quickly, slowly); tonality 0 .. 1
    Vec rvGain;                      // the dereverb's own gain in dB, smoothed near the tail's level
    Vec gRaw, gTmp, gain;            // gains: this frame's, smoothed across frequency, smoothed across time
};

} // namespace detonatr

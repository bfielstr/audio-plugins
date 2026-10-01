// The Tape stage: two-band tape saturation modelled on FabFilter Saturn 2's Warm Tape style, with the
// split at Split (200 Hz by default) and per band Drive, Dynamics, Level and Mix.
//
// The split is linear phase and complementary (Saturn's Linear Phase crossover): the lows are the
// input through four moving averages B of L samples, twiced (2 B - B B, flat further down), the highs
// the input (delayed as much) minus the lows, so the two add back to the input exactly. L is the
// length whose lows are 6 dB down at Split. The split's delay depends on L, so the input is delayed
// first by what the longest L (the lowest Split, 80 Hz) needs more: the latency stays constant.
// Each band is saturated at 4x (Smacheratr's oversampler, as Saturn's High Quality), on its own:
//   - Drive (dB) into a warm tape curve: a tanh with a bias (asymmetric: even harmonics too), its
//     small-signal gain and offset taken out, then a gentle roll-off of the highs (a one-pole at 14 kHz),
//   - level compensation: the output scaled by what keeps a -18 dBFS sine as loud as it went in, so
//     Drive changes the colour and not much the level,
//   - Dynamics (-100 .. +100 %): the drive follows the band's level (its RMS over 20 ms) around
//     -18 dBFS, half a dB per dB at 100 %: positive drives louder parts harder (and louder), negative
//     the quieter ones,
//   - Mix against the band unsaturated, and Level (dB).
// What the saturation adds to a band goes through a DC blocker (5 Hz): the asymmetric curve makes an
// offset; the band itself is not filtered, so with Mix at 0 the stage is the input, delayed.
#pragma once

#include "Dsp.h"

#include "smacheratr/src/core/Oversampler.h"

#include <array>
#include <vector>

namespace detonatr {

class Tape
{
public:
    static constexpr double kMinSplit = 80.0, kMaxSplit = 1000.0, kBias = 0.2, kWarmHz = 14000.0;
    static constexpr int kBands = 2;

    void prepare (double sampleRate, int maxBlock);
    void reset ();
    int latency () const { return 4 * (maxLen - 1) + over[0][0].latency (); }

    void setSplit (double hz);
    void setDriveDb (int band, double db);
    void setMix (int band, double m) { bands[(size_t)band].mix = (float)std::clamp (m, 0.0, 1.0); }
    void setDynamics (int band, double d) { bands[(size_t)band].dyn = (float)std::clamp (d, -1.0, 1.0); }
    void setLevelDb (int band, double db) { bands[(size_t)band].level = (float)std::pow (10.0, db / 20.0); }

    void process (float* l, float* r, int n);

    // the curve as it shapes a band (for the display): out for an input level x, at the band's drive
    float curve (int band, float x) const;
    double splitHz () const { return split; }
    // the lows' response of the split at hz (the highs' is 1 minus it)
    double lowResponse (double hz) const;

private:
    struct Band
    {
        float drive = 2.0f, comp = 0.5f, mix = 1.0f, dyn = 0.0f, level = 1.0f;
        float env = 0.0f;
        float warm[2] {};
        float dcX[2] {}, dcY[2] {}; // the DC blocker on what the saturation adds
    };
    struct Box
    {
        std::vector<double> buf;
        double sum = 0.0;
        int pos = 0;
        void prepare (int len)
        {
            buf.assign ((size_t)std::max (1, len), 0.0);
            sum = 0.0;
            pos = 0;
        }
        inline double push (double x)
        {
            sum += x - buf[(size_t)pos];
            buf[(size_t)pos] = x;
            if (++pos >= (int)buf.size ())
            {
                pos = 0;
                sum = 0.0;
                for (double v : buf)
                    sum += v;
            }
            return sum / (double)buf.size ();
        }
    };
    struct Ring
    {
        std::vector<float> buf;
        int pos = 0, len = 0;
        void prepare (int n)
        {
            len = std::max (0, n);
            buf.assign ((size_t)std::max (1, len), 0.0f);
            pos = 0;
        }
        inline float push (float x)
        {
            if (len == 0)
                return x;
            const float y = buf[(size_t)pos];
            buf[(size_t)pos] = x;
            if (++pos >= len)
                pos = 0;
            return y;
        }
    };
    static int lengthFor (double hz, double sr);
    void configure ();
    void updateComp (int band);
    float shape (const Band& b, float x, float drive) const;

    double sr = 48000.0, split = 200.0;
    int maxLen = 2, len = 2, maxBlock = 512;
    bool dirty = true;
    std::array<Band, kBands> bands {};
    Box boxes[2][8];
    Ring pad[2], mid[2], highDelay[2];
    smacheratr::Oversampler over[kBands][2];
    std::vector<float> bandBuf[kBands][2], up, driveBuf;
    float envC = 0.01f, warmC = 0.5f, dcR = 0.999f;
    float biasOut = 0.19737532f, biasSlope = 1.0405362f; // tanh (kBias) and 1 / (1 - tanh^2 (kBias))
};

} // namespace detonatr

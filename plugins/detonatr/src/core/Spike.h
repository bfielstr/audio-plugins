// The Spike stage: a spectral transient processor in the spirit of oeksound's Spiff. It finds the
// transients in each of 16 frequency bands and boosts them (Boost) or cuts them (Cut).
//
// The bands: 16 band-pass filters (two cascaded sections each), log-spaced from Low to High. In each
// band and channel two followers run on the band's level: a fast one (0.1 ms attack, 15 ms release)
// and a slow one whose attack is set by Sharpness (17 ms at 1.3, down to 3.6 ms at 10: the sharper,
// the quicker the slow one catches up, so only sharper onsets count). A transient is where the fast
// level is above the slow one: by t dB. Sensitivity (0 .. 10) sets how far above counts: from 10 - S
// dB on, fully 6 dB further. Depth (0 .. 10) is the most the band is turned up (Boost) or down (Cut):
// 1.8 dB per step (5.1: 9.2 dB). The gain moves there at once and comes back over Decay
// (0 .. 10: 2 ms x 2^(0.7 Decay), 63 ms at 7.1); Decay LF/HF tilts it (-10 .. +10: at +10 the highest
// band takes 4 times as long and the lowest a quarter; the frequency curve is otherwise flat).
// Stereo Link blends each channel's own detection (0 %) with the louder channel's (100 %).
// The change is added to the input: out = in + Mix x the sum over bands of (gain - 1) x band, so with
// no transient (or Mix 0) the input passes untouched. Trim is an output gain. No latency.
#pragma once

#include "Dsp.h"

#include <array>

namespace detonatr {

class Spike
{
public:
    static constexpr int kBands = 16;
    enum Mode { kCut = 0, kBoost = 1 };

    void prepare (double sampleRate, int maxBlock);
    void reset ();
    int latency () const { return 0; }

    void setMode (int m) { boost = m == kBoost; }
    void setDepth (double d) { maxDb = (float)(1.8 * std::clamp (d, 0.0, 10.0)); }
    void setSensitivity (double s) { threshDb = (float)(10.0 - std::clamp (s, 0.0, 10.0)); }
    void setDecay (double d);
    void setSharpness (double s);
    void setDecayTilt (double t);
    void setLink (double l) { link = (float)std::clamp (l, 0.0, 1.0); }
    void setRange (double lowHz, double highHz);
    void setMix (double m) { mix = (float)std::clamp (m, 0.0, 1.0); }
    void setTrimDb (double db) { trim = (float)std::pow (10.0, db / 20.0); }

    void process (float* l, float* r, int n);

    double centre (int b) const { return centres[(size_t)b]; }
    // the gain change in each band now (dB, the larger of the two channels)
    float bandGainDb (int b) const { return std::max (gainDb[(size_t)b][0], gainDb[(size_t)b][1]) * (boost ? 1.0f : -1.0f); }

private:
    void design ();
    void updateDecay ();
    double sr = 48000.0;
    double lowHz = 20.0, highHz = 20000.0, decay = 7.1, tilt = 0.0, sharpness = 1.3;
    bool boost = true;
    float maxDb = 9.18f, threshDb = 6.3f, link = 1.0f, mix = 1.0f, trim = 1.0f;
    float fastAtt = 0.5f, fastRel = 0.001f, slowAtt = 0.001f, gainAtt = 0.1f;
    std::array<double, kBands> centres {};
    std::array<std::array<dsp::BandPass, 2>, kBands> filters {};
    std::array<float, kBands> gainRel {};
    std::array<std::array<float, 2>, kBands> fast {}, slow {}, gainDb {};
};

} // namespace detonatr

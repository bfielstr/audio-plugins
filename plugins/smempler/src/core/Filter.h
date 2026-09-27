// Multi-mode filter with several circuit flavours.
//   Clean : linear trapezoidal SVF (Cytomic/Simper), 12 dB or two cascaded stages for 24 dB
//   OSR   : SVF whose resonant state is hard-clipped (diode-style resonance limiting)
//   MS2   : SVF with a soft-clipped resonant state (Sallen-Key-like character)
//   SMP   : 4-pole ladder with a soft-clipped feedback path
//   PRD   : 4-pole ladder with a saturating input stage and no explicit resonance limiting
// MS2/SMP/PRD are only offered for low-pass and high-pass (as in the original); other types
// fall back to Clean.
#pragma once

#include "Params.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace smempler {

inline float fastTanh (float x)
{
    // Pade approximation, accurate to ~1e-3 and exactly bounded to [-1, 1].
    if (x > 4.97f)
        return 1.0f;
    if (x < -4.97f)
        return -1.0f;
    const float x2 = x * x;
    const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return std::clamp (a / b, -1.0f, 1.0f);
}

struct FilterSettings
{
    int type = kLowpass;
    int circuit = kClean;
    bool slope24 = false;
    float cutoff = 1000.0f; // Hz, already modulated
    float res = 0.0f;       // 0..1
    float driveDb = 0.0f;
    float morph = 0.0f;     // 0..1

    int effectiveCircuit () const { return circuitSupported (type, circuit) ? circuit : kClean; }
};

// Morph weights (lp, bp, hp) along LP -> BP -> HP -> Notch -> LP.
inline void morphWeights (float m, float& wl, float& wb, float& wh)
{
    static const float pts[5][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}};
    m = std::clamp (m, 0.0f, 1.0f) * 4.0f;
    int i = std::min (3, (int)m);
    float f = m - (float)i;
    wl = pts[i][0] + f * (pts[i + 1][0] - pts[i][0]);
    wb = pts[i][1] + f * (pts[i + 1][1] - pts[i][1]);
    wh = pts[i][2] + f * (pts[i + 1][2] - pts[i][2]);
}

inline void typeWeights (int type, float morph, float& wl, float& wb, float& wh)
{
    switch (type)
    {
        case kLowpass: wl = 1, wb = 0, wh = 0; break;
        case kHighpass: wl = 0, wb = 0, wh = 1; break;
        case kBandpass: wl = 0, wb = 1, wh = 0; break;
        case kNotch: wl = 1, wb = 0, wh = 1; break;
        default: morphWeights (morph, wl, wb, wh); break;
    }
}

class MultiFilter
{
public:
    void reset ()
    {
        for (auto& c : ch)
            c = Chan {};
    }

    void setup (const FilterSettings& s, float sr)
    {
        settings = s;
        circuit = s.effectiveCircuit ();
        const float fc = std::clamp (s.cutoff, 20.0f, std::min (22000.0f, sr * 0.45f));
        g = std::tan ((float)M_PI * fc / sr);
        const float res = std::clamp (s.res, 0.0f, 1.0f);
        typeWeights (s.type, s.morph, wl, wb, wh);
        driveGain = circuit == kClean ? 1.0f : std::pow (10.0f, s.driveDb / 20.0f);
        makeup = 1.0f / std::sqrt (driveGain);

        if (circuit == kSMP || circuit == kPRD)
        {
            G = g / (1.0f + g);
            k = res * 4.15f;
            ladderGain = 1.0f + 0.55f * std::min (k, 4.0f);
            return;
        }
        // SVF variants
        const bool nonlinear = circuit != kClean;
        if (!s.slope24)
        {
            k1 = nonlinear ? 1.4142f - 1.5f * res : 1.4142f * (1.0f - 0.99f * res);
            coeff (k1, a[0]);
        }
        else
        {
            k1 = 1.8478f;
            k2 = nonlinear ? 0.7654f - 0.83f * res : 0.7654f * (1.0f - 0.985f * res);
            coeff (k1, a[0]);
            coeff (k2, a[1]);
        }
        // Normalise the band-pass output to unity peak gain.
        bpNorm0 = std::max (0.02f, k1);
        bpNorm1 = std::max (0.02f, k2);
    }

    float process (float x, int c)
    {
        Chan& st = ch[c];
        if (circuit != kClean)
            x = fastTanh (x * driveGain);
        float y;
        if (circuit == kSMP || circuit == kPRD)
            y = ladder (x, st) * ladderGain;
        else if (!settings.slope24)
            y = svfStage (x, st.s[0], a[0], k1, bpNorm0, circuit != kClean);
        else
        {
            const float y1 = svfStage (x, st.s[0], a[0], k1, bpNorm0, false);
            y = svfStage (y1, st.s[1], a[1], k2, bpNorm1, circuit != kClean);
        }
        return circuit != kClean ? y * makeup : y;
    }

private:
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
    };
    struct Chan
    {
        Svf s[2];
        float l[4] {};
    };
    struct Coef
    {
        float a1, a2, a3;
    };

    void coeff (float kk, Coef& c) const
    {
        c.a1 = 1.0f / (1.0f + g * (g + kk));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
    }

    float svfStage (float v0, Svf& s, const Coef& c, float kk, float bpn, bool limit) const
    {
        const float v3 = v0 - s.ic2;
        const float v1 = c.a1 * s.ic1 + c.a2 * v3;
        const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;
        if (limit)
        {
            if (circuit == kOSR)
                s.ic1 = std::clamp (s.ic1, -1.6f, 1.6f);   // hard-clipping diode
            else
                s.ic1 = 1.8f * fastTanh (s.ic1 * (1.0f / 1.8f)); // soft clip
            s.ic2 = std::clamp (s.ic2, -8.0f, 8.0f);
        }
        const float lp = v2, bp = v1 * bpn, hp = v0 - kk * v1 - v2;
        return wl * lp + wb * bp + wh * hp;
    }

    float ladder (float x, Chan& st) const
    {
        const float s1 = st.l[0], s2 = st.l[1], s3 = st.l[2], s4 = st.l[3];
        const float inv = 1.0f / (1.0f + g);
        const float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
        const float sigma = G3 * s1 * inv + G2 * s2 * inv + G * s3 * inv + s4 * inv;
        const float y4lin = (G4 * x + sigma) / (1.0f + k * G4);
        float u;
        if (circuit == kPRD)
            u = fastTanh (x - k * y4lin);
        else
            u = x - k * fastTanh (y4lin);
        auto onePole = [this] (float in, float& s) {
            const float v = (in - s) * G;
            const float y = v + s;
            s = y + v;
            return y;
        };
        const float y1 = onePole (u, st.l[0]);
        const float y2 = onePole (y1, st.l[1]);
        const float y3 = onePole (y2, st.l[2]);
        const float y4 = onePole (y3, st.l[3]);
        const bool hp = settings.type == kHighpass;
        if (settings.slope24)
            return hp ? u - 4.0f * y1 + 6.0f * y2 - 4.0f * y3 + y4 : y4;
        return hp ? u - 2.0f * y1 + y2 : y2;
    }

    FilterSettings settings;
    int circuit = kClean;
    float g = 0.1f, G = 0.1f, k = 0.0f, k1 = 2.0f, k2 = 0.7654f;
    float wl = 1, wb = 0, wh = 0;
    float bpNorm0 = 1.0f, bpNorm1 = 1.0f;
    float driveGain = 1.0f, makeup = 1.0f, ladderGain = 1.0f;
    Coef a[2] {};
    Chan ch[2];
};

// Magnitude response (dB) of the linearised filter, for the editor's display.
inline float filterResponseDb (const FilterSettings& s, float freq)
{
    using cd = std::complex<double>;
    const int circuit = s.effectiveCircuit ();
    const double res = std::clamp ((double)s.res, 0.0, 1.0);
    const cd sj (0.0, freq / std::max (1.0f, s.cutoff));
    cd h;
    if (circuit == kSMP || circuit == kPRD)
    {
        const double k = std::min (3.95, res * 4.15);
        const cd h1 = 1.0 / (1.0 + sj);
        const cd fb = 1.0 + k * h1 * h1 * h1 * h1;
        const bool hp = s.type == kHighpass;
        const cd one (1.0, 0.0);
        const cd num = s.slope24 ? (hp ? std::pow (one - h1, 4) : h1 * h1 * h1 * h1)
                                 : (hp ? std::pow (one - h1, 2) : h1 * h1);
        h = num / fb * (1.0 + 0.55 * k);
    }
    else
    {
        float wl, wb, wh;
        typeWeights (s.type, s.morph, wl, wb, wh);
        const bool nl = circuit != kClean;
        auto stage = [&] (double k) {
            const cd d = sj * sj + k * sj + 1.0;
            const double kb = std::max (0.02, k);
            return ((double)wl + (double)wb * kb * sj + (double)wh * sj * sj) / d;
        };
        if (!s.slope24)
            h = stage (std::max (0.01, nl ? 1.4142 - 1.5 * res : 1.4142 * (1.0 - 0.99 * res)));
        else
        {
            const double k2 = std::max (0.01, nl ? 0.7654 - 0.83 * res : 0.7654 * (1.0 - 0.985 * res));
            h = stage (1.8478) * stage (k2);
        }
    }
    return (float)(20.0 * std::log10 (std::max (1e-9, std::abs (h))));
}

} // namespace smempler

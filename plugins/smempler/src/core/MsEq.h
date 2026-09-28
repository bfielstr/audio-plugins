// Mid/side EQ after the built-in effects: a high-pass on the side signal (6, 12 or 24 dB per
// octave), so the low end is mono below its cutoff, and the levels of mid and side. Also reports
// the mid and side peaks for the editor. Zero latency.
#pragma once

#include <algorithm>
#include <cmath>

namespace smempler {

class MsEq
{
public:
    enum Slope { k6 = 0, k12, k24 };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reset ();
    }
    void reset ()
    {
        for (auto& s : st)
            s = {};
        hz = 0.0;
        curSlope = -1;
        midG = sideG = -1.0f;
    }

    // In place.
    void process (float* L, float* R, int n, double hpHz, int slope, double sideDb, double midDb)
    {
        update (hpHz, slope);
        const float midT = dbToGain (midDb), sideT = dbToGain (sideDb);
        if (midG < 0.0f)
        {
            midG = midT;
            sideG = sideT;
        }
        const float k = (float)(1.0 - std::exp (-1.0 / (0.01 * sr)));
        const int stages = slope == k24 ? 2 : 1;
        float pm = 0.0f, ps = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            midG += (midT - midG) * k;
            sideG += (sideT - sideG) * k;
            double m = 0.5 * ((double)L[i] + R[i]), s = 0.5 * ((double)L[i] - R[i]);
            for (int j = 0; j < stages; ++j)
                s = st[j].tick (c[j], s);
            m *= midG;
            s *= sideG;
            L[i] = (float)(m + s);
            R[i] = (float)(m - s);
            pm = std::max (pm, (float)std::fabs (m));
            ps = std::max (ps, (float)std::fabs (s));
        }
        midPeak = pm;
        sidePeak = ps;
    }

    // Off: only the levels, for the display.
    void measure (const float* L, const float* R, int n)
    {
        float pm = 0.0f, ps = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            pm = std::max (pm, 0.5f * std::fabs (L[i] + R[i]));
            ps = std::max (ps, 0.5f * std::fabs (L[i] - R[i]));
        }
        midPeak = pm;
        sidePeak = ps;
    }

    float midPeak = 0.0f, sidePeak = 0.0f;

    // The side high-pass's magnitude (dB) at f, for the display.
    static double responseDb (double f, double hpHz, int slope)
    {
        const double x = f / std::max (1.0, hpHz);
        const int order = slope == k6 ? 1 : (slope == k12 ? 2 : 4);
        const double mag = std::pow (x, order) / std::sqrt (1.0 + std::pow (x, 2 * order));
        return 20.0 * std::log10 (std::max (1e-9, mag));
    }

private:
    struct Coeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };
    struct State
    {
        double z1 = 0.0, z2 = 0.0;
        double tick (const Coeffs& c, double x) // transposed direct form II
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };
    static float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }

    Coeffs highPass (double q) const
    {
        const double w = 2.0 * M_PI * hz / sr, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        Coeffs r;
        r.b0 = (1.0 + cw) / 2.0 / a0;
        r.b1 = -(1.0 + cw) / a0;
        r.b2 = r.b0;
        r.a1 = -2.0 * cw / a0;
        r.a2 = (1.0 - alpha) / a0;
        return r;
    }

    void update (double target, int slope)
    {
        target = std::clamp (target, 10.0, 0.45 * sr);
        const double next = hz <= 0.0 ? target : hz * std::pow (target / hz, 0.3); // glide per block
        if (slope == curSlope && std::fabs (next - hz) < 1e-4 * hz)
            return;
        hz = next;
        curSlope = slope;
        if (slope == k6)
        {
            // first order (bilinear): unity at Nyquist, zero at DC
            const double t = std::tan (M_PI * hz / sr), b0 = 1.0 / (1.0 + t);
            c[0] = {b0, -b0, 0.0, (t - 1.0) / (t + 1.0), 0.0};
        }
        else if (slope == k12)
            c[0] = highPass (M_SQRT1_2);
        else
        {
            c[0] = highPass (0.54119610); // Butterworth, 4th order
            c[1] = highPass (1.30656296);
        }
    }

    double sr = 48000.0, hz = 0.0;
    int curSlope = -1;
    Coeffs c[2];
    State st[2];
    float midG = -1.0f, sideG = -1.0f;
};

} // namespace smempler

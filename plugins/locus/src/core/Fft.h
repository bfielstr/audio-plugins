// Small real-input FFT (radix-2, N/2 complex transform + split). Each instance owns its
// scratch memory, so give every voice/analysis job its own instance.
#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace locus {

class Fft
{
public:
    using cf = std::complex<float>;

    explicit Fft (int size = 1024) { init (size); }

    void init (int size)
    {
        n = size;
        m = n / 2;
        bits = 0;
        while ((1 << bits) < m)
            ++bits;
        rev.resize ((size_t)m);
        for (int i = 0; i < m; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev[(size_t)i] = r;
        }
        tw.resize ((size_t)m / 2 + 1);
        for (int i = 0; i <= m / 2; ++i)
        {
            double a = -2.0 * M_PI * i / m;
            tw[(size_t)i] = cf ((float)std::cos (a), (float)std::sin (a));
        }
        split.resize ((size_t)m + 1);
        for (int k = 0; k <= m; ++k)
        {
            double a = -2.0 * M_PI * k / n;
            split[(size_t)k] = cf ((float)std::cos (a), (float)std::sin (a));
        }
        z.resize ((size_t)m);
    }

    int size () const { return n; }
    int bins () const { return m + 1; }

    // in: n real samples. out: n/2 + 1 bins (unnormalized).
    void forward (const float* in, cf* out)
    {
        for (int i = 0; i < m; ++i)
            z[(size_t)rev[(size_t)i]] = cf (in[2 * i], in[2 * i + 1]);
        transform (false);
        const cf z0 = z[0];
        out[0] = cf (z0.real () + z0.imag (), 0.0f);
        out[m] = cf (z0.real () - z0.imag (), 0.0f);
        for (int k = 1; k < m; ++k)
        {
            const cf a = z[(size_t)k];
            const cf b = std::conj (z[(size_t)(m - k)]);
            const cf e = (a + b) * 0.5f;
            const cf o = (a - b) * cf (0.0f, -0.5f);
            out[k] = e + split[(size_t)k] * o;
        }
    }

    // in: n/2 + 1 bins. out: n real samples (normalized so inverse(forward(x)) == x).
    void inverse (const cf* in, float* out)
    {
        for (int k = 0; k < m; ++k)
        {
            const cf a = in[k];
            const cf b = std::conj (in[m - k]);
            const cf e = (a + b) * 0.5f;
            const cf o = (a - b) * 0.5f * std::conj (split[(size_t)k]);
            z[(size_t)rev[(size_t)k]] = e + cf (0.0f, 1.0f) * o;
        }
        transform (true);
        const float s = 1.0f / (float)m;
        for (int i = 0; i < m; ++i)
        {
            out[2 * i] = z[(size_t)i].real () * s;
            out[2 * i + 1] = z[(size_t)i].imag () * s;
        }
    }

private:
    void transform (bool inverseDir)
    {
        for (int len = 2; len <= m; len <<= 1)
        {
            const int half = len >> 1;
            const int step = m / len;
            for (int i = 0; i < m; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    cf w = tw[(size_t)(j * step)];
                    if (inverseDir)
                        w = std::conj (w);
                    const cf u = z[(size_t)(i + j)];
                    const cf v = z[(size_t)(i + j + half)] * w;
                    z[(size_t)(i + j)] = u + v;
                    z[(size_t)(i + j + half)] = u - v;
                }
            }
        }
    }

    int n = 0, m = 0, bits = 0;
    std::vector<int> rev;
    std::vector<cf> tw, split, z;
};

} // namespace locus

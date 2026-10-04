// Small real-input FFT (an N/2-point complex transform + split). Each instance owns its scratch
// memory, so give every voice/analysis job its own instance.
//
// The complex transform works on split real / imaginary arrays (so the compiler can vectorise the
// butterflies), with the first two radix-2 stages merged into one radix-4 pass (their twiddles are
// 1 and -i) and every later stage reading its twiddles from its own contiguous table. It computes the
// same transform as the plain radix-2 one Smemplr used before (kept in the tests as the reference),
// to float rounding.
#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace smemplr {

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
        // the stages from len 8 on: stage len's twiddles exp (-2 pi i j / len), j < len / 2, one after
        // the other (len / 2 - 4 + ... = m - 4 entries in all)
        twr.assign ((size_t)std::max (1, m), 0.0f);
        twi.assign ((size_t)std::max (1, m), 0.0f);
        for (int len = 8, off = 0; len <= m; off += len / 2, len <<= 1)
            for (int j = 0; j < len / 2; ++j)
            {
                const double a = -2.0 * M_PI * j / len;
                twr[(size_t)(off + j)] = (float)std::cos (a);
                twi[(size_t)(off + j)] = (float)std::sin (a);
            }
        splitR.resize ((size_t)m + 1);
        splitI.resize ((size_t)m + 1);
        for (int k = 0; k <= m; ++k)
        {
            const double a = -2.0 * M_PI * k / n;
            splitR[(size_t)k] = (float)std::cos (a);
            splitI[(size_t)k] = (float)std::sin (a);
        }
        zr.assign ((size_t)m, 0.0f);
        zi.assign ((size_t)m, 0.0f);
    }

    int size () const { return n; }
    int bins () const { return m + 1; }

    // in: n real samples. out: n/2 + 1 bins (unnormalized).
    void forward (const float* in, cf* out)
    {
        float* re = zr.data ();
        float* im = zi.data ();
        const int* rv = rev.data ();
        for (int i = 0; i < m; ++i)
        {
            re[rv[i]] = in[2 * i];
            im[rv[i]] = in[2 * i + 1];
        }
        transform (false);
        out[0] = cf (re[0] + im[0], 0.0f);
        out[m] = cf (re[0] - im[0], 0.0f);
        const float* sR = splitR.data ();
        const float* sI = splitI.data ();
        for (int k = 1; k < m; ++k)
        {
            // a = z[k], b = conj (z[m - k]); e = (a + b) / 2, o = (a - b) * (-i / 2); out = e + split[k] * o
            const float ar = re[k], ai = im[k], br = re[m - k], bi = -im[m - k];
            const float er = (ar + br) * 0.5f, ei = (ai + bi) * 0.5f;
            const float or_ = (ai - bi) * 0.5f, oi = -(ar - br) * 0.5f;
            out[k] = cf (er + (sR[k] * or_ - sI[k] * oi), ei + (sR[k] * oi + sI[k] * or_));
        }
    }

    // in: n/2 + 1 bins. out: n real samples (normalized so inverse(forward(x)) == x).
    void inverse (const cf* in, float* out)
    {
        float* re = zr.data ();
        float* im = zi.data ();
        const int* rv = rev.data ();
        const float* sR = splitR.data ();
        const float* sI = splitI.data ();
        for (int k = 0; k < m; ++k)
        {
            // a = in[k], b = conj (in[m - k]); e = (a + b) / 2, o = (a - b) / 2 * conj (split[k]); z = e + i o
            const float ar = in[k].real (), ai = in[k].imag (), br = in[m - k].real (), bi = -in[m - k].imag ();
            const float er = (ar + br) * 0.5f, ei = (ai + bi) * 0.5f;
            const float dr = (ar - br) * 0.5f, di = (ai - bi) * 0.5f;
            const float or_ = dr * sR[k] + di * sI[k], oi = di * sR[k] - dr * sI[k];
            re[rv[k]] = er - oi;
            im[rv[k]] = ei + or_;
        }
        transform (true);
        const float s = 1.0f / (float)m;
        for (int i = 0; i < m; ++i)
        {
            out[2 * i] = re[i] * s;
            out[2 * i + 1] = im[i] * s;
        }
    }

private:
    void transform (bool inverseDir)
    {
        float* __restrict re = zr.data ();
        float* __restrict im = zi.data ();
        if (m < 4)
        {
            // (sizes below 8: plain radix-2)
            for (int len = 2; len <= m; len <<= 1)
                for (int i = 0; i < m; i += len)
                {
                    const float ur = re[i], ui = im[i], vr = re[i + 1], vi = im[i + 1];
                    re[i] = ur + vr;
                    im[i] = ui + vi;
                    re[i + 1] = ur - vr;
                    im[i + 1] = ui - vi;
                }
            return;
        }
        // stages len 2 and 4 together: twiddles 1, and 1 and -i (+i inverse)
        const float sgn = inverseDir ? -1.0f : 1.0f;
        for (int i = 0; i < m; i += 4)
        {
            const float a0r = re[i] + re[i + 1], a0i = im[i] + im[i + 1];
            const float a1r = re[i] - re[i + 1], a1i = im[i] - im[i + 1];
            const float a2r = re[i + 2] + re[i + 3], a2i = im[i + 2] + im[i + 3];
            const float a3r = re[i + 2] - re[i + 3], a3i = im[i + 2] - im[i + 3];
            // v = a3 * w, w = -i forward: (a3i, -a3r); +i inverse: (-a3i, a3r)
            const float vr = sgn * a3i, vi = -sgn * a3r;
            re[i] = a0r + a2r;
            im[i] = a0i + a2i;
            re[i + 2] = a0r - a2r;
            im[i + 2] = a0i - a2i;
            re[i + 1] = a1r + vr;
            im[i + 1] = a1i + vi;
            re[i + 3] = a1r - vr;
            im[i + 3] = a1i - vi;
        }
        const float* __restrict wR = twr.data ();
        const float* __restrict wI = twi.data ();
        for (int len = 8, off = 0; len <= m; off += len / 2, len <<= 1)
        {
            const int half = len >> 1;
            const float* __restrict cr = wR + off;
            const float* __restrict ci = wI + off;
            for (int i = 0; i < m; i += len)
            {
                float* __restrict ar = re + i;
                float* __restrict ai = im + i;
                float* __restrict br = re + i + half;
                float* __restrict bi = im + i + half;
                if (inverseDir)
                    for (int j = 0; j < half; ++j)
                    {
                        // w = conj (c)
                        const float vr = br[j] * cr[j] + bi[j] * ci[j];
                        const float vi = bi[j] * cr[j] - br[j] * ci[j];
                        const float ur = ar[j], ui = ai[j];
                        ar[j] = ur + vr;
                        ai[j] = ui + vi;
                        br[j] = ur - vr;
                        bi[j] = ui - vi;
                    }
                else
                    for (int j = 0; j < half; ++j)
                    {
                        const float vr = br[j] * cr[j] - bi[j] * ci[j];
                        const float vi = br[j] * ci[j] + bi[j] * cr[j];
                        const float ur = ar[j], ui = ai[j];
                        ar[j] = ur + vr;
                        ai[j] = ui + vi;
                        br[j] = ur - vr;
                        bi[j] = ui - vi;
                    }
            }
        }
    }

    int n = 0, m = 0, bits = 0;
    std::vector<int> rev;
    std::vector<float> twr, twi, splitR, splitI, zr, zi;
};

} // namespace smemplr

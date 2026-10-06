#include "Wavetable.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace ciphr {

namespace {

constexpr double kPi = 3.14159265358979323846;

// In-place iterative radix-2 FFT with the + sign in the exponent (an unnormalized inverse transform).
void inverseFft (std::vector<std::complex<double>>& x)
{
    const size_t n = x.size ();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double a = 2.0 * kPi / (double)len;
        const std::complex<double> wl (std::cos (a), std::sin (a));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const std::complex<double> u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

double bump (double h, double centre, double width) { return std::exp (-((h - centre) / width) * ((h - centre) / width)); }

} // namespace

const char* waveName (int wave)
{
    static const char* const names[kNumWaves] = {"Sine", "Triangle", "Saw", "Square", "Pulse", "Organ",
                                                 "Vowel A", "Vowel O", "Hollow", "Buzz", "Soft", "Glass"};
    return wave >= 0 && wave < kNumWaves ? names[wave] : "";
}

double waveHarmonic (int wave, int h)
{
    if (h < 1)
        return 0.0;
    const double hd = (double)h;
    const bool odd = (h & 1) != 0;
    switch (wave)
    {
        case kWaveSine: return h == 1 ? 1.0 : 0.0;
        case kWaveTriangle: return odd ? ((((h - 1) / 2) & 1) ? -1.0 : 1.0) / (hd * hd) : 0.0;
        case kWaveSaw: return 1.0 / hd;
        case kWaveSquare: return odd ? 1.0 / hd : 0.0;
        case kWavePulse: return std::sin (kPi * hd * 0.25) / hd;
        case kWaveOrgan:
            switch (h)
            {
                case 1: return 1.0;
                case 2: return 0.7;
                case 3: return 0.5;
                case 4: return 0.4;
                case 6: return 0.3;
                case 8: return 0.25;
                default: return 0.0;
            }
        case kWaveVowelA: return std::pow (hd, -0.7) * (0.15 + bump (hd, 5.0, 2.0) + 0.7 * bump (hd, 9.0, 2.5));
        case kWaveVowelO: return std::pow (hd, -0.7) * (0.12 + bump (hd, 3.0, 1.3) + 0.6 * bump (hd, 6.0, 1.8));
        case kWaveHollow: return odd ? std::pow (hd, -1.5) : 0.0;
        case kWaveBuzz: return 1.0 / std::sqrt (hd);
        case kWaveSoft: return 1.0 / (hd * hd);
        case kWaveGlass:
            switch (h)
            {
                case 1: return 1.0;
                case 3: return 0.6;
                case 7: return 0.45;
                case 11: return 0.3;
                case 16: return 0.2;
                default: return 0.0;
            }
        default: return 0.0;
    }
}

const WaveBank& WaveBank::get ()
{
    static const WaveBank bank;
    return bank;
}

WaveBank::WaveBank ()
{
    data.assign ((size_t)kNumWaves * kTableLevels * kTableStride, 0.0f);
    std::vector<std::complex<double>> x ((size_t)kTableSize);
    for (int w = 0; w < kNumWaves; ++w)
    {
        double scale = 1.0;
        for (int level = 0; level < kTableLevels; ++level)
        {
            // x[n] = sum a_h sin (2 pi h n / N): X[h] = -i a_h / 2, X[N - h] = +i a_h / 2
            std::fill (x.begin (), x.end (), std::complex<double> (0.0, 0.0));
            const int top = harmonicsAt (level);
            for (int h = 1; h <= top && h < kTableSize / 2; ++h)
            {
                const double a = waveHarmonic (w, h);
                x[(size_t)h] = std::complex<double> (0.0, -0.5 * a);
                x[(size_t)(kTableSize - h)] = std::complex<double> (0.0, 0.5 * a);
            }
            inverseFft (x);
            if (level == 0)
            {
                // every level of a wave by the same factor: its full table peaks at 1
                double peak = 0.0;
                for (const auto& v : x)
                    peak = std::max (peak, std::fabs (v.real ()));
                scale = peak > 0.0 ? 1.0 / peak : 1.0;
            }
            float* t = data.data () + ((size_t)w * kTableLevels + (size_t)level) * kTableStride;
            for (int i = 0; i < kTableSize; ++i)
                t[i] = (float)(x[(size_t)i].real () * scale);
            t[kTableSize] = t[0];
            t[kTableSize + 1] = t[1];
        }
    }
}

} // namespace ciphr

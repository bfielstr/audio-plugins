#include "Wavetable.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>

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

// a formant's peak (a resonance's magnitude): 1 at `centre` Hz, half power `bw` Hz wide
double formant (double hz, double centre, double bw)
{
    const double d = (hz - centre) / (0.5 * bw);
    return 1.0 / std::sqrt (1.0 + d * d);
}

// a fixed pseudo-random number in [0, 1) for a wave's harmonic (integer arithmetic only: the same on every
// machine)
double scatter (int wave, int h, uint64_t salt)
{
    uint64_t z = (uint64_t)(wave * 4096 + h) * 0x9E3779B97F4A7C15ull + salt;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

constexpr double kC2 = 65.40639132514966, kC3 = 130.8127826502993; // the vowels' formants are placed for these notes

} // namespace

const char* waveName (int wave)
{
    static const char* const names[kNumWaves] = {"Sine", "Triangle", "Saw", "Square", "Pulse", "Organ",
                                                 "Vowel A", "Vowel O", "Hollow", "Buzz", "Soft", "Glass",
                                                 "Plate", "Metal", "Scrape", "Screech", "Deep OO", "Deep AA",
                                                 "Deep OH", "Throat", "Choir"};
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
        case kWavePlate:
            switch (h)
            {
                case 1: return 0.3;
                case 2: return 1.0;
                case 5: return 0.8;
                case 8: return 0.6;
                case 10: return 0.55;
                case 13: return 0.45;
                case 17: return 0.35;
                case 20: return 0.3;
                default: return 0.0;
            }
        case kWaveMetal:
        {
            if (h == 1)
                return 0.35;
            static constexpr int centres[5] = {7, 14, 23, 34, 48};
            for (int c : centres)
                if (std::abs (h - c) <= 1)
                    return (h == c ? 1.0 : h < c ? 0.7 : 0.8) * std::pow ((double)c / 7.0, -0.4);
            return 0.0;
        }
        case kWaveScrape:
            if (h == 1)
                return 0.25;
            if (h < 12)
                return 0.08 / hd;
            return h <= 220 ? (0.25 + 0.75 * scatter (wave, h, 1)) * std::pow (hd / 12.0, -0.35) : 0.0;
        case kWaveScreech:
            if (h > 160)
                return 0.0;
            return (h == 1 ? 0.3 : 0.0) + (h < 8 ? 0.12 / hd : 0.0) +
                   std::pow (hd / 30.0, -0.3) * (0.04 + bump (hd, 30.0, 1.6) + 0.5 * bump (hd, 47.0, 2.0));
        case kWaveDeepOO:
        {
            const double f = hd * kC2;
            return std::pow (hd, -0.8) * (0.01 + formant (f, 300.0, 90.0) + 0.3 * formant (f, 870.0, 110.0) + 0.08 * formant (f, 2240.0, 160.0));
        }
        case kWaveDeepAA:
        {
            const double f = hd * kC2;
            return std::pow (hd, -0.8) * (0.01 + formant (f, 730.0, 90.0) + 0.55 * formant (f, 1090.0, 110.0) + 0.15 * formant (f, 2440.0, 160.0));
        }
        case kWaveDeepOH:
        {
            const double f = hd * kC2;
            return std::pow (hd, -0.8) * (0.01 + formant (f, 570.0, 90.0) + 0.5 * formant (f, 840.0, 110.0) + 0.08 * formant (f, 2410.0, 160.0));
        }
        case kWaveThroat:
        {
            const double f = hd * kC2;
            return std::pow (hd, -0.7) * (0.02 + 0.8 * formant (f, 400.0, 120.0) + 0.3 * formant (f, 2600.0, 300.0) + 2.2 * bump (hd, 12.0, 0.45));
        }
        case kWaveChoir:
        {
            const double f = hd * kC3;
            return std::pow (hd, -0.9) * (0.02 + formant (f, 650.0, 110.0) + 0.5 * formant (f, 1080.0, 120.0) + 0.28 * formant (f, 2650.0, 200.0) +
                                          0.25 * formant (f, 3000.0, 250.0));
        }
        default: return 0.0;
    }
}

double wavePhase (int wave, int h)
{
    switch (wave)
    {
        case kWaveMetal:
        case kWaveScrape:
        case kWaveScreech:
        case kWaveChoir: return h > 1 ? 2.0 * kPi * scatter (wave, h, 2) : 0.0;
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
            // x[n] = sum a_h sin (2 pi h n / N + phi_h): X[h] = -i a_h e^(i phi_h) / 2, X[N - h] its conjugate
            std::fill (x.begin (), x.end (), std::complex<double> (0.0, 0.0));
            const int top = harmonicsAt (level);
            for (int h = 1; h <= top && h < kTableSize / 2; ++h)
            {
                const double a = waveHarmonic (w, h), phi = wavePhase (w, h);
                if (phi == 0.0)
                {
                    x[(size_t)h] = std::complex<double> (0.0, -0.5 * a);
                    x[(size_t)(kTableSize - h)] = std::complex<double> (0.0, 0.5 * a);
                }
                else
                {
                    // a sin (wt + phi): X[h] = -i a e^(i phi) / 2, X[N - h] its conjugate
                    x[(size_t)h] = std::complex<double> (0.5 * a * std::sin (phi), -0.5 * a * std::cos (phi));
                    x[(size_t)(kTableSize - h)] = std::conj (x[(size_t)h]);
                }
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

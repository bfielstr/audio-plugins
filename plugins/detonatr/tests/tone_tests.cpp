// Tests for the Tone stage of Detonatr. Run: ./detonatr_tone_tests [filter]
#include "Tone.h"

#include "Harness.h"
#include "smacheratr/src/core/Biquad.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <vector>

using namespace detonatr;

namespace {

using Buf = std::vector<float>;
using cd = std::complex<double>;

double dB (double x) { return 20.0 * std::log10 (std::max (1e-30, x)); }

// Settings for a test: everything off but what the test turns on.
void setup (Tone& t, double sr, double dry, double res, double car, double disp, int maxBlock = 512)
{
    t.prepare (sr, maxBlock);
    t.setRoot (82.4);
    t.setMaterial (kMetalPot);
    t.setDecay (0.8);
    t.setDry (dry);
    t.setResonators (res);
    t.setCarriers (car);
    t.setDisperse (disp);
    t.setDisperseFreq (400.0);
    for (int s = 0; s < kCarrierSlots; ++s)
    {
        t.setCarrierLevel (s, 1.0);
        t.setCarrier (s, nullptr);
    }
}

void run (Tone& t, Buf& L, Buf& R, int block = 256)
{
    for (size_t i = 0; i < L.size (); i += (size_t)block)
    {
        const int n = (int)std::min<size_t> ((size_t)block, L.size () - i);
        t.process (L.data () + i, R.data () + i, n);
    }
}

// A noise-burst impact: gaussian noise under a fast exponential fall (tau), len seconds long.
void addHit (Buf& L, Buf& R, double sr, double at, double amp, uint32_t seed, double tau = 0.02,
             double len = 0.15)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> g (0.0, 1.0);
    const size_t start = (size_t)(at * sr), n = (size_t)(len * sr);
    for (size_t i = 0; i < n && start + i < L.size (); ++i)
    {
        const double e = amp * std::exp (-(double)i / (tau * sr));
        L[start + i] += (float)(e * g (rng));
        R[start + i] += (float)(e * g (rng));
    }
}

double peakAbs (const Buf& x, size_t from = 0, size_t to = SIZE_MAX)
{
    double p = 0.0;
    for (size_t i = from; i < std::min (to, x.size ()); ++i)
        p = std::max (p, (double)std::abs (x[i]));
    return p;
}

double rms (const Buf& x, size_t from, size_t to)
{
    double s = 0.0;
    to = std::min (to, x.size ());
    for (size_t i = from; i < to; ++i)
        s += (double)x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (1, to - from));
}

bool allFinite (const Buf& x)
{
    for (float v : x)
        if (!std::isfinite (v))
            return false;
    return true;
}

void fft (std::vector<cd>& a)
{
    const size_t n = a.size ();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        for (size_t k = 0; k < len / 2; ++k)
        {
            const cd w = std::polar (1.0, -2.0 * M_PI * (double)k / (double)len);
            for (size_t i = 0; i < n; i += len)
            {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// Hann-windowed power spectrum of x[from .. from + N) (N a power of two), bins 0 .. N/2
std::vector<double> spectrum (const Buf& x, size_t from, size_t N)
{
    std::vector<cd> a (N);
    for (size_t i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double)i / (double)N);
        a[i] = from + i < x.size () ? w * x[from + i] : 0.0;
    }
    fft (a);
    std::vector<double> p (N / 2 + 1);
    for (size_t k = 0; k <= N / 2; ++k)
        p[k] = std::norm (a[k]);
    return p;
}

double binHz (size_t N, double sr) { return sr / (double)N; }

// spectral flatness (geometric / arithmetic mean of the power) between lo and hi Hz
double flatness (const std::vector<double>& p, double sr, double lo, double hi)
{
    const size_t N = (p.size () - 1) * 2;
    double lg = 0.0, ar = 0.0;
    int cnt = 0;
    for (size_t k = 0; k < p.size (); ++k)
    {
        const double f = k * binHz (N, sr);
        if (f < lo || f > hi)
            continue;
        lg += std::log (p[k] + 1e-30);
        ar += p[k];
        ++cnt;
    }
    return std::exp (lg / cnt) / (ar / cnt);
}

// share of the energy in the strongest `count` bins
double topShare (const std::vector<double>& p, int count)
{
    std::vector<double> s (p);
    std::sort (s.begin (), s.end (), std::greater<double> ());
    double top = 0.0, all = 0.0;
    for (size_t k = 0; k < s.size (); ++k)
    {
        all += s[k];
        if ((int)k < count)
            top += s[k];
    }
    return top / all;
}

double energyBetween (const std::vector<double>& p, double sr, double lo, double hi)
{
    const size_t N = (p.size () - 1) * 2;
    double e = 0.0;
    for (size_t k = 0; k < p.size (); ++k)
    {
        const double f = k * binHz (N, sr);
        if (f >= lo && f < hi)
            e += p[k];
    }
    return e;
}

// How far (dB) the strongest bin within 2 Hz of f stands above the median of [f / 1.4, f * 1.4].
double prominence (const std::vector<double>& p, double sr, double f)
{
    const size_t N = (p.size () - 1) * 2;
    const double bw = binHz (N, sr);
    double pk = 0.0;
    std::vector<double> around;
    for (size_t k = 1; k < p.size (); ++k)
    {
        const double fk = k * bw;
        if (std::abs (fk - f) <= 2.0)
            pk = std::max (pk, p[k]);
        if (fk >= f / 1.4 && fk <= f * 1.4)
            around.push_back (p[k]);
    }
    std::nth_element (around.begin (), around.begin () + around.size () / 2, around.end ());
    return 10.0 * std::log10 (pk / (around[around.size () / 2] + 1e-30));
}

// the strongest frequency between lo and hi (Hz), with parabolic interpolation between bins
double peakHz (const std::vector<double>& p, double sr, double lo, double hi)
{
    const size_t N = (p.size () - 1) * 2;
    const double bw = binHz (N, sr);
    size_t best = 1;
    for (size_t k = 1; k + 1 < p.size (); ++k)
        if (k * bw >= lo && k * bw <= hi && p[k] > p[best])
            best = k;
    const double a = std::log (p[best - 1] + 1e-30), b = std::log (p[best] + 1e-30),
                 c = std::log (p[best + 1] + 1e-30);
    const double d = 0.5 * (a - c) / (a - 2.0 * b + c);
    return (best + d) * bw;
}

// the power-weighted mean frequency between lo and hi
double centroidHz (const std::vector<double>& p, double sr, double lo, double hi)
{
    const size_t N = (p.size () - 1) * 2;
    double a = 0.0, b = 0.0;
    for (size_t k = 0; k < p.size (); ++k)
    {
        const double f = k * binHz (N, sr);
        if (f >= lo && f <= hi)
        {
            a += f * p[k];
            b += p[k];
        }
    }
    return a / b;
}

// Times (s) from the envelope's peak until it is 20 and 40 dB below it (10 ms RMS frames).
void ringTimes (const Buf& x, double sr, double& t20, double& t40)
{
    const size_t frame = (size_t)(0.01 * sr);
    std::vector<double> env;
    for (size_t i = 0; i + frame <= x.size (); i += frame)
        env.push_back (rms (x, i, i + frame));
    const size_t pk = (size_t)(std::max_element (env.begin (), env.end ()) - env.begin ());
    t20 = t40 = -1.0;
    for (size_t k = pk; k < env.size (); ++k)
    {
        if (t20 < 0.0 && env[k] < env[pk] * 0.1)
            t20 = (k - pk) * 0.01;
        if (t40 < 0.0 && env[k] < env[pk] * 0.01)
            t40 = (k - pk) * 0.01;
    }
}

Carrier makeCarrier (double sr, double seconds, std::vector<double> freqs, bool stereo = false)
{
    Carrier c;
    c.sampleRate = sr;
    c.frames = (int)std::lround (sr * seconds);
    c.ch[0].resize ((size_t)c.frames);
    for (int i = 0; i < c.frames; ++i)
    {
        double v = 0.0;
        for (double f : freqs)
            v += std::sin (2.0 * M_PI * f * i / sr);
        c.ch[0][(size_t)i] = (float)(0.5 * v / freqs.size ());
    }
    if (stereo)
        c.ch[1] = c.ch[0];
    return c;
}

Carrier noiseCarrier (double sr, double seconds, uint32_t seed, bool stereo)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> g (0.0f, 0.2f);
    Carrier c;
    c.sampleRate = sr;
    c.frames = (int)std::lround (sr * seconds);
    for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
    {
        c.ch[ch].resize ((size_t)c.frames);
        for (auto& v : c.ch[ch])
            v = g (rng);
    }
    return c;
}

const char* kMaterialNames[kNumMaterials] = {"glass", "metal pot", "pipe", "wood", "bell", "bottle"};

} // namespace

// ---------------------------------------------------------------------------------------------------

TEST (dry_passes_input_unchanged)
{
    const Carrier car = makeCarrier (48000.0, 1.0, {440.0});
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        Tone t;
        setup (t, sr, 1.0, 0.0, 0.0, 0.0);
        t.setCarrier (0, &car); // loaded, but Carriers is 0
        std::mt19937 rng (7);
        std::normal_distribution<float> g (0.0f, 0.3f);
        Buf L ((size_t)sr), R ((size_t)sr);
        for (size_t i = 0; i < L.size (); ++i)
        {
            L[i] = g (rng);
            R[i] = g (rng);
        }
        Buf l = L, r = R;
        std::uniform_int_distribution<int> blk (1, 512);
        for (size_t i = 0; i < l.size ();)
        {
            const int n = (int)std::min<size_t> ((size_t)blk (rng), l.size () - i);
            t.process (l.data () + i, r.data () + i, n);
            i += (size_t)n;
        }
        size_t diff = 0;
        for (size_t i = 0; i < L.size (); ++i)
            diff += (l[i] != L[i]) + (r[i] != R[i]);
        std::printf ("    %.0f Hz: %zu samples differ\n", sr, diff);
        CHECK (diff == 0, "Dry 1, rest 0 changed %zu samples at %.0f Hz", diff, sr);
    }
}

TEST (resonators_make_it_tonal)
{
    const double sr = 48000.0;
    const size_t N = 65536;
    for (int m = 0; m < kNumMaterials; ++m)
    {
        Tone t;
        setup (t, sr, 0.0, 1.0, 0.0, 0.0);
        t.setMaterial (m);
        Buf L (N + 4800, 0.0f), R (N + 4800, 0.0f);
        addHit (L, R, sr, 0.05, 0.3, 11);
        Buf inL = L;
        run (t, L, R);
        const auto pin = spectrum (inL, 2400, N), pout = spectrum (L, 2400, N);
        const double fin = flatness (pin, sr, 30.0, 16000.0), fout = flatness (pout, sr, 30.0, 16000.0);
        const double all = energyBetween (pout, sr, 0.0, sr / 2);
        const double low = energyBetween (pout, sr, 0.0, 150.0) / all;
        const double high = energyBetween (pout, sr, 8000.0, 20000.0) / all;
        std::printf ("    %-9s flatness in %.3f -> out %.5f (%.0fx); top 20 bins %.0f%%; <150 Hz %.0f%%; "
                     "8-20 kHz %.0f dB\n",
                     kMaterialNames[m], fin, fout, fin / fout, 100.0 * topShare (pout, 20), 100.0 * low,
                     10.0 * std::log10 (high + 1e-30));
        CHECK (fout < 0.01 && fin / fout > 50.0, "%s: flatness %.4f -> %.5f", kMaterialNames[m], fin, fout);
        // wood is short, so its peaks are wider
        const double need = m == kWood ? 15.0 : 30.0;
        CHECK (prominence (pout, sr, 82.4) > need, "%s: no peak at the root (%.1f dB)", kMaterialNames[m],
               prominence (pout, sr, 82.4));
    }

    // the first modes of a pipe and a glass stand out
    struct Case
    {
        int material;
        double ratios[3];
    };
    const Case cases[] = {{kPipe, {1.0, 2.004, 3.012}}, {kGlass, {1.0, 2.828, 5.424}},
                          {kBell, {1.0, 1.94, 2.37}}, {kBottle, {1.0, 6.8, 11.3}}};
    for (const auto& c : cases)
    {
        Tone t;
        setup (t, sr, 0.0, 1.0, 0.0, 0.0);
        t.setMaterial (c.material);
        t.setRoot (110.0);
        Buf L (N + 4800, 0.0f), R (N + 4800, 0.0f);
        addHit (L, R, sr, 0.05, 0.3, 12);
        run (t, L, R);
        const auto p = spectrum (L, 2400, N);
        std::printf ("    %-9s at 110 Hz:", kMaterialNames[c.material]);
        for (double r : c.ratios)
        {
            const double pr = prominence (p, sr, 110.0 * r);
            std::printf ("  %.0f Hz +%.0f dB", 110.0 * r, pr);
            CHECK (pr > 20.0, "%s: mode %.3f (%.0f Hz) only %.1f dB above its surroundings",
                   kMaterialNames[c.material], r, 110.0 * r, pr);
        }
        std::printf ("\n");
    }
}

TEST (resonators_ring_for_decay)
{
    const double sr = 48000.0;
    for (int m = 0; m < kNumMaterials; ++m)
    {
        std::printf ("    %-9s", kMaterialNames[m]);
        for (double decay : {0.2, 0.8, 2.0})
        {
            Tone t;
            setup (t, sr, 0.0, 1.0, 0.0, 0.0);
            t.setMaterial (m);
            t.setDecay (decay);
            Buf L ((size_t)((decay * 1.5 + 0.3) * sr), 0.0f), R = L;
            addHit (L, R, sr, 0.02, 0.3, 21, 0.005, 0.03);
            run (t, L, R);
            double t20, t40;
            ringTimes (L, sr, t20, t40);
            std::printf ("  Decay %.1f: -20 dB %.2f s, -40 dB %.2f s", decay, t20, t40);
            if (m == kPipe || m == kMetalPot || m == kBell)
                CHECK (t40 > 0.4 * decay && t40 < 0.9 * decay,
                       "%s at Decay %.1f: -20 dB after %.2f s, -40 dB after %.2f s", kMaterialNames[m], decay,
                       t20, t40);
        }
        std::printf ("\n");
    }
}

TEST (resonators_follow_hits)
{
    const double sr = 48000.0;
    Tone t;
    setup (t, sr, 0.0, 1.0, 0.0, 0.0);
    t.setDecay (0.3);
    Buf L ((size_t)(2.2 * sr), 0.0f), R = L;
    addHit (L, R, sr, 0.2, 0.3, 31);
    addHit (L, R, sr, 1.2, 0.3 * 0.25, 31); // the same hit, 12 dB quieter
    run (t, L, R);
    const auto at = [sr] (double s) { return (size_t)(s * sr); };
    const double before = peakAbs (L, 0, at (0.2));
    const double p1 = peakAbs (L, at (0.2), at (0.7)), p2 = peakAbs (L, at (1.2), at (1.7));
    const double gap = peakAbs (L, at (1.1), at (1.2));
    std::printf ("    first hit %.1f dBFS, second %.1f dBFS (%.1f dB, the input's is -12 dB), "
                 "just before the second %.1f dB, before the first %.1f dBFS\n",
                 dB (p1), dB (p2), dB (p2 / p1), dB (gap / p1), dB (before));
    CHECK (before == 0.0, "rang before the hit");
    CHECK (std::abs (dB (p2 / p1) + 12.0) < 1.0, "second hit %.1f dB against the first", dB (p2 / p1));
    CHECK (dB (gap / p1) < -40.0, "still %.1f dB between the hits", dB (gap / p1));
}

TEST (resonators_level)
{
    // Resonators 1 against the input (peak to peak): about as loud for a noisy hit, and held to the
    // ceiling (+3 dB) for a hit that sits right on the root.
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        for (int m = 0; m < kNumMaterials; ++m)
        {
            double lv[3];
            for (int kind = 0; kind < 3; ++kind)
            {
                Tone t;
                setup (t, sr, 0.0, 1.0, 0.0, 0.0);
                t.setMaterial (m);
                Buf L ((size_t)(1.5 * sr), 0.0f), R = L;
                if (kind == 0) // white noise hit
                    addHit (L, R, sr, 0.05, 0.3, 41);
                else if (kind == 1) // a dull hit: the same noise low-passed at 300 Hz
                {
                    addHit (L, R, sr, 0.05, 0.3, 41, 0.04, 0.3);
                    const float a = (float)std::exp (-2.0 * M_PI * 300.0 / sr);
                    float zl = 0.0f, zr = 0.0f;
                    for (size_t i = 0; i < L.size (); ++i)
                    {
                        zl = L[i] = zl * a + (1.0f - a) * L[i];
                        zr = R[i] = zr * a + (1.0f - a) * R[i];
                    }
                }
                else // a thump: a falling 70 Hz sine
                    for (size_t i = 0; i < (size_t)(0.4 * sr); ++i)
                        L[i + 2400] = R[i + 2400] =
                            (float)(0.8 * std::sin (2.0 * M_PI * 70.0 * i / sr) * std::exp (-(double)i / (0.08 * sr)));
                const double pin = std::max (peakAbs (L), peakAbs (R));
                run (t, L, R);
                lv[kind] = dB (std::max (peakAbs (L), peakAbs (R)) / pin);
            }
            if (sr == 48000.0 || m == kMetalPot)
                std::printf ("    %.0f Hz %-9s noise hit %+.1f dB, dull hit %+.1f dB, 70 Hz thump %+.1f dB\n", sr,
                             kMaterialNames[m], lv[0], lv[1], lv[2]);
            CHECK (lv[0] > -12.0 && lv[0] < 3.5, "%s: a noise hit comes out at %+.1f dB", kMaterialNames[m], lv[0]);
            CHECK (lv[1] < 3.5 && lv[2] < 3.5, "%s: louder than the ceiling (%+.1f, %+.1f dB)", kMaterialNames[m],
                   lv[1], lv[2]);
        }
    }
}

TEST (vocoder_plays_the_carrier_by_the_input)
{
    const double sr = 48000.0;
    const Carrier sine = makeCarrier (sr, 2.0, {440.0});
    Tone t;
    setup (t, sr, 0.0, 0.0, 1.0, 0.0);
    t.setCarrier (0, &sine);
    // noise bursts: 100 ms on, 400 ms off
    const size_t N = 131072;
    Buf L (N, 0.0f), R (N, 0.0f);
    std::mt19937 rng (51);
    std::normal_distribution<float> g (0.0f, 0.3f);
    for (size_t i = 0; i < N; ++i)
        if (std::fmod (i / sr, 0.5) < 0.1)
        {
            L[i] = g (rng);
            R[i] = g (rng);
        }
    const Buf inL = L;
    run (t, L, R);
    double loud = 0.0, quiet = 0.0, inLoud = 0.0;
    int nl = 0, nq = 0;
    for (double s = 0.0; s + 0.5 <= N / sr; s += 0.5)
    {
        loud += rms (L, (size_t)((s + 0.02) * sr), (size_t)((s + 0.1) * sr));
        inLoud += rms (inL, (size_t)((s + 0.02) * sr), (size_t)((s + 0.1) * sr));
        quiet = std::max (quiet, rms (L, (size_t)((s + 0.3) * sr), (size_t)((s + 0.5) * sr)));
        ++nl;
        ++nq;
    }
    loud /= nl;
    inLoud /= nl;
    const auto p = spectrum (L, 0, N);
    const double share = energyBetween (p, sr, 380.0, 500.0) / energyBetween (p, sr, 0.0, sr / 2);
    // the energy's centre near 440 Hz (the bursts' 2 Hz rhythm puts sidebands either side of it)
    const double pk = centroidHz (p, sr, 420.0, 460.0);
    std::printf ("    in the bursts %.1f dB (input %.1f dB); between them (worst) %.1f dB below; centred on %.2f Hz; "
                 "%.1f%% of the energy within 380..500 Hz\n",
                 dB (loud), dB (inLoud), dB (loud / quiet), pk, 100.0 * share);
    CHECK (std::abs (pk - 440.0) < 1.0, "the peak is at %.2f Hz", pk);
    CHECK (share > 0.9, "only %.1f%% near 440 Hz", 100.0 * share);
    CHECK (dB (loud / quiet) > 40.0, "only %.1f dB quieter between the bursts", dB (loud / quiet));
}

TEST (vocoder_chord)
{
    const double sr = 48000.0;
    const Carrier chord = makeCarrier (sr, 2.0, {220.0, 277.18, 329.63}, true);
    Tone t;
    setup (t, sr, 0.0, 0.0, 1.0, 0.0);
    t.setCarrier (2, &chord);
    const size_t N = 131072;
    Buf L (N, 0.0f), R (N, 0.0f);
    std::mt19937 rng (52);
    std::normal_distribution<float> g (0.0f, 0.3f);
    for (size_t i = 0; i < N; ++i)
        if (std::fmod (i / sr, 0.5) < 0.2)
        {
            L[i] = g (rng);
            R[i] = g (rng);
        }
    run (t, L, R);
    for (Buf* x : {&L, &R})
    {
        const auto p = spectrum (*x, 0, N);
        std::printf ("    %s:", x == &L ? "left " : "right");
        for (double f : {220.0, 277.18, 329.63})
        {
            const double pr = prominence (p, sr, f);
            std::printf ("  %.0f Hz +%.0f dB", f, pr);
            CHECK (pr > 20.0, "the chord's %.0f Hz only %.1f dB up", f, pr);
        }
        const double share = (energyBetween (p, sr, 215.0, 225.0) + energyBetween (p, sr, 272.0, 282.0) +
                              energyBetween (p, sr, 325.0, 335.0)) /
                             energyBetween (p, sr, 0.0, sr / 2);
        std::printf ("  (%.0f%% of the energy within 5 Hz of the notes)\n", 100.0 * share);
        CHECK (share > 0.5, "only %.0f%% of the energy on the chord", 100.0 * share);
    }
}

TEST (vocoder_empty_slots_are_silent)
{
    const double sr = 48000.0;
    const Carrier sine = makeCarrier (sr, 1.0, {440.0});
    Tone t;
    setup (t, sr, 0.0, 0.0, 1.0, 0.0);
    std::mt19937 rng (61);
    std::normal_distribution<float> g (0.0f, 0.3f);
    Buf L (48000), R (48000);
    const auto fill = [&] () {
        for (size_t i = 0; i < L.size (); ++i)
        {
            L[i] = g (rng);
            R[i] = g (rng);
        }
    };
    fill ();
    run (t, L, R);
    CHECK (peakAbs (L) == 0.0 && peakAbs (R) == 0.0, "no carriers, yet %.3g out", peakAbs (L));

    t.setCarrier (1, &sine);
    fill ();
    run (t, L, R);
    const double on = rms (L, 4800, L.size ());
    t.setCarrier (1, nullptr); // removed: it fades out within 10 ms and then adds nothing
    fill ();
    run (t, L, R);
    const double tail = peakAbs (L, 0, 480), after = std::max (peakAbs (L, 960), peakAbs (R, 960));
    std::printf ("    playing %.1f dB; after removal: first 10 ms peak %.1f dB, then %.3g\n", dB (on), dB (tail), after);
    CHECK (on > 0.01, "the carrier did not play (%.3g)", on);
    CHECK (after == 0.0, "a removed carrier still plays (%.3g)", after);

    // a slot at level 0 adds nothing either
    t.setCarrier (1, &sine);
    t.setCarrierLevel (1, 0.0);
    fill ();
    run (t, L, R);
    CHECK (peakAbs (L, 4800) == 0.0, "a slot at level 0 plays (%.3g)", peakAbs (L, 4800));
}

TEST (vocoder_resamples_carriers)
{
    struct Case
    {
        double carrierRate, hostRate;
    };
    for (const auto& c : {Case {44100.0, 48000.0}, Case {44100.0, 96000.0}, Case {96000.0, 44100.0},
                          Case {22050.0, 192000.0}})
    {
        const Carrier sine = makeCarrier (c.carrierRate, 1.0, {440.0});
        Tone t;
        setup (t, c.hostRate, 0.0, 0.0, 1.0, 0.0);
        t.setCarrier (3, &sine);
        size_t N = 1;
        while (N < (size_t)(2.5 * c.hostRate))
            N <<= 1;
        Buf L (N), R (N);
        std::mt19937 rng (71);
        std::normal_distribution<float> g (0.0f, 0.3f);
        for (size_t i = 0; i < N; ++i)
        {
            L[i] = g (rng);
            R[i] = g (rng);
        }
        run (t, L, R, 333);
        const auto p = spectrum (L, 0, N);
        const double pk = peakHz (p, c.hostRate, 20.0, 20000.0);
        std::printf ("    a 440 Hz carrier at %.0f Hz, host %.0f Hz: peak %.2f Hz\n", c.carrierRate, c.hostRate, pk);
        CHECK (std::abs (pk - 440.0) < 1.0, "plays at %.2f Hz", pk);
    }
}

TEST (vocoder_level)
{
    // a flat (noise) carrier vocoded by steady noise comes out near the input's level
    const double sr = 48000.0;
    const Carrier noise = noiseCarrier (sr, 3.0, 81, true);
    for (int colour = 0; colour < 2; ++colour)
    {
        Tone t;
        setup (t, sr, 0.0, 0.0, 1.0, 0.0);
        t.setCarrier (0, &noise);
        Buf L ((size_t)(2 * sr)), R = L;
        std::mt19937 rng (82);
        std::normal_distribution<float> g (0.0f, 0.3f);
        float zl = 0.0f, zr = 0.0f;
        const float a = (float)std::exp (-2.0 * M_PI * 500.0 / sr);
        for (size_t i = 0; i < L.size (); ++i)
        {
            L[i] = g (rng);
            R[i] = g (rng);
            if (colour == 1) // a dull input
            {
                zl = L[i] = zl * a + (1.0f - a) * L[i];
                zr = R[i] = zr * a + (1.0f - a) * R[i];
            }
        }
        const double in = rms (L, 4800, L.size ());
        run (t, L, R);
        const double out = rms (L, 4800, L.size ());
        std::printf ("    %s noise in, noise carrier: out %+.1f dB against the input\n", colour ? "dull " : "white",
                     dB (out / in));
        CHECK (std::abs (dB (out / in)) < 6.0, "vocoded level %+.1f dB", dB (out / in));
    }
}

TEST (disperser_is_allpass)
{
    const double sr = 48000.0;
    for (double amount : {0.3, 1.0})
        for (double f : {80.0, 400.0, 3000.0})
        {
            Tone t;
            setup (t, sr, 1.0, 0.0, 0.0, amount);
            t.setDisperseFreq (f);
            Buf L ((size_t)(6 * sr), 0.0f), R = L;
            std::mt19937 rng (91);
            std::normal_distribution<float> g (0.0f, 0.3f);
            for (size_t i = 0; i < (size_t)(4 * sr); ++i)
            {
                L[i] = g (rng);
                R[i] = g (rng);
            }
            const double in = rms (L, 0, L.size ());
            run (t, L, R);
            const double out = rms (L, 0, L.size ());

            // and the impulse response holds exactly the impulse's energy
            Buf h ((size_t)(3 * sr), 0.0f), hr = h;
            Tone u;
            setup (u, sr, 1.0, 0.0, 0.0, amount);
            u.setDisperseFreq (f);
            h[0] = 1.0f;
            run (u, h, hr);
            double e = 0.0;
            for (float v : h)
                e += (double)v * v;
            std::printf ("    Disperse %.1f at %4.0f Hz: noise %+.3f dB, impulse energy %.5f\n", amount, f,
                         dB (out / in), e);
            CHECK (std::abs (dB (out / in)) < 0.5, "noise energy moved %+.3f dB", dB (out / in));
            CHECK (std::abs (e - 1.0) < 0.01, "impulse energy %.5f", e);
        }
}

TEST (disperser_spreads_an_impulse)
{
    const double sr = 48000.0;
    // group delay at f: Re (DFT (n h) / DFT (h))
    const auto groupDelay = [sr] (const Buf& h, double f) {
        cd a = 0.0, b = 0.0;
        for (size_t n = 0; n < h.size (); ++n)
        {
            const cd e = std::polar (1.0, -2.0 * M_PI * f * n / sr);
            a += (double)n * h[n] * e;
            b += (double)h[n] * e;
        }
        return (a / b).real () / sr;
    };
    double gdAt[2] = {};
    int idx = 0;
    for (double amount : {0.3, 1.0})
    {
        Tone t;
        setup (t, sr, 1.0, 0.0, 0.0, amount);
        Buf h ((size_t)sr, 0.0f), r = h;
        h[0] = 1.0f;
        run (t, h, r);
        // The part of the impulse around 400 Hz (an octave-wide band-pass) arrives late and spread out:
        // its energy's centre and 5 .. 95 % span, against a plain impulse through the same band-pass.
        const auto arrival = [sr] (const Buf& x, double& centre, double& span) {
            smacheratr::Biquad bp;
            bp.c = smacheratr::bandPass (sr, 400.0, 1.4);
            std::vector<double> e (x.size ());
            double total = 0.0, moment = 0.0;
            for (size_t n = 0; n < x.size (); ++n)
            {
                const double y = bp.process (x[n]);
                e[n] = y * y;
                total += e[n];
                moment += n * e[n];
            }
            double cum = 0.0, t5 = -1.0, t95 = -1.0;
            for (size_t n = 0; n < x.size (); ++n)
            {
                cum += e[n];
                if (t5 < 0.0 && cum > 0.05 * total)
                    t5 = n / sr;
                if (t95 < 0.0 && cum > 0.95 * total)
                    t95 = n / sr;
            }
            centre = moment / total / sr;
            span = t95 - t5;
        };
        Buf delta (h.size (), 0.0f);
        delta[0] = 1.0f;
        double c0, s0, c1, s1;
        arrival (delta, c0, s0);
        arrival (h, c1, s1);
        const double g100 = groupDelay (h, 100.0), g400 = groupDelay (h, 400.0), g1600 = groupDelay (h, 1600.0),
                     g6k = groupDelay (h, 6400.0);
        std::printf ("    Disperse %.1f at 400 Hz: group delay 100 Hz %.1f ms, 400 Hz %.1f ms, 1.6 kHz %.1f ms, "
                     "6.4 kHz %.2f ms; the 400 Hz octave arrives %.1f ms late, spread over %.1f ms (%.1f ms "
                     "undispersed)\n",
                     amount, 1e3 * g100, 1e3 * g400, 1e3 * g1600, 1e3 * g6k, 1e3 * (c1 - c0), 1e3 * s1, 1e3 * s0);
        CHECK (g400 > 4.0 * g6k && g400 > 2.0 * g1600, "no chirp around 400 Hz (%.1f ms, %.1f ms)", 1e3 * g400,
               1e3 * g1600);
        CHECK (c1 - c0 > 0.005 && s1 > 1.5 * s0, "the 400 Hz band is not delayed and spread (%.1f ms, %.1f ms)",
               1e3 * (c1 - c0), 1e3 * s1);
        gdAt[idx++] = g400;
    }
    CHECK (gdAt[1] > 4.0 * gdAt[0], "Disperse 1 is not much more than 0.3 (%.1f against %.1f ms)", 1e3 * gdAt[1],
           1e3 * gdAt[0]);
}

TEST (disperser_off_is_transparent)
{
    // after it has been on, Disperse 0 returns to passing the signal bit for bit (after a 15 ms fade)
    const double sr = 48000.0;
    Tone t;
    setup (t, sr, 1.0, 0.0, 0.0, 1.0);
    std::mt19937 rng (101);
    std::normal_distribution<float> g (0.0f, 0.3f);
    Buf L ((size_t)sr), R = L;
    for (size_t i = 0; i < L.size (); ++i)
    {
        L[i] = g (rng);
        R[i] = g (rng);
    }
    Buf l = L, r = R;
    const size_t half = L.size () / 2;
    for (size_t i = 0; i < half; i += 128)
        t.process (l.data () + i, r.data () + i, 128);
    t.setDisperse (0.0);
    for (size_t i = half; i < L.size (); i += 128)
        t.process (l.data () + i, r.data () + i, (int)std::min<size_t> (128, L.size () - i));
    size_t diff = 0, firstExact = 0;
    for (size_t i = half; i < L.size (); ++i)
        if (l[i] != L[i] || r[i] != R[i])
        {
            ++diff;
            firstExact = i + 1;
        }
    std::printf ("    exact again %.1f ms after Disperse went to 0\n", 1e3 * (firstExact - half) / sr);
    CHECK (firstExact - half < (size_t)(0.03 * sr), "still changed %.1f ms after Disperse 0",
           1e3 * (firstExact - half) / sr);
}

TEST (defaults_level)
{
    // the plug-in's defaults on a noise hit: level and finiteness, for the record
    for (double sr : {44100.0, 48000.0, 192000.0})
    {
        const Carrier c1 = makeCarrier (44100.0, 1.3, {164.8, 247.0, 1320.0}, true);
        Tone t;
        t.prepare (sr, 512);
        t.setCarrier (0, &c1);
        Buf L ((size_t)(2 * sr), 0.0f), R = L;
        addHit (L, R, sr, 0.1, 0.3, 111);
        addHit (L, R, sr, 1.0, 0.3, 112);
        const double pin = peakAbs (L);
        size_t N = 1;
        while (N * 2 <= L.size ())
            N <<= 1;
        const double fin = flatness (spectrum (L, 0, N), sr, 30.0, 16000.0);
        run (t, L, R);
        const double fout = flatness (spectrum (L, 0, N), sr, 30.0, 16000.0);
        std::printf ("    %.0f Hz defaults: peak out %+.1f dB against the input; flatness %.3f -> %.4f\n", sr,
                     dB (peakAbs (L) / pin), fin, fout);
        CHECK (allFinite (L) && allFinite (R), "not finite");
        CHECK (peakAbs (L) < 4.0 * pin, "defaults %+.1f dB", dB (peakAbs (L) / pin));
    }
}

TEST (fuzz_stays_finite)
{
    std::mt19937 rng (121);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    std::normal_distribution<float> g (0.0f, 1.0f);
    std::vector<Carrier> pool;
    pool.push_back (makeCarrier (48000.0, 0.5, {440.0}));
    pool.push_back (makeCarrier (8000.0, 0.3, {200.0, 310.0}, true));
    pool.push_back (noiseCarrier (192000.0, 0.2, 5, true));
    pool.push_back (noiseCarrier (44100.0, 1.0, 6, false));
    {
        Carrier one; // a single frame
        one.frames = 1;
        one.ch[0] = {0.7f};
        pool.push_back (one);
        Carrier three;
        three.frames = 3;
        three.sampleRate = 96000.0;
        three.ch[0] = {1.0f, -1.0f, 0.5f};
        three.ch[1] = {0.0f, 1.0f, 0.0f};
        pool.push_back (three);
        Carrier none; // no frames at all
        pool.push_back (none);
        Carrier lying; // says more frames than it has
        lying.frames = 100000;
        lying.ch[0].assign (100, 0.3f);
        lying.ch[1].assign (10, 0.3f);
        pool.push_back (lying);
        Carrier loud = makeCarrier (48000.0, 0.25, {3000.0});
        for (auto& v : loud.ch[0])
            v *= 50.0f;
        pool.push_back (loud);
    }
    double worst = 0.0;
    bool finite = true;
    for (double sr : {44100.0, 96000.0, 192000.0})
    {
        const int maxBlock = 997;
        Tone t;
        t.prepare (sr, maxBlock);
        Buf L ((size_t)maxBlock), R ((size_t)maxBlock);
        const int blocks = (int)(20.0 * sr / 500.0);
        int mode = 0;
        double phase = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            if (u (rng) < 0.05)
            {
                t.setRoot (10.0 + u (rng) * 600.0);
                t.setMaterial ((int)(u (rng) * 8) - 1);
                t.setDecay (u (rng) < 0.2 ? 4.0 : u (rng) * 4.5);
                t.setResonators (u (rng) * 1.2);
                t.setCarriers (u (rng) * 1.2);
                t.setDry (u (rng));
                t.setDisperse (u (rng) < 0.2 ? 0.0 : u (rng) * 1.1);
                t.setDisperseFreq (u (rng) * 8000.0);
                t.setCarrierLevel ((int)(u (rng) * 6) - 1, u (rng));
                mode = (int)(u (rng) * 5);
            }
            if (u (rng) < 0.03)
            {
                const int k = (int)(u (rng) * (pool.size () + 2));
                t.setCarrier ((int)(u (rng) * 5), k < (int)pool.size () ? &pool[(size_t)k] : nullptr);
            }
            if (u (rng) < 0.001)
                t.reset ();
            const int n = 1 + (int)(u (rng) * maxBlock) / (u (rng) < 0.5 ? 1 : 16);
            for (int i = 0; i < n; ++i)
            {
                float x = 0.0f;
                switch (mode)
                {
                    case 0: x = 0.0f; break;
                    case 1: x = 4.0f * g (rng); break;
                    case 2: // a full-scale square wave on the default root
                        phase += 82.4 / sr;
                        x = phase - std::floor (phase) < 0.5 ? 1.0f : -1.0f;
                        break;
                    case 3: x = 1.0f; break;
                    default: x = u (rng) < 0.001 ? 8.0f : 0.1f * g (rng); break;
                }
                L[(size_t)i] = x;
                R[(size_t)i] = mode == 3 ? -x : x * 0.5f;
            }
            t.process (L.data (), R.data (), n);
            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (L[(size_t)i]) && std::isfinite (R[(size_t)i]);
                worst = std::max (worst, (double)std::max (std::abs (L[(size_t)i]), std::abs (R[(size_t)i])));
            }
        }
    }
    std::printf ("    largest output %.1f (inputs up to about 16)\n", worst);
    CHECK (finite, "not finite");
    CHECK (worst < 500.0, "output reached %.1f", worst);
}

TEST (silence_after_a_hit)
{
    // everything on, a loud hit, then a minute of silence: it dies away and stays finite
    const double sr = 48000.0;
    const Carrier c = noiseCarrier (sr, 1.0, 9, true);
    Tone t;
    setup (t, sr, 0.7, 1.0, 1.0, 1.0);
    t.setDecay (4.0);
    t.setMaterial (kGlass);
    t.setCarrier (0, &c);
    Buf L ((size_t)(60 * sr), 0.0f), R = L;
    addHit (L, R, sr, 0.1, 1.0, 131);
    const auto t0 = std::chrono::steady_clock::now ();
    run (t, L, R, 512);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    const double tail = std::max (peakAbs (L, (size_t)(55 * sr)), peakAbs (R, (size_t)(55 * sr)));
    std::printf ("    last 5 s peak %.3g; a minute took %.2f s\n", tail, secs);
    CHECK (allFinite (L) && allFinite (R), "not finite");
    CHECK (tail < 1e-6, "still %.3g after a minute", tail);
}

TEST (cpu)
{
    const double sr = 48000.0;
    std::vector<Carrier> cs;
    cs.push_back (noiseCarrier (44100.0, 3.0, 1, true));
    cs.push_back (makeCarrier (48000.0, 2.0, {220.0, 330.0}, true));
    cs.push_back (noiseCarrier (96000.0, 1.5, 2, true));
    cs.push_back (makeCarrier (22050.0, 1.0, {1000.0}, true));
    Tone t;
    t.prepare (sr, 256);
    t.setResonators (1.0);
    t.setCarriers (1.0);
    t.setDry (0.7);
    t.setDisperse (1.0);
    t.setDecay (4.0);
    for (int s = 0; s < kCarrierSlots; ++s)
        t.setCarrier (s, &cs[(size_t)s]);
    const double seconds = 20.0;
    Buf L ((size_t)(seconds * sr), 0.0f), R = L;
    for (double at = 0.1; at < seconds; at += 0.5)
        addHit (L, R, sr, at, 0.3, (uint32_t)(at * 10));
    const auto t0 = std::chrono::steady_clock::now ();
    run (t, L, R, 256);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    std::printf ("    4 stereo carriers, everything on, 48 kHz stereo: %.2f%% of real time\n", 100.0 * secs / seconds);
    CHECK (secs / seconds < 0.1, "%.1f%% of real time", 100.0 * secs / seconds);
    CHECK (allFinite (L), "not finite");
}

DETONATR_TEST_MAIN

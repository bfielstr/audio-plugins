// Tests for the Clean stage of Detonatr. Run: ./detonatr_clean_tests [filter]
//
// Levels are read off long Hann-windowed spectra (Smemplr's FFT) of the input and the output over
// the same stretch of time, so the numbers printed are "what the stage did" per region.
#include "Clean.h"

#include "Harness.h"

#include "smemplr/src/core/Fft.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

using namespace detonatr;

namespace {
constexpr double kPi = 3.14159265358979323846;

using Buf = std::vector<float>;

struct Stereo
{
    Buf l, r;
};

// runs the whole signal through in blocks (random sizes from 1 to maxBlock if block <= 0)
Stereo run (Clean& c, const Stereo& in, int block, int maxBlock = 512, unsigned seed = 1)
{
    Stereo o = in;
    std::mt19937 rng (seed);
    std::uniform_int_distribution<int> sz (1, maxBlock);
    for (int i = 0; i < (int)o.l.size ();)
    {
        const int n = std::min (block > 0 ? block : sz (rng), (int)o.l.size () - i);
        c.process (o.l.data () + i, o.r.data () + i, n);
        i += n;
    }
    return o;
}

double db (double powerRatio) { return 10.0 * std::log10 (std::max (powerRatio, 1e-30)); }

// Power spectrum (both channels added) of x[start, start + n), n a power of two, Hann windowed.
std::vector<double> spectrum (const Stereo& x, int start, int n)
{
    smemplr::Fft fft (n);
    std::vector<double> p ((size_t)fft.bins (), 0.0);
    std::vector<float> w ((size_t)n);
    std::vector<smemplr::Fft::cf> X ((size_t)fft.bins ());
    for (const Buf* ch : {&x.l, &x.r})
    {
        for (int i = 0; i < n; ++i)
            w[(size_t)i] = (*ch)[(size_t)(start + i)] * (float)(0.5 - 0.5 * std::cos (2.0 * kPi * i / n));
        fft.forward (w.data (), X.data ());
        for (size_t k = 0; k < p.size (); ++k)
            p[k] += std::norm (X[k]);
    }
    return p;
}

// sum of the spectrum over [f0, f1] Hz
double bandPower (const std::vector<double>& p, double sr, int n, double f0, double f1)
{
    double s = 0.0;
    const int k0 = std::max (0, (int)std::ceil (f0 * n / sr));
    const int k1 = std::min ((int)p.size () - 1, (int)std::floor (f1 * n / sr));
    for (int k = k0; k <= k1; ++k)
        s += p[(size_t)k];
    return s;
}

// mean power per bin over [f0, f1] Hz
double bandMean (const std::vector<double>& p, double sr, int n, double f0, double f1)
{
    const int k0 = (int)std::ceil (f0 * n / sr), k1 = (int)std::floor (f1 * n / sr);
    return bandPower (p, sr, n, f0, f1) / std::max (1, k1 - k0 + 1);
}

double energy (const Buf& x, int a, int b)
{
    double s = 0.0;
    for (int i = std::max (0, a); i < std::min ((int)x.size (), b); ++i)
        s += (double)x[(size_t)i] * x[(size_t)i];
    return s;
}

double peak (const Buf& x, int a, int b)
{
    double m = 0.0;
    for (int i = std::max (0, a); i < std::min ((int)x.size (), b); ++i)
        m = std::max (m, (double)std::fabs (x[(size_t)i]));
    return m;
}

void addNoise (Stereo& x, double rms, unsigned seed, int from = 0, int to = -1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> nd (0.0, rms);
    if (to < 0)
        to = (int)x.l.size ();
    for (int i = from; i < to; ++i)
    {
        x.l[(size_t)i] += (float)nd (rng);
        x.r[(size_t)i] += (float)nd (rng);
    }
}

// A synthetic impact (the partials of the other stages' tests plus a few higher ones), 1 ms attack,
// amplitude decay time tau, into both channels from `start`.
void addHit (Stereo& x, double sr, int start, double amp, double tau)
{
    static const double f[] = {82.0, 165.0, 249.0, 334.0, 507.0, 1000.0, 2300.0};
    static const double a[] = {0.45, 0.25, 0.14, 0.09, 0.07, 0.1, 0.05};
    for (int i = start; i < (int)x.l.size (); ++i)
    {
        const double t = (i - start) / sr;
        const double env = amp * std::min (1.0, t / 0.001) * std::exp (-t / tau);
        if (env < 1e-7 && t > 0.01)
            break;
        double s = 0.0;
        for (int k = 0; k < 7; ++k)
            s += a[k] * std::sin (2.0 * kPi * f[k] * t);
        x.l[(size_t)i] += (float)(env * s);
        x.r[(size_t)i] += (float)(env * s);
    }
}

Stereo silence (int n) { return {Buf ((size_t)n, 0.0f), Buf ((size_t)n, 0.0f)}; }

bool allFinite (const Stereo& x)
{
    for (const Buf* ch : {&x.l, &x.r})
        for (float v : *ch)
            if (!std::isfinite (v))
                return false;
    return true;
}
} // namespace

// Both amounts at 0: the output is the input delayed by latency (), and latency () never moves.
TEST (clean_passthrough_and_latency)
{
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        Clean c;
        c.prepare (sr, 512);
        const int lat = c.latency ();
        for (double a : {0.3, 1.0})
            for (double b : {0.0, 0.7})
            {
                c.setDenoise (a);
                c.setDereverb (b);
                CHECK (c.latency () == lat, "latency moved with the settings at %.0f Hz", sr);
            }
        c.setDenoise (0.0);
        c.setDereverb (0.0);
        c.reset ();

        const int n = (int)(sr * 1.5);
        Stereo in = silence (n);
        addNoise (in, 0.2, 7);
        addHit (in, sr, n / 3, 0.8, 0.1);
        const Stereo out = run (c, in, 0, 512, 3);
        double err = 0.0, pre = 0.0;
        for (int i = 0; i < n; ++i)
        {
            if (i < lat)
                pre = std::max (pre, (double)std::max (std::fabs (out.l[(size_t)i]), std::fabs (out.r[(size_t)i])));
            else
                err = std::max (err, (double)std::max (std::fabs (out.l[(size_t)i] - in.l[(size_t)(i - lat)]),
                                                       std::fabs (out.r[(size_t)i] - in.r[(size_t)(i - lat)])));
        }
        std::printf ("    %6.0f Hz: latency %d samples (%.1f ms), max |out - delayed in| %.2e, before it %.2e\n", sr,
                     lat, 1000.0 * lat / sr, err, pre);
        CHECK (err < 1e-6, "not a clean delay at %.0f Hz: %g", sr, err);
        CHECK (pre < 1e-6, "output before the latency at %.0f Hz: %g", sr, pre);
    }
    Clean c;
    c.prepare (48000.0, 256);
    CHECK (c.latency () == 2047, "latency at 48 kHz: %d", c.latency ());
}

// A steady tone (82 Hz and 1 kHz) in white noise 30 dB under it: Denoise 1 takes the noise down by
// well over 10 dB and leaves both partials within 1 dB.
TEST (clean_denoise_tone_in_noise)
{
    for (double sr : {48000.0, 96000.0, 44100.0})
    {
        const int n = (int)(sr * 6.0);
        Stereo in = silence (n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const float s = (float)(0.25 * std::sin (2.0 * kPi * 82.0 * t) + 0.25 * std::sin (2.0 * kPi * 1000.0 * t + 1.0));
            in.l[(size_t)i] = in.r[(size_t)i] = s;
        }
        const Stereo tone = in;
        addNoise (in, 0.25 * std::pow (10.0, -30.0 / 20.0), 11); // tone RMS 0.25, noise 30 dB under

        Clean c;
        c.prepare (sr, 512);
        c.setDenoise (1.0);
        const Stereo out = run (c, in, 0, 512, 5);

        const int fn = sr > 60000.0 ? 131072 : 65536, lat = c.latency ();
        const int start = n - fn - 1000; // the last ~1.4 s: long settled
        const auto pIn = spectrum (in, start - lat, fn), pOut = spectrum (out, start, fn), pTone = spectrum (tone, start - lat, fn);
        const double p82 = db (bandPower (pOut, sr, fn, 76, 88) / bandPower (pTone, sr, fn, 76, 88));
        const double p1k = db (bandPower (pOut, sr, fn, 994, 1006) / bandPower (pTone, sr, fn, 994, 1006));
        const double nLow = db (bandMean (pOut, sr, fn, 200, 900) / bandMean (pIn, sr, fn, 200, 900));
        const double nMid = db (bandMean (pOut, sr, fn, 1100, 1900) / bandMean (pIn, sr, fn, 1100, 1900));
        const double nHigh = db (bandMean (pOut, sr, fn, 2000, 20000) / bandMean (pIn, sr, fn, 2000, 20000));
        std::printf ("    %6.0f Hz: partials 82 Hz %+.2f dB, 1 kHz %+.2f dB; noise 200-900 Hz %+.1f dB, 1.1-1.9 kHz %+.1f dB, 2-20 kHz %+.1f dB\n",
                     sr, p82, p1k, nLow, nMid, nHigh);
        CHECK (std::fabs (p82) < 1.0 && std::fabs (p1k) < 1.0, "a partial moved: %+.2f / %+.2f dB", p82, p1k);
        CHECK (nLow < -15.0 && nMid < -15.0 && nHigh < -15.0, "noise not down enough: %+.1f / %+.1f / %+.1f dB", nLow, nMid, nHigh);
        CHECK (allFinite (out), "not finite");
    }
}

// Harder tonal material: a buzzy 82 Hz tone (20 harmonics, 1/k), which leaves little room between
// its partials. Sustained in noise from the very start (so the floor has nothing else to learn from),
// and a slowly decaying version (T60 4 s) coming out of digital silence, whose harmonics fade into
// the noise during the measured stretch: those still clear of it must stay within 2 dB.
TEST (clean_denoise_dense_harmonics)
{
    const double sr = 48000.0;
    const int n = (int)(sr * 6.0), fn = 65536;
    for (int decaying = 0; decaying < 2; ++decaying)
    {
        const int t0 = decaying ? (int)(sr * 1.0) : 0;
        Stereo tone = silence (n);
        for (int i = t0; i < n; ++i)
        {
            const double t = (i - t0) / sr;
            const double env = decaying ? std::pow (10.0, -3.0 * t / 4.0) : 1.0;
            double s = 0.0;
            for (int h = 1; h <= 20; ++h)
                s += std::sin (2.0 * kPi * 82.0 * h * t + h) / h;
            tone.l[(size_t)i] = tone.r[(size_t)i] = (float)(0.2 * env * s);
        }
        Stereo in = tone;
        addNoise (in, 0.2 * std::pow (10.0, -30.0 / 20.0), 17, t0); // ~30 dB under the tone's start
        Stereo noise = silence (n);
        addNoise (noise, 0.2 * std::pow (10.0, -30.0 / 20.0), 17, t0);

        Clean c;
        c.prepare (sr, 512);
        c.setDenoise (1.0);
        const Stereo out = run (c, in, 0, 512, 5);
        const int lat = c.latency ();
        // the second half of the run
        const int start = decaying ? t0 + (int)(1.5 * sr) : n - fn - 3000;
        const auto pOut = spectrum (out, start + lat, fn), pTone = spectrum (tone, start, fn), pIn = spectrum (in, start, fn);
        const auto pNoise = spectrum (noise, start, fn);
        double worst = 0.0, sumOut = 0.0, sumTone = 0.0;
        int clear = 0;
        for (int h = 1; h <= 20; ++h)
        {
            const double o = bandPower (pOut, sr, fn, 82.0 * h - 6, 82.0 * h + 6), t = bandPower (pTone, sr, fn, 82.0 * h - 6, 82.0 * h + 6);
            sumOut += o;
            sumTone += t;
            // "the worst": among the harmonics at least 10 dB over the noise in a 12 Hz band (about 5 dB
            // over it in one of the Clean stage's own ~35 Hz wide bins, on average over the stretch;
            // the decaying one's top harmonics have sunk into the noise by then)
            if (t > 10.0 * bandPower (pNoise, sr, fn, 82.0 * h - 6, 82.0 * h + 6))
            {
                ++clear;
                if (std::fabs (db (o / t)) > std::fabs (worst))
                    worst = db (o / t);
            }
        }
        const double nHigh = db (bandMean (pOut, sr, fn, 3000, 20000) / bandMean (pIn, sr, fn, 3000, 20000));
        std::printf ("    %s: harmonics together %+.2f dB, the worst of the %d clear of the noise %+.2f dB; noise 3-20 kHz %+.1f dB\n",
                     decaying ? "decaying (T60 4 s) after silence, 1.5-2.9 s in" : "sustained, from the start", db (sumOut / sumTone),
                     clear, worst, nHigh);
        CHECK (std::fabs (db (sumOut / sumTone)) < 1.0, "the harmonics lost %+.2f dB", db (sumOut / sumTone));
        CHECK (std::fabs (worst) < 2.0, "a harmonic lost %+.2f dB", worst);
        CHECK (nHigh < -15.0, "noise %+.1f dB", nHigh);
    }
}

// "Musical noise": what is left of plain noise should still be noise, not a sprinkle of bins that
// flicker through (they would stand out far above the rest). Looked at through an independent
// short-time spectrum (1024-point frames): how far the loudest 0.1 % of time-frequency cells stand
// above the median, for the input and the Denoise 1 output.
TEST (clean_denoise_no_musical_noise)
{
    const double sr = 48000.0;
    const int n = (int)(sr * 8.0), fn = 1024;
    Stereo in = silence (n);
    addNoise (in, 0.01, 61);
    const auto spread = [&] (const Stereo& x, int from) {
        std::vector<double> cells;
        for (int s = from; s + fn <= from + (int)(4.0 * sr); s += fn / 2)
        {
            const auto p = spectrum (x, s, fn);
            for (int k = 10; k < fn / 2 - 10; ++k)
                cells.push_back (p[(size_t)k]);
        }
        std::sort (cells.begin (), cells.end ());
        return db (cells[cells.size () * 999 / 1000] / cells[cells.size () / 2]);
    };
    const int from = n - (int)(4.0 * sr) - fn;
    for (double amount : {0.3, 0.6, 1.0, -0.5, -1.0}) // negative: Dereverb (steady noise looks like a tail)
    {
        Clean c;
        c.prepare (sr, 512);
        c.setDenoise (std::max (0.0, amount));
        c.setDereverb (std::max (0.0, -amount));
        const Stereo out = run (c, in, 0, 512, 23);
        const double sIn = spread (in, from - c.latency ()), sOut = spread (out, from);
        const double level = db ((energy (out.l, from, n) + energy (out.r, from, n))
                                 / (energy (in.l, from - c.latency (), n - c.latency ()) + energy (in.r, from - c.latency (), n - c.latency ())));
        std::printf ("    %s %.1f: noise %+.1f dB; the loudest 0.1 %% of cells over the median: input %.1f dB, output %.1f dB\n",
                     amount > 0 ? "Denoise" : "Dereverb", std::fabs (amount), level, sIn, sOut);
        CHECK (sOut < sIn + 6.0, "the residual is spiky: %.1f dB vs %.1f dB", sOut, sIn);
    }
}

// Denoise 0.5 does something in between.
TEST (clean_denoise_amount_scales)
{
    const double sr = 48000.0;
    const int n = (int)(sr * 4.0), fn = 65536;
    Stereo in = silence (n);
    addNoise (in, 0.01, 21);
    double prev = 1.0;
    for (double a : {0.0, 0.25, 0.5, 1.0})
    {
        Clean c;
        c.prepare (sr, 512);
        c.setDenoise (a);
        const Stereo out = run (c, in, 256);
        const int start = n - fn - 100;
        const double r = db (bandMean (spectrum (out, start, fn), sr, fn, 100, 20000)
                             / bandMean (spectrum (in, start - c.latency (), fn), sr, fn, 100, 20000));
        std::printf ("    Denoise %.2f: noise %+.1f dB\n", a, r);
        CHECK (r < prev + 0.01, "more Denoise, less reduction: %.2f -> %+.1f dB", a, r);
        prev = r;
    }
}

// A decaying tonal hit after two seconds of noise (40 dB under the hit): Denoise 1 keeps the hit's
// first half second whole (the floor rises slowly, so it does not chase the decay).
TEST (clean_denoise_keeps_a_decaying_hit)
{
    const double sr = 48000.0;
    const int n = (int)(sr * 4.0), t0 = (int)(sr * 2.0);
    Stereo hit = silence (n);
    addHit (hit, sr, t0, 0.8, 0.35);
    Stereo in = hit;
    addNoise (in, 0.8 * 0.35 * std::pow (10.0, -40.0 / 20.0), 31);

    Clean c;
    c.prepare (sr, 512);
    c.setDenoise (1.0);
    const Stereo out = run (c, in, 0, 512, 9);
    const int lat = c.latency ();
    const auto seg = [&] (double a, double b) { return std::make_pair (t0 + (int)(a * sr), t0 + (int)(b * sr)); };
    for (auto ab : {seg (0.0, 0.1), seg (0.1, 0.5), seg (0.5, 1.0)})
    {
        const double eOut = energy (out.l, ab.first + lat, ab.second + lat) + energy (out.r, ab.first + lat, ab.second + lat);
        const double eHit = energy (hit.l, ab.first, ab.second) + energy (hit.r, ab.first, ab.second);
        const double eIn = energy (in.l, ab.first, ab.second) + energy (in.r, ab.first, ab.second);
        std::printf ("    %4.0f-%4.0f ms after the hit: out vs clean hit %+.2f dB (noisy input vs clean %+.2f dB)\n",
                     1000.0 * (ab.first - t0) / sr, 1000.0 * (ab.second - t0) / sr, db (eOut / eHit), db (eIn / eHit));
        if (ab.first - t0 < (int)(0.5 * sr))
            CHECK (std::fabs (db (eOut / eHit)) < 1.0, "the hit lost %+.2f dB", db (eOut / eHit));
    }
    // the noise alone, from the start of the stream (the floor is learned in about a second) up to the hit
    std::printf ("    the noise before the hit (the stream starts with it):");
    double settled = -1e9;
    for (double a = 0.0; a < 1.99; a += 0.25)
    {
        const int i0 = (int)(a * sr), i1 = (int)((a + 0.25) * sr) - 1000;
        const double r = db (energy (out.l, i0 + lat, i1 + lat) / energy (in.l, i0, i1));
        std::printf (" %.2f s %+.1f dB%s", a, r, a < 1.7 ? "," : "\n");
        if (a >= 1.25)
            settled = std::max (settled, r);
    }
    CHECK (settled < -25.0, "noise before the hit, 1.25 s on: %+.1f dB", settled);
}

// The noisy side of an explosion: a burst of broadband rumble dying away (T60 2 s) over a noise floor
// 45 dB under its start. It is not tonal, so only the floor protects it: the floor was learned before
// and rises slowly, so the rumble passes until it gets near the noise.
TEST (clean_denoise_keeps_a_noisy_decay)
{
    const double sr = 48000.0;
    const int n = (int)(sr * 5.0), t0 = (int)(sr * 2.0);
    Stereo burst = silence (n);
    std::mt19937 rng (71);
    std::normal_distribution<double> nd (0.0, 1.0);
    const double lp = std::exp (-2.0 * kPi * 800.0 / sr);
    double zl = 0.0, zr = 0.0;
    for (int i = t0; i < n; ++i)
    {
        const double t = (i - t0) / sr;
        zl = lp * zl + (1.0 - lp) * nd (rng);
        zr = lp * zr + (1.0 - lp) * nd (rng);
        const double env = 0.5 * std::min (1.0, t / 0.005) * std::pow (10.0, -3.0 * t / 2.0) * 8.0;
        burst.l[(size_t)i] = (float)(env * zl);
        burst.r[(size_t)i] = (float)(env * zr);
    }
    const double burstRms = std::sqrt (energy (burst.l, t0, t0 + (int)(0.1 * sr)) / (0.1 * sr));
    Stereo noise = silence (n), in = burst;
    addNoise (noise, burstRms * std::pow (10.0, -45.0 / 20.0), 73);
    for (int i = 0; i < n; ++i)
    {
        in.l[(size_t)i] += noise.l[(size_t)i];
        in.r[(size_t)i] += noise.r[(size_t)i];
    }

    Clean c;
    c.prepare (sr, 512);
    c.setDenoise (1.0);
    const Stereo out = run (c, in, 0, 512, 29);
    const int lat = c.latency ();
    std::printf ("    rumble (out vs clean):");
    for (double a : {0.0, 0.25, 0.5, 1.0, 1.5})
    {
        const double b = a == 0.0 ? 0.25 : a == 0.25 ? 0.5 : a + 0.5;
        const int i0 = t0 + (int)(a * sr), i1 = t0 + (int)(b * sr);
        const double eOut = energy (out.l, i0 + lat, i1 + lat) + energy (out.r, i0 + lat, i1 + lat);
        const double eBurst = energy (burst.l, i0, i1) + energy (burst.r, i0, i1);
        const double over = db (eBurst / (energy (noise.l, i0, i1) + energy (noise.r, i0, i1))); // burst over the noise
        std::printf (" %.2f-%.2f s %+.2f dB (%.0f dB over the noise)%s", a, b, db (eOut / eBurst), over, a < 1.4 ? "," : "\n");
        if (over > 15.0)
            CHECK (std::fabs (db (eOut / eBurst)) < 1.0, "the rumble lost %+.2f dB %.2f s in, %.0f dB over the noise",
                   db (eOut / eBurst), a, over);
    }
}

// A short tonal hit with a synthetic room tail (decaying low-passed noise, T60 1 s, different in
// each channel): Dereverb 1 pulls the tail 100-600 ms after the hit down by several dB and leaves the
// hit's peak within 1.5 dB.
TEST (clean_dereverb_tail)
{
    for (double sr : {48000.0, 96000.0})
    {
        const int n = (int)(sr * 3.0), t0 = (int)(sr * 0.5);
        Stereo dry = silence (n);
        addHit (dry, sr, t0, 0.9, 0.03);
        Stereo in = dry;
        std::mt19937 rng (41);
        std::normal_distribution<double> nd (0.0, 1.0);
        const double lp = std::exp (-2.0 * kPi * 5000.0 / sr);
        double zl = 0.0, zr = 0.0;
        for (int i = t0; i < n; ++i)
        {
            const double t = (i - t0) / sr - 0.005; // the room answers 5 ms late
            zl = lp * zl + (1.0 - lp) * nd (rng);
            zr = lp * zr + (1.0 - lp) * nd (rng);
            if (t < 0.0)
                continue;
            const double env = 0.12 * std::min (1.0, t / 0.01) * std::pow (10.0, -3.0 * t / 1.0); // T60 1 s
            in.l[(size_t)i] += (float)(env * zl * 3.0);
            in.r[(size_t)i] += (float)(env * zr * 3.0);
        }

        const int lat = [&] { Clean c; c.prepare (sr, 512); return c.latency (); } ();
        const auto measure = [&] (double amount, double& pk, double& tail, double& body) {
            Clean c;
            c.prepare (sr, 512);
            c.setDereverb (amount);
            const Stereo out = run (c, in, 0, 512, 13);
            pk = std::max (peak (out.l, t0 + lat, t0 + lat + (int)(0.05 * sr)), peak (out.r, t0 + lat, t0 + lat + (int)(0.05 * sr)));
            const int a = t0 + lat + (int)(0.1 * sr), b = t0 + lat + (int)(0.6 * sr);
            tail = energy (out.l, a, b) + energy (out.r, a, b);
            body = energy (out.l, t0 + lat, t0 + lat + (int)(0.04 * sr)) + energy (out.r, t0 + lat, t0 + lat + (int)(0.04 * sr));
            if (amount == 1.0)
            {
                // out vs in, stretch by stretch
                static const double edges[] = {0.0, 0.05, 0.1, 0.2, 0.4, 0.6, 1.0, 1.5, 2.0};
                std::printf ("      Dereverb 1, out vs in from the hit:");
                for (int e = 0; e < 8; ++e)
                {
                    const int i0 = t0 + (int)(edges[e] * sr), i1 = t0 + (int)(edges[e + 1] * sr);
                    std::printf (" %.0f-%.0f ms %+.1f dB%s", 1000.0 * edges[e], 1000.0 * edges[e + 1],
                                 db ((energy (out.l, i0 + lat, i1 + lat) + energy (out.r, i0 + lat, i1 + lat))
                                     / (energy (in.l, i0, i1) + energy (in.r, i0, i1))),
                                 e < 7 ? "," : "\n");
                }
            }
        };
        double pk0, tail0, body0, pk1, tail1, body1, pkH, tailH, bodyH;
        measure (0.0, pk0, tail0, body0);
        measure (0.5, pkH, tailH, bodyH);
        measure (1.0, pk1, tail1, body1);
        const double drr = db ((energy (dry.l, t0, n) + energy (dry.r, t0, n))
                               / (energy (in.l, t0, n) + energy (in.r, t0, n) - energy (dry.l, t0, n) - energy (dry.r, t0, n)));
        std::printf ("    %6.0f Hz (direct-to-reverb %+.1f dB): Dereverb 0.5 / 1: peak %+.2f / %+.2f dB, first 40 ms %+.2f / %+.2f dB, tail 100-600 ms %+.1f / %+.1f dB\n",
                     sr, drr, 20.0 * std::log10 (pkH / pk0), 20.0 * std::log10 (pk1 / pk0), db (bodyH / body0), db (body1 / body0),
                     db (tailH / tail0), db (tail1 / tail0));
        CHECK (std::fabs (20.0 * std::log10 (pk1 / pk0)) < 1.5, "the peak moved %+.2f dB", 20.0 * std::log10 (pk1 / pk0));
        CHECK (db (tail1 / tail0) < -6.0, "the tail only went %+.1f dB", db (tail1 / tail0));
        CHECK (db (tailH / tail0) < -1.0 && db (tailH / tail0) > db (tail1 / tail0), "Dereverb 0.5 not in between: %+.1f dB",
               db (tailH / tail0));
    }
}

// What Dereverb 1 does to things that are not a room: a dry ringing hit (no tail at all) and a
// steady tone. Informative (a late-reverb estimate can not tell a slow ring from a room), with loose
// bounds: the first 50 ms of the hit stay.
TEST (clean_dereverb_dry_material)
{
    const double sr = 48000.0;
    const int n = (int)(sr * 3.0), t0 = (int)(sr * 0.5);
    for (double tau : {0.05, 0.3})
    {
        Stereo in = silence (n);
        addHit (in, sr, t0, 0.8, tau);
        Clean c;
        c.prepare (sr, 512);
        c.setDereverb (1.0);
        const Stereo out = run (c, in, 0, 512, 3);
        const int lat = c.latency ();
        const auto seg = [&] (double a, double b) {
            const int i0 = t0 + (int)(a * sr), i1 = t0 + (int)(b * sr);
            return db ((energy (out.l, i0 + lat, i1 + lat) + energy (out.r, i0 + lat, i1 + lat))
                       / (energy (in.l, i0, i1) + energy (in.r, i0, i1)));
        };
        std::printf ("    dry hit, decay tau %.2f s: 0-50 ms %+.2f dB, 50-100 ms %+.2f dB, 100-300 ms %+.1f dB\n", tau,
                     seg (0.0, 0.05), seg (0.05, 0.1), seg (0.1, 0.3));
        CHECK (std::fabs (seg (0.0, 0.05)) < 1.0, "the hit's first 50 ms moved %+.2f dB", seg (0.0, 0.05));
    }
    Stereo in = silence (n);
    for (int i = 0; i < n; ++i)
        in.l[(size_t)i] = in.r[(size_t)i] = (float)(0.3 * std::sin (2.0 * kPi * 220.0 * i / sr));
    Clean c;
    c.prepare (sr, 512);
    c.setDereverb (1.0);
    const Stereo out = run (c, in, 256);
    const double r = db (energy (out.l, n / 2, n) / energy (in.l, n / 2, n));
    std::printf ("    a steady 220 Hz tone: %+.2f dB\n", r);
    CHECK (r > -12.0, "a steady tone lost %+.2f dB", r);
}

// The gains are linked: a sound panned left stays exactly where it was (right = half of left in,
// right = half of left out), and a left-only sound puts nothing in the right channel.
TEST (clean_stereo_linked)
{
    const double sr = 48000.0;
    const int n = (int)(sr * 3.0);
    Stereo mono = silence (n);
    addNoise (mono, 0.01, 51);
    addHit (mono, sr, n / 3, 0.8, 0.2);
    Stereo panned = mono, leftOnly = mono;
    for (int i = 0; i < n; ++i)
    {
        panned.r[(size_t)i] = 0.5f * mono.l[(size_t)i];
        leftOnly.r[(size_t)i] = 0.0f;
    }
    Clean c;
    c.prepare (sr, 512);
    c.setDenoise (1.0);
    c.setDereverb (1.0);
    const Stereo a = run (c, panned, 0, 512, 17);
    c.reset ();
    const Stereo b = run (c, leftOnly, 0, 512, 17);
    double worst = 0.0, leak = 0.0, peakL = peak (b.l, 0, n);
    for (int i = c.latency (); i < n; ++i)
    {
        worst = std::max (worst, (double)std::fabs (a.r[(size_t)i] - 0.5f * a.l[(size_t)i]));
        leak = std::max (leak, (double)std::fabs (b.r[(size_t)i]));
    }
    std::printf ("    panned: max |R - L/2| %.1e (L peaks at %.2f); left only: right peaks at %.1e\n", worst, peak (a.l, 0, n), leak);
    CHECK (worst < 1e-5, "the pan moved: %g", worst);
    CHECK (leak < 1e-5 * peakL, "the right channel got %g", leak);
}

// Silence in, silence out (exactly, and no denormals), also right after something loud.
TEST (clean_silence)
{
    Clean c;
    c.prepare (48000.0, 512);
    c.setDenoise (1.0);
    c.setDereverb (1.0);
    Stereo z = silence (48000);
    Stereo out = run (c, z, 480);
    double m = peak (out.l, 0, 48000) + peak (out.r, 0, 48000);
    CHECK (m == 0.0, "silence became %g", m);

    Stereo burst = silence (96000 * 3);
    addNoise (burst, 0.5, 3, 0, 24000);
    out = run (c, burst, 333);
    int subnormals = 0;
    for (const Buf* ch : {&out.l, &out.r})
        for (float v : *ch)
            if (std::fpclassify (v) == FP_SUBNORMAL)
                ++subnormals;
    const double tailPeak = peak (out.l, 24000 + 2 * 2048, (int)out.l.size ()) + peak (out.r, 24000 + 2 * 2048, (int)out.l.size ());
    std::printf ("    silence: %g out; after a burst: %g once the frames have passed, %d subnormal samples\n", m, tailPeak, subnormals);
    CHECK (tailPeak == 0.0, "the silence after a burst is %g", tailPeak);
    CHECK (subnormals == 0, "%d subnormal samples", subnormals);
    CHECK (allFinite (out), "not finite");
}

// Random settings, sample rates, block sizes and nasty signals: always finite.
TEST (clean_fuzz)
{
    std::mt19937 rng (1234);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    const double rates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};
    bool finite = true;
    for (int run = 0; run < 12 && finite; ++run)
    {
        const double sr = rates[run % 6];
        const int maxBlock = 1 + (int)(u (rng) * 2047);
        Clean c;
        c.prepare (sr, maxBlock);
        const int lat = c.latency ();
        Buf l ((size_t)maxBlock), r ((size_t)maxBlock);
        const int total = (int)(sr * 2.0);
        double ph = 0.0;
        for (int done = 0; done < total && finite;)
        {
            const int n = 1 + (int)(u (rng) * (maxBlock - 1));
            if (u (rng) < 0.1)
                c.setDenoise (u (rng) < 0.3 ? 0.0 : u (rng));
            if (u (rng) < 0.1)
                c.setDereverb (u (rng) < 0.3 ? 0.0 : u (rng));
            if (u (rng) < 0.005)
                c.reset ();
            const int kind = (int)(u (rng) * 6);
            for (int i = 0; i < n; ++i)
            {
                ph += 2.0 * kPi * 100.0 / sr;
                switch (kind)
                {
                    case 0: l[(size_t)i] = r[(size_t)i] = 0.0f; break;
                    case 1: l[(size_t)i] = (float)(u (rng) * 2 - 1); r[(size_t)i] = (float)(u (rng) * 2 - 1); break;
                    case 2: l[(size_t)i] = (float)(8.0 * std::sin (ph)); r[(size_t)i] = -l[(size_t)i]; break;
                    case 3: l[(size_t)i] = 1e-38f; r[(size_t)i] = -1e-39f; break; // tiny and subnormal
                    case 4: // now and then garbage: NaN, infinity, absurdly loud
                        l[(size_t)i] = i % 97 == 0 ? std::nanf ("") : (float)(u (rng) - 0.5);
                        r[(size_t)i] = i % 89 == 0 ? (i % 2 ? std::numeric_limits<float>::infinity () : 1e30f) : (float)(u (rng) - 0.5);
                        break;
                    default: l[(size_t)i] = (float)((u (rng) * 2 - 1) * 1e-6); r[(size_t)i] = 0.0f; break;
                }
            }
            c.process (l.data (), r.data (), n);
            for (int i = 0; i < n; ++i)
                finite = finite && std::isfinite (l[(size_t)i]) && std::isfinite (r[(size_t)i]) && std::fabs (l[(size_t)i]) < 100.0f
                         && std::fabs (r[(size_t)i]) < 100.0f;
            CHECK (c.latency () == lat, "latency moved");
            done += n;
        }
        CHECK (finite, "run %d (%.0f Hz, max block %d): not finite or blew up", run, sr, maxBlock);
    }
    std::printf ("    12 runs of 2 s, random settings / blocks / signals: %s\n", finite ? "all finite" : "FAILED");
}

// CPU: well under real time.
TEST (clean_cpu)
{
    for (double sr : {48000.0, 192000.0})
    {
        const int n = (int)(sr * 20.0);
        Stereo in = silence (n);
        addNoise (in, 0.05, 5);
        for (int t = 0; t < n; t += (int)(sr * 0.7))
            addHit (in, sr, t, 0.7, 0.2);
        Clean c;
        c.prepare (sr, 256);
        c.setDenoise (0.8);
        c.setDereverb (0.6);
        const auto t0 = std::chrono::steady_clock::now ();
        const Stereo out = run (c, in, 256);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
        const double pct = 100.0 * secs / 20.0;

        // The work comes in lumps: one frame per hop (~10.7 ms), all in the block that completes it.
        // With 64-sample blocks, the slowest single block against that block's own duration:
        Stereo x = in;
        double worstBlock = 0.0;
        for (int i = 0; i + 64 <= n; i += 64)
        {
            const auto b0 = std::chrono::steady_clock::now ();
            c.process (x.l.data () + i, x.r.data () + i, 64);
            worstBlock = std::max (worstBlock, std::chrono::duration<double> (std::chrono::steady_clock::now () - b0).count ());
        }
        std::printf ("    %6.0f Hz, both on, 256-sample blocks: %.2f %% of real time (one core); the slowest 64-sample block "
                     "takes %.0f us (%.0f %% of its %.0f us)\n",
                     sr, pct, 1e6 * worstBlock, 100.0 * worstBlock * sr / 64.0, 1e6 * 64.0 / sr);
        CHECK (pct < (sr > 100000.0 ? 12.0 : 3.0), "too slow: %.2f %%", pct);
        CHECK (allFinite (out), "not finite");
    }
}

DETONATR_TEST_MAIN

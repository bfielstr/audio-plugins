// Small DSP pieces Ciphr's voices and processor use: an ADSR envelope, a morphing state-variable filter
// (TPT / zero-delay form), a frequency shifter (an IIR Hilbert transformer: two allpass chains a quarter
// cycle apart), a fractional delay line and an allpass diffuser.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ciphr::dsp {

constexpr double kPi = 3.14159265358979323846;

// ---- ADSR: a linear attack from wherever the level is (so a retriggered or stolen voice never clicks),
// exponential decay and release (each reaches within 1 % of its target in its time).
class Envelope
{
public:
    enum Stage { kIdle, kAttack, kDecay, kSustain, kRelease };

    void setSampleRate (double s) { sr = s; }
    void set (double attackMs, double decayMs, double sustain, double releaseMs)
    {
        attackStep = 1.0f / (float)std::max (1.0, attackMs * 0.001 * sr);
        decayCoef = (float)std::exp (-4.6 / std::max (1.0, decayMs * 0.001 * sr));
        sus = (float)std::clamp (sustain, 0.0, 1.0);
        releaseCoef = (float)std::exp (-4.6 / std::max (1.0, releaseMs * 0.001 * sr));
    }
    void gateOn () { stage = kAttack; }
    void gateOff ()
    {
        if (stage != kIdle)
            stage = kRelease;
    }
    void reset ()
    {
        stage = kIdle;
        level = 0.0f;
    }
    inline float tick ()
    {
        switch (stage)
        {
            case kAttack:
                level += attackStep;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = kDecay;
                }
                break;
            case kDecay:
                level = sus + (level - sus) * decayCoef;
                if (std::fabs (level - sus) < 1e-5f)
                {
                    level = sus;
                    stage = kSustain;
                }
                break;
            case kSustain: level = sus; break;
            case kRelease:
                level *= releaseCoef;
                if (level < 1e-5f)
                {
                    level = 0.0f;
                    stage = kIdle;
                }
                break;
            case kIdle: break;
        }
        return level;
    }
    float value () const { return level; }
    Stage current () const { return stage; }
    bool active () const { return stage != kIdle; }

private:
    double sr = 48000.0;
    Stage stage = kIdle;
    float level = 0.0f, attackStep = 0.01f, decayCoef = 0.999f, sus = 0.8f, releaseCoef = 0.999f;
};

// ---- Morphing multimode state-variable filter (Zavalishin's topology-preserving transform, Simper's
// formulation). Type 0 low-pass, 0.5 band-pass, 1 high-pass, crossfading between neighbours.
class Svf
{
public:
    // cutoff in Hz, resonance 0 .. 1, type 0 .. 1
    void set (double cutoff, double resonance, double type, double sr)
    {
        const double fc = std::clamp (cutoff, 10.0, 0.47 * sr);
        const double g = std::tan (kPi * fc / sr);
        k = (float)(0.04 + 1.37 * (1.0 - std::clamp (resonance, 0.0, 1.0)));
        a1 = (float)(1.0 / (1.0 + g * (g + k)));
        a2 = (float)(g * a1);
        a3 = (float)(g * a2);
        const double t = std::clamp (type, 0.0, 1.0);
        wl = (float)std::max (0.0, 1.0 - 2.0 * t);
        wb = (float)(t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t);
        wh = (float)std::max (0.0, 2.0 * t - 1.0);
    }
    void reset () { ic1 = ic2 = 0.0f; }
    inline float tick (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        const float hp = x - k * v1 - v2;
        return wl * v2 + wb * v1 + wh * hp;
    }

private:
    float k = 1.4f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float wl = 1.0f, wb = 0.0f, wh = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

// ---- Frequency shifter: an IIR Hilbert transformer (two chains of four second-order allpasses whose
// outputs stay a quarter cycle apart over most of the band; Olli Niemitalo's coefficients) and a
// complex oscillator. The output is the single sideband: every frequency moved up by `hz` (down for a
// negative `hz`). `inPhase` is the unshifted signal through the same allpasses (the same phase response
// as the shifted one), so the two can be summed without colouring the sound at 0 Hz.
class FreqShifter
{
public:
    void setSampleRate (double s) { sr = s; }
    void setShift (double hz) { step = 2.0 * kPi * hz / sr; }
    void reset ()
    {
        for (auto& s : a)
            s = {};
        for (auto& s : b)
            s = {};
        aDelay = 0.0;
        phase = 0.0;
    }
    inline double tick (double x, double& inPhase)
    {
        static constexpr double ca[4] = {0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737};
        static constexpr double cb[4] = {0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278};
        double i = x, q = x;
        for (int s = 0; s < 4; ++s)
            i = a[s].tick (i, ca[s] * ca[s]);
        for (int s = 0; s < 4; ++s)
            q = b[s].tick (q, cb[s] * cb[s]);
        const double id = aDelay; // (the first chain one sample later)
        aDelay = i;
        inPhase = id;
        const double y = id * std::cos (phase) + q * std::sin (phase);
        phase += step;
        if (phase > kPi)
            phase -= 2.0 * kPi;
        else if (phase < -kPi)
            phase += 2.0 * kPi;
        return y;
    }

private:
    // y[n] = c (x[n] + y[n-2]) - x[n-2]
    struct Stage
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        inline double tick (double x, double c)
        {
            const double y = c * (x + y2) - x2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    double sr = 48000.0, step = 0.0, phase = 0.0, aDelay = 0.0;
    Stage a[4], b[4];
};

// ---- A delay line read at fractional delays (linear interpolation). Its length is a power of two.
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        size_t n = 1;
        while (n < (size_t)maxSamples + 4)
            n <<= 1;
        buf.assign (n, 0.0f);
        mask = n - 1;
        pos = 0;
    }
    void reset ()
    {
        std::fill (buf.begin (), buf.end (), 0.0f);
        pos = 0;
    }
    inline void write (float x)
    {
        buf[pos] = x;
        pos = (pos + 1) & mask;
    }
    // `d` samples ago (>= 1: the sample written last is 1 ago), at most the length less a few
    inline float read (double d) const
    {
        const int di = (int)d;
        const float f = (float)(d - di);
        const float x0 = buf[(pos - (size_t)di) & mask], x1 = buf[(pos - (size_t)di - 1) & mask];
        return x0 + f * (x1 - x0);
    }
    size_t size () const { return buf.size (); }

private:
    std::vector<float> buf;
    size_t mask = 0, pos = 0;
};

// ---- A Schroeder allpass diffuser with a fractional (modulatable) delay: unity gain at every frequency,
// smears an impulse into a train of echoes.
class Allpass
{
public:
    void prepare (int maxSamples) { line.prepare (maxSamples); }
    void reset () { line.reset (); }
    inline float tick (float x, double delay, float g)
    {
        const float d = line.read (delay);
        const float v = x + g * d;
        line.write (v);
        return d - g * v;
    }

private:
    DelayLine line;
};

// fast, smooth saturation: about linear up to 0.5, never beyond +-1
inline float softClip (float x)
{
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

} // namespace ciphr::dsp

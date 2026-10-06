#include "Voice.h"

#include <algorithm>
#include <cmath>

namespace ciphr {

namespace {
// the cluster's sum: about as loud as one full-scale oscillator
const float kOscGain = (float)(0.6 / std::sqrt ((double)kOscs));

inline double wrap (double p) { return p - std::floor (p); }
} // namespace

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    amp.setSampleRate (sr);
    fenv.setSampleRate (sr);
    kill ();
}

void Voice::setEnvelopes (double a, double d, double s, double r, double fa, double fd, double fs, double fr)
{
    amp.set (a, d, s, r);
    fenv.set (fa, fd, fs, fr);
}

void Voice::start (int note, float velocity, const Patch& p, bool fresh, uint32_t order)
{
    noteNum = note;
    velGainRaw = std::clamp (velocity, 0.0f, 1.0f);
    gate = true;
    startOrder = order;
    if (fresh)
    {
        for (int k = 0; k < kOscs; ++k)
        {
            for (int e = 0; e < kEntries; ++e)
                phase[k][e] = p.phase0[k];
            last[k] = 0.0f;
        }
        filter.reset ();
        amp.reset ();
        fenv.reset ();
    }
    amp.gateOn ();
    fenv.gateOn ();
}

void Voice::release ()
{
    gate = false;
    amp.gateOff ();
    fenv.gateOff ();
}

void Voice::kill ()
{
    gate = false;
    amp.reset ();
    fenv.reset ();
    filter.reset ();
}

void Voice::render (float* out, int n, const VoiceBlock& b, const Patch& p)
{
    if (!amp.active ())
        return;
    const WaveBank& bank = WaveBank::get ();

    // each oscillator: the two entries of its list it crossfades, their tables (by octave) and steps
    const float* tab0[kOscs];
    const float* tab1[kOscs];
    double inc0[kOscs], inc1[kOscs], pmScale[kOscs];
    int e0[kOscs], e1[kOscs];
    float frac[kOscs];
    for (int k = 0; k < kOscs; ++k)
    {
        const double pos = std::clamp (b.oscPos[k], 0.0, (double)(kEntries - 1));
        int a = (int)pos;
        if (a >= kEntries - 1)
            a = kEntries - 1;
        e0[k] = a;
        e1[k] = std::min (a + 1, kEntries - 1);
        frac[k] = e1[k] == a ? 0.0f : (float)(pos - a);
        auto stepOf = [&] (int e, const float*& tab) {
            const Entry& en = p.osc[k][e];
            const double semis = noteNum - 69 + b.tune + en.semis + b.oscCents[k] * 0.01;
            const double hz = 440.0 * std::pow (2.0, semis / 12.0);
            const double inc = hz / sr;
            const int level = WaveBank::levelFor (inc);
            tab = level < 0 ? nullptr : bank.table (en.wave, level);
            return inc;
        };
        inc0[k] = stepOf (e0[k], tab0[k]);
        inc1[k] = stepOf (e1[k], tab1[k]);
        pmScale[k] = std::min (1.0, kPmFullBelowHz / std::max (1.0, inc0[k] * sr));
    }

    // the filter for this slice: the cutoff with key tracking (from C3) and its envelope where it is now
    const double octaves = b.keyTrack * (noteNum - 60) / 12.0 + b.envAmount * 5.0 * fenv.value ();
    filter.set (b.cutoff * std::pow (2.0, octaves), b.resonance, b.type, sr);
    const float velGain = 1.0f - b.velocity + b.velocity * velGainRaw;
    const bool plain = crossBypass || (b.crossFrom == 0.0f && b.crossTo == 0.0f);

    for (int i = 0; i < n; ++i)
    {
        float o[kOscs];
        float cross = 0.0f;
        if (!plain)
            cross = b.crossFrom + (b.crossTo - b.crossFrom) * (float)(i + 1) / (float)n;
        if (cross < 0.0f)
        {
            // FM (phase modulation): each oscillator's phase moved by its neighbour's output (the first by the
            // last one's previous sample)
            const double depth = -cross * kPmDepth;
            float prev = last[kOscs - 1];
            for (int k = 0; k < kOscs; ++k)
            {
                const double m = depth * pmScale[k] * prev;
                const float s0 = tab0[k] ? WaveBank::read (tab0[k], wrap (phase[k][e0[k]] + m)) : 0.0f;
                const float s1 = tab1[k] ? WaveBank::read (tab1[k], wrap (phase[k][e1[k]] + m)) : 0.0f;
                o[k] = s0 + frac[k] * (s1 - s0);
                prev = o[k];
            }
        }
        else
        {
            for (int k = 0; k < kOscs; ++k)
            {
                const float s0 = tab0[k] ? WaveBank::read (tab0[k], phase[k][e0[k]]) : 0.0f;
                const float s1 = tab1[k] ? WaveBank::read (tab1[k], phase[k][e1[k]]) : 0.0f;
                o[k] = s0 + frac[k] * (s1 - s0);
            }
            if (cross > 0.0f)
            {
                // ring modulation: each oscillator times its neighbour
                float r[kOscs];
                for (int k = 0; k < kOscs; ++k)
                    r[k] = o[k] * o[(k + 1) % kOscs];
                for (int k = 0; k < kOscs; ++k)
                    o[k] += cross * (kRingGain * r[k] - o[k]);
            }
        }
        float sum = 0.0f;
        for (int k = 0; k < kOscs; ++k)
        {
            sum += o[k];
            last[k] = o[k];
            double& ph0 = phase[k][e0[k]];
            ph0 += inc0[k];
            if (ph0 >= 1.0)
                ph0 -= std::floor (ph0);
            if (e1[k] != e0[k])
            {
                double& ph1 = phase[k][e1[k]];
                ph1 += inc1[k];
                if (ph1 >= 1.0)
                    ph1 -= std::floor (ph1);
            }
        }
        sum *= kOscGain;
        if (b.input)
            sum += b.input[i] * b.inputGain;
        const float y = filter.tick (sum);
        fenv.tick ();
        out[i] += y * amp.tick () * velGain;
    }
}

} // namespace ciphr

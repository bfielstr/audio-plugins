#include "SpaceFx.h"

#include <cmath>

namespace ciphr {

namespace {
// the diffusers' delays (ms) at Length 420 ms; they scale with Length (0.25 x .. 2.5 x)
constexpr double kRefLengthMs = 420.0;
constexpr double kMinApScale = 0.25, kMaxApScale = 2.5;
constexpr double kInMs[2][4] = {{4.77, 3.59, 12.73, 9.31}, {5.21, 3.13, 11.97, 9.87}};
constexpr double kOutMs[2][4] = {{13.1, 19.7, 27.3, 35.9}, {14.3, 18.1, 29.9, 33.7}};
constexpr double kLoopMs[2][2] = {{23.3, 37.9}, {25.7, 35.1}};
constexpr float kInG = 0.62f, kOutG = 0.6f, kLoopG = 0.55f;
} // namespace

void SpaceFx::prepare (double sampleRate)
{
    sr = sampleRate;
    // the longest tap with its modulation and drift, and room for the reads
    const int maxDelay = (int)std::ceil (kMaxLengthMs * 0.001 * sr * 1.06 + 0.01 * sr) + 8;
    const int maxAp = (int)std::ceil (45.0 * kMaxApScale * 0.001 * sr) + 8;
    for (int c = 0; c < 2; ++c)
    {
        line[c].prepare (maxDelay);
        for (auto& a : inAp[c])
            a.prepare (maxAp);
        for (auto& a : outAp[c])
            a.prepare (maxAp);
        for (auto& a : loopAp[c])
            a.prepare (maxAp);
        shifter[c].setSampleRate (sr);
    }
    smooth = (float)(1.0 - std::exp (-1.0 / (0.03 * sr)));
    lengthSlew = 1.0 - std::exp (-(double)kSlice / (0.08 * sr));
    dcCoef = (float)(1.0 - 2.0 * dsp::kPi * 20.0 / sr);
    setCharacter (0.5);
    reset ();
}

void SpaceFx::reset ()
{
    for (int c = 0; c < 2; ++c)
    {
        line[c].reset ();
        for (auto& a : inAp[c])
            a.reset ();
        for (auto& a : outAp[c])
            a.reset ();
        for (auto& a : loopAp[c])
            a.reset ();
        shifter[c].reset ();
        fb[c] = damp[c] = dcX[c] = dcY[c] = 0.0f;
    }
    space = spaceT;
    regen = regenT;
    lengthNow = lengthT;
    for (int i = 0; i < kTaps; ++i)
        lfoPhase[i] = pattern[i].lfoPhase;
    updateTaps (0); // the taps where they belong, no ramp
}

void SpaceFx::setPattern (const Tap* taps)
{
    for (int i = 0; i < kTaps; ++i)
        pattern[i] = taps[i];
}

void SpaceFx::setShift (double hz)
{
    for (auto& s : shifter)
        s.setShift (hz);
}

void SpaceFx::setCharacter (double c)
{
    c = std::clamp (c, 0.0, 1.0);
    const double count = 2.0 + 6.0 * c;
    for (int i = 0; i < kTaps; ++i)
        weight[i] = i < 2 ? 1.0 : std::clamp (count - i, 0.0, 1.0);
    const double damping = 2500.0 * std::pow (2.0, c * std::log2 (16000.0 / 2500.0));
    dampCoef = (float)(1.0 - std::exp (-2.0 * dsp::kPi * std::min (damping, 0.45 * sr) / sr));
}

void SpaceFx::setDrift (const double* time, const double* gain)
{
    for (int i = 0; i < kTaps; ++i)
    {
        driftTime[i] = time[i];
        driftGain[i] = gain[i];
    }
}

void SpaceFx::updateTaps (int n)
{
    lengthNow += (lengthT - lengthNow) * (n > 0 ? lengthSlew : 1.0);
    apScale = std::clamp (lengthNow / (kRefLengthMs * 0.001 * sr), kMinApScale, kMaxApScale);
    const double maxDelay = (double)line[0].size () - 4.0;
    const double rateScale = 0.4 + 1.6 * movement;
    double norm = 0.0;
    for (int i = 0; i < kTaps; ++i)
    {
        const Tap& t = pattern[i];
        const double base = t.time * lengthNow * (1.0 + 0.03 * driftTime[i]);
        lfoPhase[i] += t.lfoRate * rateScale * (double)n / sr;
        lfoPhase[i] -= std::floor (lfoPhase[i]);
        const double depth = movement * (0.0015 * sr + 0.004 * base);
        const double target = std::clamp (base + depth * std::sin (2.0 * dsp::kPi * lfoPhase[i]), 1.0, maxDelay);
        if (n > 0)
            delayStep[i] = (target - delayNow[i]) / (double)n;
        else
        {
            delayNow[i] = target;
            delayStep[i] = 0.0;
        }
        const double g = weight[i] * t.gain * (1.0 + 0.3 * driftGain[i]);
        norm += g * g;
    }
    tapNorm = (float)(norm > 1e-9 ? 0.8 / std::sqrt (norm) : 0.0);
}

void SpaceFx::process (float* l, float* r, int n)
{
    float* io[2] = {l, r};
    for (int a = 0; a < n; a += kSlice)
    {
        const int m = std::min (kSlice, n - a);
        updateTaps (m);
        double inD[2][4], outD[2][4], loopD[2][2];
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 4; ++k)
            {
                inD[c][k] = kInMs[c][k] * 0.001 * sr * apScale;
                outD[c][k] = kOutMs[c][k] * 0.001 * sr * apScale;
            }
            for (int k = 0; k < 2; ++k)
                loopD[c][k] = kLoopMs[c][k] * 0.001 * sr * apScale;
        }
        float gl[kTaps], gr[kTaps];
        for (int i = 0; i < kTaps; ++i)
        {
            const float g = (float)(weight[i] * pattern[i].gain * (1.0 + 0.3 * driftGain[i])) * tapNorm;
            gl[i] = g * (float)std::min (1.0, 1.0 - pattern[i].pan);
            gr[i] = g * (float)std::min (1.0, 1.0 + pattern[i].pan);
        }
        // the loop's channel mixing (a rotation: no gain), from Space at the slice's start
        const float rot = 0.6f * space, cs = std::cos (rot), sn = std::sin (rot);
        for (int j = 0; j < m; ++j)
        {
            space += (spaceT - space) * smooth;
            regen += (regenT - regen) * smooth;
            const float s = space;
            float tapL = 0.0f, tapR = 0.0f;
            for (int i = 0; i < kTaps; ++i)
            {
                delayNow[i] += delayStep[i];
                if (gl[i] != 0.0f || gr[i] != 0.0f)
                {
                    tapL += gl[i] * line[0].read (delayNow[i]);
                    tapR += gr[i] * line[1].read (delayNow[i]);
                }
            }
            // the feedback's source: tap 0 (Length itself), with the channels mixed a little at high Space
            float src[2] = {line[0].read (delayNow[0]), line[1].read (delayNow[0])};
            const float mixed[2] = {cs * src[0] + sn * src[1], cs * src[1] - sn * src[0]};
            const float tapOut[2] = {tapL, tapR};
            const float gain = (float)kLoopGain * std::fabs (regen);
            for (int c = 0; c < 2; ++c)
            {
                // the input, diffused as much as Space asks, and the feedback, into the line
                const float x = io[c][a + j];
                float d = x;
                for (int k = 0; k < 4; ++k)
                    d = inAp[c][k].tick (d, inD[c][k], kInG);
                line[c].write (x + s * (d - x) + fb[c]);

                // the wet signal: the taps, diffused as much as Space asks
                float w = tapOut[c], wd = w;
                for (int k = 0; k < 4; ++k)
                    wd = outAp[c][k].tick (wd, outD[c][k], kOutG);
                io[c][a + j] = w + s * (wd - w);

                // the loop: diffusion, damping, the shifter, Regen, DC blocker, soft clip
                float f = mixed[c], fd = f;
                for (int k = 0; k < 2; ++k)
                    fd = loopAp[c][k].tick (fd, loopD[c][k], kLoopG);
                f += s * (fd - f);
                damp[c] += (f - damp[c]) * dampCoef;
                double unshifted = 0.0;
                const double shifted = shifter[c].tick (damp[c], unshifted);
                const float back = (float)(regen >= 0.0f ? shifted : 0.5 * (shifted + unshifted)) * gain;
                const float hp = back - dcX[c] + dcCoef * dcY[c];
                dcX[c] = back;
                dcY[c] = hp;
                fb[c] = dsp::softClip (hp);
            }
        }
    }
}

} // namespace ciphr

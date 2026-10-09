#include "ParaSplit.h"

#include <algorithm>
#include <cmath>

namespace moistr {

namespace {
// glides x towards t by c, landing on it once close
inline void glide (double& x, double t, double c)
{
    x += (t - x) * c;
    if (std::fabs (t - x) < 1e-9)
        x = t;
}
inline double clampHz (double hz, double sr) { return std::clamp (hz, 10.0, 0.45 * sr); }
} // namespace

void ParaSplit::prepare (double sampleRate)
{
    sr = sampleRate;
    tickSmooth = 1.0 - std::exp (-(double)kMaxTick / (0.03 * sr));
    fadeStep = (double)kMaxTick / (0.02 * sr);
}

ParaSplit::Shape ParaSplit::shapeAt (const double* p, double ph)
{
    const double w = 2.0 * dsp::kPi * (ph - std::floor (ph));
    Shape s;
    s.lpLevel = 1.0 - std::clamp (p[kParaLpMove], 0.0, 1.0) * (0.5 - 0.5 * std::cos (w));
    s.hpPos = 0.5 - 0.5 * std::cos (w + 0.5 * dsp::kPi);
    s.hpLevel = 1.0 - std::clamp (p[kParaHpLevelMove], 0.0, 1.0) * (0.5 + 0.5 * std::cos (w));
    return s;
}

void ParaSplit::reset (const double* p, double phase)
{
    fade = p[kParaOn] >= 0.5 ? 1.0 : 0.0;
    guardFade = p[kSubGuard] >= 0.5 ? 1.0 : 0.0;
    for (int c = 0; c < 2; ++c)
    {
        lp1[c].reset ();
        lp2[c].reset ();
        hp1[c].reset ();
        hp2[c].reset ();
        guardLp[c].reset ();
        guardHp[c].reset ();
    }
    targets (p, shapeAt (p, phase), std::clamp (p[kSubGuardFreq], kGuardFreqMin, kGuardFreqMax), true);
}

void ParaSplit::targets (const double* p, const Shape& to, double guardHz, bool snap)
{
    const double lpT = std::log2 (std::clamp (p[kParaLpFreq], kParaLpMin, kParaLpMax));
    const double hpT = std::log2 (std::clamp (p[kParaHpFreq], kParaHpMin, kParaHpMax));
    const double moveT = std::clamp (p[kParaHpMove], 0.0, kParaHpMoveMax), mixT = std::clamp (p[kParaMix], 0.0, 1.0);
    const double guardT = std::log2 (std::clamp (guardHz, kGuardFreqMin, kGuardFreqMax));
    if (snap)
    {
        logLp = lpT;
        logHp = hpT;
        hpMove = moveT;
        mix = mixT;
        logGuard = guardT;
    }
    else
    {
        glide (logLp, lpT, tickSmooth);
        glide (logHp, hpT, tickSmooth);
        glide (hpMove, moveT, tickSmooth);
        glide (mix, mixT, tickSmooth);
        glide (logGuard, guardT, tickSmooth);
    }
    lpGPrev = lpGNow;
    hpGPrev = hpGNow;
    guardGPrev = guardGNow;
    lpPrev = lpNow;
    hpPrev = hpNow;
    mixPrev = mixNow;
    lpGNow = std::tan (dsp::kPi * clampHz (std::exp2 (logLp), sr) / sr);
    hpHzNow = clampHz (std::exp2 (logHp + hpMove * std::clamp (to.hpPos, 0.0, 1.0)), sr);
    hpGNow = std::tan (dsp::kPi * hpHzNow / sr);
    guardGNow = std::tan (dsp::kPi * clampHz (std::exp2 (logGuard), sr) / sr);
    lpNow = std::clamp (to.lpLevel, 0.0, 1.0);
    hpNow = std::clamp (to.hpLevel, 0.0, 1.0);
    mixNow = mix;
    if (snap)
    {
        lpGPrev = lpGNow;
        hpGPrev = hpGNow;
        guardGPrev = guardGNow;
        lpPrev = lpNow;
        hpPrev = hpNow;
        mixPrev = mixNow;
    }
}

void ParaSplit::tick (const double* p, double* l, double* r, int m, const Shape& to, double guardHz, bool guard)
{
    const bool on = p[kParaOn] >= 0.5;
    if (!on && fade <= 0.0)
        return;
    targets (p, to, guardHz, false);
    const double fade0 = fade, guard0 = guardFade;
    fade = std::clamp (fade + (on ? fadeStep : -fadeStep) * m / kMaxTick, 0.0, 1.0);
    guardFade = std::clamp (guardFade + (guard ? fadeStep : -fadeStep) * m / kMaxTick, 0.0, 1.0);
    const bool guarding = guardFade > 0.0 || guard0 > 0.0;
    for (int i = 0; i < m; ++i)
    {
        const double t = (double)(i + 1) / m;
        dsp::SvfCoefs cl, ch, cg[2];
        cl.set (lpGPrev + (lpGNow - lpGPrev) * t, dsp::kSqrt2);
        ch.set (hpGPrev + (hpGNow - hpGPrev) * t, dsp::kSqrt2);
        if (guarding)
            dsp::Lr8Split::coefs (guardGPrev + (guardGNow - guardGPrev) * t, cg);
        const double gl = lpPrev + (lpNow - lpPrev) * t, gh = hpPrev + (hpNow - hpPrev) * t, wet = mixPrev + (mixNow - mixPrev) * t;
        const double f = fade0 + (fade - fade0) * t, gf = guard0 + (guardFade - guard0) * t;
        double* io[2] = {l + i, r + i};
        for (int c = 0; c < 2; ++c)
        {
            const double x = *io[c];
            const double lo = lp2[c].tick (lp1[c].tick (x, cl).lp, cl).lp;
            const double hi = hp2[c].tick (hp1[c].tick (x, ch).hp, ch).hp;
            double lpOut = gl * lo, hpOut = gh * hi;
            if (guarding)
            {
                // (the low-pass path's lows at 0 dB, its level moving above the guard; the high-pass path without its lows)
                double ls, lh, hs, hh;
                guardLp[c].tick (lo, cg, ls, lh);
                guardHp[c].tick (hi, cg, hs, hh);
                lpOut += (ls + gl * lh - lpOut) * gf;
                hpOut += (gh * hh - hpOut) * gf;
            }
            const double y = x + (lpOut + hpOut - x) * wet;
            *io[c] = x + (y - x) * f;
        }
    }
    if (guardFade <= 0.0 && guard0 > 0.0)
        for (int c = 0; c < 2; ++c)
        {
            guardLp[c].reset ();
            guardHp[c].reset ();
        }
    if (fade <= 0.0)
        for (int c = 0; c < 2; ++c)
        {
            lp1[c].reset ();
            lp2[c].reset ();
            hp1[c].reset ();
            hp2[c].reset ();
            guardLp[c].reset ();
            guardHp[c].reset ();
        }
}

} // namespace moistr

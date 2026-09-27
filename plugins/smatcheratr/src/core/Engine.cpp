#include "Engine.h"

#include "Color.h"

#include <algorithm>
#include <cmath>

namespace smatcheratr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
constexpr double kDcHz = 10.0;
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

void Engine::Channel::reset ()
{
    dc.reset ();
    preLo.reset ();
    preHi.reset ();
    postLo.reset ();
    postHi.reset ();
    os.reset ();
    dryDelay.reset ();
    wetDelay.reset ();
}

void Engine::prepare (double sampleRate, int mb)
{
    sr = sampleRate;
    maxBlock = std::max (1, mb);
    for (auto& c : chan)
    {
        c.os.prepare (sr, maxBlock);
        c.dc.c = highPass (sr, kDcHz, M_SQRT1_2);
    }
    const int lat = latency ();
    for (auto& c : chan)
    {
        c.dryDelay.resize (lat);
        c.wetDelay.resize (lat);
    }
    for (auto* v : {&dry, &pre, &wet, &gDrive, &gOut, &gMix})
        v->assign ((size_t)maxBlock, 0.0f);
    osBuf.assign ((size_t)maxBlock * 4, 0.0f);
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    updateFilters (true);
    reset ();
}

void Engine::reset ()
{
    for (auto& c : chan)
        c.reset ();
    drive = dbToGain (p[kDrive]);
    out = dbToGain (p[kOutput]);
    mix = (float)std::clamp (p[kDryWet], 0.0, 1.0);
}

ShaperSettings Engine::shaperSettings () const
{
    ShaperSettings s;
    s.curve = std::clamp ((int)std::lround (p[kCurve]), 0, kNumCurves - 1);
    s.bassThresholdDb = p[kBassThreshold];
    s.ws.drive = p[kWsDrive];
    s.ws.curve = p[kWsCurve];
    s.ws.depth = p[kWsDepth];
    s.ws.linear = p[kWsLinear];
    s.ws.damp = p[kWsDamp];
    s.ws.period = p[kWsPeriod];
    return s;
}

void Engine::updateFilters (bool force)
{
    const double rate = p[kHiQuality] >= 0.5 ? 4.0 * sr : sr;
    const double lo = colorDb (p[kColorLo]), hi = colorDb (p[kColorHi]), freq = p[kColorFreq], width = p[kColorWidth];
    if (!force && lo == cLo && hi == cHi && freq == cFreq && width == cWidth && rate == cRate)
        return;
    cLo = lo;
    cHi = hi;
    cFreq = freq;
    cWidth = width;
    cRate = rate;
    const double f = colorPeakHz (freq, rate), q = colorQ (width);
    const BiquadCoeffs preLo = lowShelf (rate, kColorLowHz, lo), postLo = lowShelf (rate, kColorLowHz, -lo);
    const BiquadCoeffs preHi = peak (rate, f, hi, q), postHi = peak (rate, f, -hi, q);
    for (auto& c : chan)
    {
        c.preLo.c = preLo;
        c.postLo.c = postLo;
        c.preHi.c = preHi;
        c.postHi.c = postHi;
    }
}

float Engine::shapeChain (Channel& c, float v, const ShaperSettings& s, bool color, int post) const
{
    double d = v;
    if (color)
    {
        d = c.preLo.process (d);
        d = c.preHi.process (d);
    }
    d = shape (d, s);
    if (color)
    {
        d = c.postLo.process (d);
        d = c.postHi.process (d);
    }
    if (post == kPostSoft)
        d = analogClip (d);
    else if (post == kPostHard)
        d = digitalClip (d);
    return (float)d;
}

void Engine::process (const float* xl, const float* xr, float* yl, float* yr, int n)
{
    if (n > maxBlock)
    {
        for (int pos = 0; pos < n; pos += maxBlock)
        {
            const int m = std::min (maxBlock, n - pos);
            process (xl + pos, xr + pos, yl + pos, yr + pos, m);
        }
        return;
    }
    updateFilters (false);
    const ShaperSettings s = shaperSettings ();
    const bool hiq = p[kHiQuality] >= 0.5, color = p[kColorOn] >= 0.5, dc = p[kDcFilter] >= 0.5;
    const int post = std::clamp ((int)std::lround (p[kPostClip]), (int)kPostOff, (int)kPostHard);

    // per-sample smoothed gains, shared by both channels
    const float driveT = dbToGain (p[kDrive]), outT = dbToGain (p[kOutput]);
    const float mixT = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    for (int i = 0; i < n; ++i)
    {
        drive += (driveT - drive) * smooth;
        out += (outT - out) * smooth;
        mix += (mixT - mix) * smooth;
        gDrive[(size_t)i] = drive;
        gOut[(size_t)i] = out;
        gMix[(size_t)i] = mix;
    }

    float inPk = 0.0f, outPk = 0.0f;
    const float* ins[2] = {xl, xr};
    float* outs[2] = {yl, yr};
    for (int c = 0; c < 2; ++c)
    {
        Channel& ch = chan[c];
        const float* x = ins[c];
        float* y = outs[c];
        for (int i = 0; i < n; ++i)
        {
            float v = x[i];
            dry[(size_t)i] = ch.dryDelay.push (v);
            if (dc)
                v = (float)ch.dc.process (v);
            v *= gDrive[(size_t)i];
            inPk = std::max (inPk, std::fabs (v));
            pre[(size_t)i] = v;
        }
        if (hiq)
        {
            ch.os.up (pre.data (), osBuf.data (), n);
            for (int i = 0; i < 4 * n; ++i)
                osBuf[(size_t)i] = shapeChain (ch, osBuf[(size_t)i], s, color, post);
            ch.os.down (osBuf.data (), wet.data (), n);
        }
        else
            for (int i = 0; i < n; ++i)
                wet[(size_t)i] = ch.wetDelay.push (shapeChain (ch, pre[(size_t)i], s, color, post));
        for (int i = 0; i < n; ++i)
        {
            const float w = wet[(size_t)i], m = gMix[(size_t)i];
            outPk = std::max (outPk, std::fabs (w));
            y[i] = (dry[(size_t)i] * (1.0f - m) + w * m) * gOut[(size_t)i];
        }
    }
    if (meters)
    {
        meters->inPeak.store (inPk, std::memory_order_relaxed);
        meters->outPeak.store (outPk, std::memory_order_relaxed);
    }
}

} // namespace smatcheratr

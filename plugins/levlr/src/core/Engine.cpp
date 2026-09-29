#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define LEVLR_SSE 1
#endif

namespace levlr {

namespace {
inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
constexpr int kCoeffEvery = 16; // samples between crossover retunings while they glide

// Flushes denormals to zero while alive (as Widr does): the crossovers' filters decay towards them
// after the audio stops, and they are slow on x86.
class NoDenormals
{
public:
    NoDenormals ()
    {
#if defined(LEVLR_SSE)
        old = _mm_getcsr ();
        _mm_setcsr ((unsigned int)(old | 0x8040)); // FTZ | DAZ
#elif defined(__aarch64__) && !defined(_MSC_VER)
        uint64_t fpcr;
        asm volatile ("mrs %0, fpcr" : "=r"(fpcr));
        old = fpcr;
        fpcr |= (uint64_t)1 << 24; // FZ
        asm volatile ("msr fpcr, %0" : : "r"(fpcr));
#endif
    }
    ~NoDenormals ()
    {
#if defined(LEVLR_SSE)
        _mm_setcsr ((unsigned int)old);
#elif defined(__aarch64__) && !defined(_MSC_VER)
        asm volatile ("msr fpcr, %0" : : "r"(old));
#endif
    }
    NoDenormals (const NoDenormals&) = delete;
    NoDenormals& operator= (const NoDenormals&) = delete;

private:
    uint64_t old = 0;
};
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

void effectiveCrossovers (const double set[kCrossovers], double sampleRate, double out[kCrossovers])
{
    const double gap = std::pow (2.0, kMinGapOct);
    const double top = std::min (kMaxXoverHz, 0.45 * sampleRate);
    double lowest = kMinXoverHz;
    for (int k = 0; k < kCrossovers; ++k)
    {
        // room for the crossovers above this one
        const double highest = top / std::pow (gap, (double)(kCrossovers - 1 - k));
        const double v = std::isfinite (set[k]) ? set[k] : lowest;
        out[k] = std::clamp (v, std::min (lowest, highest), highest);
        lowest = out[k] * gap;
    }
}

std::complex<double> bandResponse (int band, const double xover[kCrossovers], int slope, double f, double sampleRate)
{
    SplitResponse s[kCrossovers];
    for (int k = 0; k < kCrossovers; ++k)
        s[k] = splitResponse (xover[k], slope, f, sampleRate);
    // the tree: band 1 = low of 1 (through the all-passes of 2 and 3), band 2 = high of 1, low of 2
    // (through 3's), band 3 = high of 1 and 2, low of 3, band 4 = the highs of all three
    switch (band)
    {
        case 0: return s[0].low * s[1].allpass * s[2].allpass;
        case 1: return s[0].high * s[1].low * s[2].allpass;
        case 2: return s[0].high * s[1].high * s[2].low;
        default: return s[0].high * s[1].high * s[2].high;
    }
}

std::complex<double> totalResponse (const double xover[kCrossovers], int slope, const double gains[kBands], double f,
                                    double sampleRate)
{
    std::complex<double> h (0.0, 0.0);
    for (int b = 0; b < kBands; ++b)
        if (gains[b] != 0.0)
            h += gains[b] * bandResponse (b, xover, slope, f, sampleRate);
    return h;
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    tail.prepare (sr, maxBlock);
    for (uint32_t id = 0; id < kNumParams; ++id)
        if (isTailParam (id))
            tail.setParam (tailField (id), p[id]);
    smoothGain = 1.0 - std::exp (-1.0 / (0.005 * sr));              // 5 ms on the band levels
    glide = 1.0 - std::exp (-1.0 / (0.015 * sr / kCoeffEvery));     // 15 ms on the crossovers, per retuning
    duckStep = (float)(1.0 / (0.004 * sr));                          // 4 ms out, 4 ms back in
    if (meters)
        meters->sampleRate.store ((float)sr);
    reset ();
}

void Engine::resetFilters ()
{
    for (auto& s : split)
        s.reset ();
    for (auto& a : apLow)
        a.reset ();
    apMid.reset ();
}

void Engine::retune ()
{
    float g[kCrossovers];
    for (int k = 0; k < kCrossovers; ++k)
    {
        g[k] = cornerG (xfNow[k], sr);
        split[k].setup (g[k], slopeNow);
    }
    apLow[0].setup (g[1], slopeNow);
    apLow[1].setup (g[2], slopeNow);
    apMid.setup (g[2], slopeNow);
}

void Engine::reset ()
{
    double set[kCrossovers];
    for (int k = 0; k < kCrossovers; ++k)
        set[k] = p[xoverParam (k)];
    effectiveCrossovers (set, sr, xfNow);
    slopeNow = std::clamp ((int)std::lround (p[kSlope]), 0, kNumSlopes - 1);
    resetFilters ();
    retune ();
    double g[kBands];
    bandGains ([this] (uint32_t id) { return p[id]; }, g);
    for (int b = 0; b < kBands; ++b)
        gain[b] = (float)g[b];
    out = (float)dbToGain (p[kOutput]);
    duck = 1.0f;
    coeffCountdown = 0;
    tail.reset ();
}

void Engine::setParam (uint32_t id, double plain)
{
    if (id >= kNumParams)
        return;
    p[id] = plain;
    if (isTailParam (id))
        tail.setParam (tailField (id), plain);
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const NoDenormals guard;
    // in pieces the size of the analyser's input copy
    for (int pos = 0; pos < n; pos += kChunk)
    {
        const int m = std::min (kChunk, n - pos);
        processChunk (inL + pos, inR + pos, outL + pos, outR + pos, m);
    }
}

void Engine::processChunk (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    // this block's targets
    double set[kCrossovers], xfT[kCrossovers], gT[kBands];
    for (int k = 0; k < kCrossovers; ++k)
        set[k] = p[xoverParam (k)];
    effectiveCrossovers (set, sr, xfT);
    bandGains ([this] (uint32_t id) { return p[id]; }, gT);
    float gainT[kBands];
    for (int b = 0; b < kBands; ++b)
        gainT[b] = (float)gT[b];
    const float outT = (float)dbToGain (p[kOutput]);
    const int slopeT = std::clamp ((int)std::lround (p[kSlope]), 0, kNumSlopes - 1);
    const float sg = (float)smoothGain;

    for (int i = 0; i < n; ++i)
    {
        // a new slope: fade out, swap the filters (from silence), fade back in
        if (slopeT != slopeNow)
        {
            duck -= duckStep;
            if (duck <= 0.0f)
            {
                duck = 0.0f;
                slopeNow = slopeT;
                resetFilters ();
                retune ();
            }
        }
        else if (duck < 1.0f)
            duck = std::min (1.0f, duck + duckStep);

        // the crossovers glide (in octaves) to where they are set
        if (--coeffCountdown < 0)
        {
            coeffCountdown = kCoeffEvery - 1;
            bool moved = false;
            for (int k = 0; k < kCrossovers; ++k)
            {
                const double ratio = xfT[k] / xfNow[k];
                if (std::fabs (ratio - 1.0) > 1e-4)
                {
                    xfNow[k] *= std::pow (ratio, glide);
                    moved = true;
                }
                else if (xfNow[k] != xfT[k])
                {
                    xfNow[k] = xfT[k];
                    moved = true;
                }
            }
            if (moved)
                retune ();
        }

        for (int b = 0; b < kBands; ++b)
            gain[b] += (gainT[b] - gain[b]) * sg;
        out += (outT - out) * sg;
        const float level = out * duck;

        const float x[2] = {inL[i], inR[i]};
        float y[2];
        for (int c = 0; c < 2; ++c)
        {
            float b0, b1, b2, b3, rest, rest2;
            split[0].tick (x[c], c, b0, rest);
            split[1].tick (rest, c, b1, rest2);
            split[2].tick (rest2, c, b2, b3);
            b0 = apLow[1].tick (apLow[0].tick (b0, c), c);
            b1 = apMid.tick (b1, c);
            y[c] = (gain[0] * b0 + gain[1] * b1 + gain[2] * b2 + gain[3] * b3) * level;
        }
        inMono[i] = 0.5f * (x[0] + x[1]); // (the input may be the output buffer)
        outL[i] = y[0];
        outR[i] = y[1];
    }
    if (hasTail)
        tail.process (outL, outR, n);

    if (meters)
    {
        // the analyser: the input, and the output after the tail
        for (int i = 0; i < n; ++i)
            meters->scope.push (inMono[i], 0.5f * (outL[i] + outR[i]));
        for (int k = 0; k < kCrossovers; ++k)
            meters->xover[k].store ((float)xfNow[k], std::memory_order_relaxed);
        meters->blocks.fetch_add (1, std::memory_order_relaxed);
    }
}

} // namespace levlr

#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define SMOOTHR_SSE 1
#endif

namespace smoothr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }

// Flushes denormals to zero while alive (as Levlr does): the filters decay towards them after the
// audio stops, and they are slow on x86.
class NoDenormals
{
public:
    NoDenormals ()
    {
#if defined(SMOOTHR_SSE)
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
#if defined(SMOOTHR_SSE)
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

// the loudest of a peak kept for the editor and a new one (the editor swaps in 0 when it takes it)
inline void raise (std::atomic<float>& a, float v)
{
    if (v > a.load (std::memory_order_relaxed))
        a.store (v, std::memory_order_relaxed);
}
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    tail.prepare (sr, kChunk);
    for (uint32_t id = 0; id < kNumParams; ++id)
        if (isTailParam (id))
            tail.setParam (tailField (id), p[id]);
    dip.prepare (sr);
    dip.setAmount (p[kCharacter]);
    limiter.prepare (sr, kChunk);
    applyLimiterParams ();
    smoothGain = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    colLen = std::max (1, (int)std::lround (sr / Meters::kColumnHz));
    if (meters)
        meters->sampleRate.store ((float)sr);
    bypL.assign ((size_t)kBypassSize, 0.0f);
    bypR.assign ((size_t)kBypassSize, 0.0f);
    reset ();
}

void Engine::reset ()
{
    std::fill (bypL.begin (), bypL.end (), 0.0f);
    std::fill (bypR.begin (), bypR.end (), 0.0f);
    bypPos = 0;
    tail.reset ();
    dip.reset ();
    limiter.reset ();
    inGain = dbToGain (p[kInput]);
    colFill = 0;
    colIn = colOut = 0.0f;
    colLow = colHigh = 1.0f;
}

void Engine::applyLimiterParams ()
{
    limiter.setCeilingDb (p[kCeiling]);
    limiter.setRelease (p[kRelease], p[kAutoRelease] >= 0.5);
    limiter.setSmooth (p[kSmooth]);
}

void Engine::setParam (uint32_t id, double plain)
{
    if (id >= kNumParams)
        return;
    p[id] = plain;
    if (isTailParam (id))
        tail.setParam (tailField (id), plain);
    else if (id == kCharacter)
        dip.setAmount (plain);
    else if (id == kCeiling || id == kRelease || id == kAutoRelease || id == kSmooth)
        applyLimiterParams ();
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const NoDenormals guard;
    for (int pos = 0; pos < n; pos += kChunk)
    {
        const int m = std::min (kChunk, n - pos);
        processChunk (inL + pos, inR + pos, outL + pos, outR + pos, m);
    }
    if (meters)
        meters->blocks.fetch_add (1, std::memory_order_relaxed);
}

void Engine::processBypassed (float* L, float* R, int n)
{
    if (bypL.empty ())
        return;
    const int d = std::clamp (latency (), 0, kBypassSize - 1);
    const int mask = kBypassSize - 1;
    for (int i = 0; i < n; ++i)
    {
        bypL[(size_t)bypPos] = L[i];
        bypR[(size_t)bypPos] = R[i];
        const int r = (bypPos - d) & mask;
        L[i] = bypL[(size_t)r];
        R[i] = bypR[(size_t)r];
        bypPos = (bypPos + 1) & mask;
    }
}

void Engine::processChunk (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    // Input (a copy first: the input may be the output buffer)
    const float inT = dbToGain (p[kInput]);
    for (int i = 0; i < n; ++i)
    {
        inGain += (inT - inGain) * smoothGain;
        bufL[i] = inL[i] * inGain;
        bufR[i] = inR[i] * inGain;
    }
    // the saturator, then Character's dip, then the limiter
    tail.process (bufL, bufR, n);
    dip.process (bufL, bufR, n);
    if (meters)
    {
        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            pl = std::max (pl, std::fabs (bufL[i]));
            pr = std::max (pr, std::fabs (bufR[i]));
        }
        raise (meters->inPeak[0], pl);
        raise (meters->inPeak[1], pr);
    }
    limiter.process (bufL, bufR, n);
    std::copy (bufL, bufL + n, outL);
    std::copy (bufR, bufR + n, outR);

    if (meters)
    {
        // the history: per column, the input and output peaks and the most each band's gain came down
        const float* gl = limiter.lowGain ();
        const float* gh = limiter.highGain ();
        const float* lvl = limiter.inLevel ();
        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float o = std::max (std::fabs (outL[i]), std::fabs (outR[i]));
            pl = std::max (pl, std::fabs (outL[i]));
            pr = std::max (pr, std::fabs (outR[i]));
            colIn = std::max (colIn, lvl[i]);
            colOut = std::max (colOut, o);
            colLow = std::min (colLow, gl[i]);
            colHigh = std::min (colHigh, gh[i]);
            if (++colFill >= colLen)
            {
                auto db = [] (float g) { return g >= 1.0f ? 0.0f : (float)(-20.0 * std::log10 (std::max (g, 1e-6f))); };
                meters->level.push (colIn, colOut);
                meters->gr.push (db (colLow), db (colHigh));
                colFill = 0;
                colIn = colOut = 0.0f;
                colLow = colHigh = 1.0f;
            }
        }
        raise (meters->outPeak[0], pl);
        raise (meters->outPeak[1], pr);
        meters->dipDb.store (dip.cutDb (), std::memory_order_relaxed);
    }
}

} // namespace smoothr

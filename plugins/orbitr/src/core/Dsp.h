// Small DSP pieces Orbitr uses (from Detonatr's): a denormal guard, a fixed delay and a 4-point
// Hermite read.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define ORBITR_SSE 1
#endif

namespace orbitr::dsp {

constexpr double kPi = 3.14159265358979323846;

// Flushes denormals to zero while alive (as Gently and Levlr do): the delay lines and the ramps
// decay towards them after the audio stops, and they are slow on x86.
class NoDenormals
{
public:
    NoDenormals ()
    {
#if defined(ORBITR_SSE)
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
#if defined(ORBITR_SSE)
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

// a fixed delay of two channels' samples (0 samples: passes through)
struct Delay
{
    std::vector<float> buf[2];
    int len = 0, pos = 0;
    void prepare (int samples)
    {
        len = std::max (0, samples);
        for (auto& b : buf)
            b.assign ((size_t)std::max (1, len), 0.0f);
        pos = 0;
    }
    void reset ()
    {
        for (auto& b : buf)
            std::fill (b.begin (), b.end (), 0.0f);
        pos = 0;
    }
    // one sample of both channels, in place
    inline void tick (float& a, float& b)
    {
        if (len == 0)
            return;
        const float x = buf[0][(size_t)pos], y = buf[1][(size_t)pos];
        buf[0][(size_t)pos] = a;
        buf[1][(size_t)pos] = b;
        a = x;
        b = y;
        if (++pos >= len)
            pos = 0;
    }
};

// 4-point Hermite (Catmull-Rom) interpolation between y0 and y1 at t (0 .. 1); ym1, y2 the neighbours
inline float hermite (float ym1, float y0, float y1, float y2, float t)
{
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}

} // namespace orbitr::dsp

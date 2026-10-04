// Flushes denormals to zero while alive (as Gentlr, Levlr and Orbitr do in their own copies): filters
// and delay lines decay towards them once the audio stops, and on x86 every operation on one is many
// times slower (a stopped track could cost several cores). Hosts usually set this on their audio
// threads, but not all do. It changes nothing but values below 1.2e-38 (they become 0).
#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define PK_NODENORMALS_SSE 1
#endif

namespace pk {

class NoDenormals
{
public:
    NoDenormals ()
    {
#if defined(PK_NODENORMALS_SSE)
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
#if defined(PK_NODENORMALS_SSE)
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

} // namespace pk

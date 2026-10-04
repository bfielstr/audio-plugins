// Four floats in one SIMD register (SSE on x86, NEON on ARM, plain floats elsewhere), with only the
// element-wise operations: each lane gets exactly the IEEE result of the same scalar operation (no
// fused multiply-adds, no reordering), so code written with it gives each lane what the scalar code
// it replaces gives, to the bit.
#pragma once

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define PK_SIMD_SSE 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>
#define PK_SIMD_NEON 1
#endif

namespace pk {

struct F4
{
#if defined(PK_SIMD_SSE)
    __m128 v;
    static F4 load (const float* p) { return {_mm_loadu_ps (p)}; }
    static F4 set1 (float x) { return {_mm_set1_ps (x)}; }
    void store (float* p) const { _mm_storeu_ps (p, v); }
    friend F4 operator+ (F4 a, F4 b) { return {_mm_add_ps (a.v, b.v)}; }
    friend F4 operator- (F4 a, F4 b) { return {_mm_sub_ps (a.v, b.v)}; }
    friend F4 operator* (F4 a, F4 b) { return {_mm_mul_ps (a.v, b.v)}; }
#elif defined(PK_SIMD_NEON)
    float32x4_t v;
    static F4 load (const float* p) { return {vld1q_f32 (p)}; }
    static F4 set1 (float x) { return {vdupq_n_f32 (x)}; }
    void store (float* p) const { vst1q_f32 (p, v); }
    friend F4 operator+ (F4 a, F4 b) { return {vaddq_f32 (a.v, b.v)}; }
    friend F4 operator- (F4 a, F4 b) { return {vsubq_f32 (a.v, b.v)}; }
    friend F4 operator* (F4 a, F4 b) { return {vmulq_f32 (a.v, b.v)}; }
#else
    float v[4];
    static F4 load (const float* p) { return {{p[0], p[1], p[2], p[3]}}; }
    static F4 set1 (float x) { return {{x, x, x, x}}; }
    void store (float* p) const
    {
        for (int j = 0; j < 4; ++j)
            p[j] = v[j];
    }
    friend F4 operator+ (F4 a, F4 b) { return {{a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3]}}; }
    friend F4 operator- (F4 a, F4 b) { return {{a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3]}}; }
    friend F4 operator* (F4 a, F4 b) { return {{a.v[0] * b.v[0], a.v[1] * b.v[1], a.v[2] * b.v[2], a.v[3] * b.v[3]}}; }
#endif
};

} // namespace pk

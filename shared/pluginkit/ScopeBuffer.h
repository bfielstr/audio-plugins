// The most recent samples of two signals, written by the audio thread and read by an editor
// (oscilloscopes, spectra). Lock-free with a single writer: a reader may catch a block while it is
// being written, which only shows as a few fresh samples in a display.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace pk {

template <int N>
class ScopeBuffer
{
    static_assert (N > 0 && (N & (N - 1)) == 0, "ScopeBuffer size must be a power of two");

public:
    static constexpr int kSize = N;

    void push (float a, float b)
    {
        const uint32_t w = pos.load (std::memory_order_relaxed);
        chA[w & (N - 1)].store (a, std::memory_order_relaxed);
        chB[w & (N - 1)].store (b, std::memory_order_relaxed);
        pos.store (w + 1, std::memory_order_release);
    }

    // The last n samples, oldest first (either destination may be null). Returns how many real
    // samples that is: fewer than n until the buffer has been written that far.
    int read (float* a, float* b, int n) const
    {
        n = std::clamp (n, 0, N);
        const uint32_t w = pos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const uint32_t k = (w - (uint32_t)n + (uint32_t)i) & (uint32_t)(N - 1);
            if (a)
                a[i] = chA[k].load (std::memory_order_relaxed);
            if (b)
                b[i] = chB[k].load (std::memory_order_relaxed);
        }
        return (int)std::min<uint32_t> (w, (uint32_t)n);
    }

    // Total samples written (wraps); a change means new audio.
    uint32_t written () const { return pos.load (std::memory_order_acquire); }

private:
    std::array<std::atomic<float>, N> chA {}, chB {};
    std::atomic<uint32_t> pos {0};
};

} // namespace pk

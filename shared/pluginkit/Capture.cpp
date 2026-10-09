#include "pluginkit/Capture.h"

#include <algorithm>
#include <cmath>

namespace pk {

namespace {
constexpr uint64_t kMask = (uint64_t)CaptureBuffer::kFrames - 1;
constexpr uint64_t kPeakMask = (uint64_t)CaptureBuffer::kPeaks - 1;
} // namespace

struct CaptureBuffer::Storage
{
    std::unique_ptr<std::atomic<float>[]> ringL, ringR, peakMin, peakMax;
    Storage ()
    : ringL (new std::atomic<float>[(size_t)kFrames]), ringR (new std::atomic<float>[(size_t)kFrames]),
      peakMin (new std::atomic<float>[(size_t)kPeaks]), peakMax (new std::atomic<float>[(size_t)kPeaks])
    {
        for (int64_t i = 0; i < kFrames; ++i)
        {
            ringL[(size_t)i].store (0.0f, std::memory_order_relaxed);
            ringR[(size_t)i].store (0.0f, std::memory_order_relaxed);
        }
        for (int64_t i = 0; i < kPeaks; ++i)
        {
            peakMin[(size_t)i].store (0.0f, std::memory_order_relaxed);
            peakMax[(size_t)i].store (0.0f, std::memory_order_relaxed);
        }
    }
};

CaptureBuffer::CaptureBuffer () = default;

CaptureBuffer::~CaptureBuffer () { delete store.load (); }

void CaptureBuffer::enable () const
{
    if (store.load (std::memory_order_acquire))
        return;
    auto* s = new Storage ();
    Storage* none = nullptr;
    // (from the next whole peak block on: what the ring holds is real)
    const uint64_t now = written ();
    since.store (now == 0 ? 0 : (now / kPeakBlock + 2) * kPeakBlock, std::memory_order_relaxed);
    if (!store.compare_exchange_strong (none, s, std::memory_order_acq_rel))
        delete s;
}

void CaptureBuffer::push (const float* l, const float* r, int frames, const Transport& t, double sampleRate)
{
    const uint64_t start = pos.load (std::memory_order_relaxed);
    // the transport first (with the block's first frame), inside the sequence count
    seq.fetch_add (1, std::memory_order_acq_rel);
    tBpm.store (t.bpm, std::memory_order_relaxed);
    tPpq.store (t.ppq, std::memory_order_relaxed);
    tStart.store (start, std::memory_order_relaxed);
    tPlaying.store (t.playing, std::memory_order_relaxed);
    tPpqValid.store (t.ppqValid, std::memory_order_relaxed);
    seq.fetch_add (1, std::memory_order_acq_rel);
    if (sampleRate > 0)
        rate.store (sampleRate, std::memory_order_relaxed);
    Storage* st = store.load (std::memory_order_acquire);
    if (!st)
    {
        pos.store (start + (uint64_t)std::max (0, frames), std::memory_order_release);
        return;
    }
    auto& ringL = st->ringL;
    auto& ringR = st->ringR;
    auto& peakMin = st->peakMin;
    auto& peakMax = st->peakMax;
    uint64_t p = start;
    for (int i = 0; i < frames; ++i, ++p)
    {
        const float a = l ? l[i] : 0.0f, b = r ? r[i] : a;
        ringL[(size_t)(p & kMask)].store (a, std::memory_order_relaxed);
        ringR[(size_t)(p & kMask)].store (b, std::memory_order_relaxed);
        if ((p & (kPeakBlock - 1)) == 0)
            curMin = curMax = a;
        curMin = std::min ({curMin, a, b});
        curMax = std::max ({curMax, a, b});
        // the block's extremes so far (stored at its end, and at the end of the push for a partial one)
        if ((p & (kPeakBlock - 1)) == kPeakBlock - 1 || i == frames - 1)
        {
            const size_t k = (size_t)((p / kPeakBlock) & kPeakMask);
            peakMin[k].store (curMin, std::memory_order_relaxed);
            peakMax[k].store (curMax, std::memory_order_relaxed);
        }
    }
    pos.store (p, std::memory_order_release);
}

double CaptureBuffer::noteHz () const
{
    const int n = lastNote.load (std::memory_order_relaxed);
    return n < 0 ? 0.0 : 440.0 * std::pow (2.0, (n - 69) / 12.0);
}

CaptureBuffer::Window CaptureBuffer::window (int choice) const
{
    Window w;
    w.sampleRate = sampleRate ();
    w.length = lengthOf (choice);
    double bpm = 0, ppq = 0;
    uint64_t start = 0, now = 0;
    bool playing = false, ppqValid = false;
    for (int tries = 0; tries < 8; ++tries)
    {
        const uint32_t s0 = seq.load (std::memory_order_acquire);
        bpm = tBpm.load (std::memory_order_relaxed);
        ppq = tPpq.load (std::memory_order_relaxed);
        start = tStart.load (std::memory_order_relaxed);
        playing = tPlaying.load (std::memory_order_relaxed);
        ppqValid = tPpqValid.load (std::memory_order_relaxed);
        now = written ();
        if ((s0 & 1) == 0 && seq.load (std::memory_order_acquire) == s0)
            break;
    }
    w.end = now;
    if (playing && bpm > 0)
    {
        // whole bars (4/4) ending on the last bar line passed
        const double perBeat = w.sampleRate * 60.0 / bpm;
        w.bars = true;
        w.frames = (int64_t)std::llround (w.length * 4.0 * perBeat);
        if (ppqValid && now >= start)
        {
            const double ppqEnd = ppq + (double)(now - start) / perBeat;
            const double bar = std::floor (ppqEnd / 4.0 + 1e-9) * 4.0;
            const auto back = (uint64_t)std::max<int64_t> (0, std::llround ((ppqEnd - bar) * perBeat));
            w.end = now >= back ? now - back : 0;
        }
    }
    else
        w.frames = (int64_t)std::llround (w.length * w.sampleRate);
    w.frames = std::clamp<int64_t> (w.frames, 1, kMaxWindow);
    return w;
}

int64_t CaptureBuffer::read (uint64_t end, int64_t frames, float* l, float* r) const
{
    Storage* st = store.load (std::memory_order_acquire);
    const uint64_t now = written ();
    const uint64_t oldest = std::max (now > (uint64_t)kMaxWindow ? now - (uint64_t)kMaxWindow : 0, since.load (std::memory_order_relaxed));
    if (!st)
    {
        for (int64_t i = 0; i < frames; ++i)
        {
            if (l)
                l[i] = 0.0f;
            if (r)
                r[i] = 0.0f;
        }
        return 0;
    }
    auto& ringL = st->ringL;
    auto& ringR = st->ringR;
    int64_t real = 0;
    for (int64_t i = 0; i < frames; ++i)
    {
        const int64_t f = (int64_t)end - frames + i;
        const bool have = f >= (int64_t)oldest && f < (int64_t)now;
        const size_t k = (size_t)((uint64_t)f & kMask);
        if (l)
            l[i] = have ? ringL[k].load (std::memory_order_relaxed) : 0.0f;
        if (r)
            r[i] = have ? ringR[k].load (std::memory_order_relaxed) : 0.0f;
        real += have ? 1 : 0;
    }
    return real;
}

void CaptureBuffer::readPeaks (uint64_t end, int64_t frames, int columns, float* mn, float* mx) const
{
    Storage* st = store.load (std::memory_order_acquire);
    const uint64_t now = written ();
    const uint64_t oldest = std::max (now > (uint64_t)kMaxWindow ? now - (uint64_t)kMaxWindow : 0, since.load (std::memory_order_relaxed));
    if (!st)
    {
        std::fill (mn, mn + columns, 0.0f);
        std::fill (mx, mx + columns, 0.0f);
        return;
    }
    auto& peakMin = st->peakMin;
    auto& peakMax = st->peakMax;
    const double from = (double)end - (double)frames;
    for (int c = 0; c < columns; ++c)
    {
        const double a = from + (double)frames * c / columns, b = from + (double)frames * (c + 1) / columns;
        int64_t b0 = (int64_t)std::floor (a / kPeakBlock), b1 = (int64_t)std::ceil (b / kPeakBlock);
        b1 = std::max (b1, b0 + 1);
        float lo = 0, hi = 0;
        bool any = false;
        for (int64_t k = b0; k < b1; ++k)
        {
            const int64_t f = k * kPeakBlock;
            if (f + kPeakBlock <= (int64_t)oldest || f >= (int64_t)now || f < 0)
                continue;
            const float m0 = peakMin[(size_t)((uint64_t)k & kPeakMask)].load (std::memory_order_relaxed);
            const float m1 = peakMax[(size_t)((uint64_t)k & kPeakMask)].load (std::memory_order_relaxed);
            lo = any ? std::min (lo, m0) : m0;
            hi = any ? std::max (hi, m1) : m1;
            any = true;
        }
        mn[c] = lo;
        mx[c] = hi;
    }
}

} // namespace pk

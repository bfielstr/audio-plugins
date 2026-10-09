// The capture buffer behind a Basic page's scope band (pluginkit/ui/CaptureView.h): the last seconds of a
// plug-in's output, kept so the editor can show them, hold them (Freeze) and hand them out as a WAV or a
// wavetable (pluginkit/WavFile.h, pluginkit/Wavetable.h).
//
// The audio thread pushes every block with the host's transport (push); nothing is allocated there: the
// ring (kFrames stereo frames, about 21 s at 48 kHz, 5 s at 192 kHz; 8 MB) is made the first time an
// editor shows the buffer (enable), so an instance whose window never opens keeps none, and it stays until
// the buffer goes. Until then a push only counts its frames and keeps the transport. One writer,
// any number of readers, lock-free: a reader may catch the block being written, which only shows as a few
// fresh samples. Beside the samples the writer keeps each kPeakBlock frames' minimum and maximum (of both
// channels), so a display can draw any length of it without reading every sample.
//
// The length to capture (window): 1, 2 or 4 bars at the host's tempo while its transport plays (the
// window then ends on the last bar line passed, so a capture is whole bars from a downbeat), else 1, 2 or
// 4 seconds ending now. A window longer than the ring holds is cut to what it holds.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace pk {

class CaptureBuffer
{
public:
    static constexpr int64_t kFrames = int64_t (1) << 20;
    static constexpr int kPeakBlock = 256;
    static constexpr int64_t kPeaks = kFrames / kPeakBlock;
    // what a reader may ask for at most (a margin for the block being written)
    static constexpr int64_t kMaxWindow = kFrames - 16384;

    struct Transport
    {
        double bpm = 0;         // the host's tempo (0: none)
        bool playing = false;   // its transport runs
        double ppq = 0;         // the block's first frame in quarter notes (ppqValid)
        bool ppqValid = false;
    };

    CaptureBuffer ();
    ~CaptureBuffer ();
    CaptureBuffer (const CaptureBuffer&) = delete;
    CaptureBuffer& operator= (const CaptureBuffer&) = delete;
    // The ring, made if it is not yet (not on the audio thread: the editor calls it). Frames pushed before
    // read as 0.
    void enable () const;
    bool enabled () const { return store.load (std::memory_order_acquire) != nullptr; }

    // ---- the audio thread
    void push (const float* l, const float* r, int frames, const Transport& t, double sampleRate);
    // the last note played (an instrument): a wavetable's pitch when none is detected (Wavetable.h)
    void noteOn (int note) { lastNote.store (note, std::memory_order_relaxed); }

    // ---- readers
    uint64_t written () const { return pos.load (std::memory_order_acquire); }
    double sampleRate () const { return rate.load (std::memory_order_relaxed); }
    // the last note's frequency (0: none played)
    double noteHz () const;

    // The length to capture: choice 0, 1, 2 is 1, 2, 4 bars (playing, with a tempo) or seconds.
    struct Window
    {
        uint64_t end = 0;   // the frame after the last one (a count of frames written)
        int64_t frames = 0; // how many, ending there
        double sampleRate = 48000;
        bool bars = false;  // in bars (synced), else seconds
        double length = 1;  // 1, 2 or 4 (bars or seconds)
    };
    Window window (int choice) const;
    static double lengthOf (int choice) { return choice <= 0 ? 1.0 : choice == 1 ? 2.0 : 4.0; }

    // The `frames` frames ending at `end`, oldest first, into l and r (either may be null); frames not
    // written yet or no longer in the ring are 0. Returns how many were real.
    int64_t read (uint64_t end, int64_t frames, float* l, float* r) const;
    // The window drawn `columns` wide: each column's minimum and maximum (both channels).
    void readPeaks (uint64_t end, int64_t frames, int columns, float* mn, float* mx) const;

private:
    struct Storage;
    mutable std::atomic<Storage*> store {nullptr};
    mutable std::atomic<uint64_t> since {0}; // the first frame the ring holds
    std::atomic<uint64_t> pos {0};
    std::atomic<double> rate {48000.0};
    std::atomic<int> lastNote {-1};
    // the transport at the last block (a sequence count around it: a reader takes a consistent set)
    std::atomic<uint32_t> seq {0};
    std::atomic<double> tBpm {0}, tPpq {0};
    std::atomic<uint64_t> tStart {0};
    std::atomic<bool> tPlaying {false}, tPpqValid {false};
    // (the writer's own) the block of peaks being filled
    float curMin = 0, curMax = 0;
};

} // namespace pk

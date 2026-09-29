// State shared by the processor (audio thread), the editor (UI thread) and a background worker:
// the clip being edited with its undo history, parameter values, the latest render and audio
// capture. The processor owns it and hands a pointer to the controller through a connection
// message, so both components must run in one process (the plug-in is not distributable).
#pragma once

#include "Clip.h"
#include "Params.h"
#include "Render.h"

#include "pluginkit/RtShared.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace stretchr {

class Session
{
public:
    Session ();
    void retain () { refs.fetch_add (1); }
    void release ()
    {
        if (refs.fetch_sub (1) == 1)
            delete this;
    }

    // --- clip (non-realtime threads) -------------------------------------------------
    Clip clip () const;
    bool hasClip () const { return hasAudio.load (std::memory_order_acquire); }
    // Bumps on every change that needs a new render (audio, markers, pitch).
    uint64_t clipVersion () const { return version.load (std::memory_order_acquire); }
    // Bumps on every user-visible change (for marking the host project dirty).
    uint64_t changeCount () const { return changes.load (std::memory_order_acquire); }
    // Replaces the whole clip. undoable = false for project loads (also clears the history).
    void setClip (Clip c, bool undoable);
    // Changes markers / pitch / name. snapshot: record an undo step first.
    void edit (const std::function<void (Clip&)>& fn, bool snapshot = true);
    void pushUndo (); // record an undo step now (start of a drag), then edit (..., false)
    bool undo ();
    bool redo ();
    bool canUndo () const;
    bool canRedo () const;
    double clipStart () const { return start.load (std::memory_order_acquire); }
    void setClipStart (double seconds, bool snapshot = true);

    // --- parameters (plain values; written by the processor and the controller) ---------
    // wake = false from the audio thread (the worker also polls every 10 ms).
    void setParam (uint32_t id, double plain, bool wake = true);
    double param (uint32_t id) const { return params[id].load (std::memory_order_relaxed); }
    RenderSettings settings () const; // realtime safe

    // --- host (written by the audio thread) --------------------------------------------
    std::atomic<double> hostRate {0.0};
    std::atomic<double> hostBpm {120.0};
    std::atomic<double> transport {0.0}; // seconds
    std::atomic<bool> playing {false};
    std::atomic<double> playOffset {-1.0}; // On Play: seconds since playback started (else -1)
    void processBegin ();
    void processEnd () { inProcess.store (false, std::memory_order_release); }

    // --- rendering ---------------------------------------------------------------------
    RenderedPtr latest () { return renders.latest (); }
    bool fetch (RenderedPtr& local, uint32_t& gen) { return renders.fetch (local, gen); } // realtime
    uint64_t wantedKey () const;                                                         // realtime
    bool upToDate () const; // the published render matches the current clip + settings (realtime)
    // Blocks until upToDate() (offline processing only). Returns false on timeout.
    bool waitUntilRendered (double timeoutSeconds);
    std::atomic<float> progress {1.0f};
    std::atomic<bool> rendering {false};

    // --- capture -----------------------------------------------------------------------
    enum CaptureState { kIdle = 0, kArmed, kRecording, kFinishing };
    int captureState () const { return state.load (std::memory_order_acquire); }
    // UI: arm, disarm, or (while recording) stop and keep the recording.
    void setArmed (bool on);
    double capturedSeconds () const;
    // Capture buffers the worker has allocated so far (each holds kChunkFrames frames).
    int captureChunksReady () const { return chunksReady.load (std::memory_order_acquire); }
    // Audio thread, once per block: records while armed and the transport plays.
    void captureBlock (const float* l, const float* r, int n, long long projectSample, bool isPlaying,
                       double sampleRate);
    bool capturing () const
    {
        const int s = captureState ();
        return s == kArmed || s == kRecording;
    }

    static constexpr int kChunkFrames = 32768;
    static constexpr int kMaxChunks = 4096; // about 45 minutes at 48 kHz
    static constexpr int kMaxUndo = 50;

private:
    ~Session ();
    void run ();
    void manageCapture ();
    void finishCapture ();
    void renderIfNeeded (std::chrono::steady_clock::time_point now);
    void snapshotLocked ();
    void notifyWorker () { wake.notify_one (); }
    static uint64_t keyFor (uint64_t version, const RenderSettings& s, double rate);

    std::atomic<int> refs {1};

    mutable std::mutex clipMutex;
    Clip current;
    std::vector<Clip> undoStack, redoStack;
    std::atomic<uint64_t> version {1}, changes {0};
    std::atomic<double> start {0.0};
    std::atomic<bool> hasAudio {false};

    std::array<std::atomic<double>, kNumParams> params;

    pk::RtShared<Rendered> renders;
    std::atomic<uint64_t> publishedKey {0};
    AnalysisCache cache;
    uint64_t pendingKey = 0;
    std::chrono::steady_clock::time_point pendingSince, firstPending;
    std::atomic<bool> offlineWaiting {false};

    struct Chunk
    {
        float l[kChunkFrames];
        float r[kChunkFrames];
    };
    std::array<std::atomic<Chunk*>, kMaxChunks> chunks {};
    std::atomic<int> chunksReady {0};
    std::atomic<int> state {kIdle};
    std::atomic<bool> stopRequest {false};
    std::atomic<long long> capFrames {0}, capStartSample {0};
    std::atomic<double> capRate {48000.0};
    long long capNext = 0; // audio thread only
    std::atomic<bool> inProcess {false};
    std::atomic<long long> lastProcessMs {0};
    int captureCount = 0;

    std::mutex wakeMutex;
    std::condition_variable wake;
    std::atomic<bool> quit {false};
    std::thread worker;
};

} // namespace stretchr

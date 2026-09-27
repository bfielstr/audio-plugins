// Shared, in-process state between the processor (audio thread) and the controller/editor
// (UI thread): the loaded sample, slice edits, playhead positions and audition notes.
// The processor owns it and hands a pointer to the controller through a connection message,
// so both components must live in the same process (the plug-in is not distributable).
#pragma once

#include "SampleData.h"
#include "Slices.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace simplr {

// Publishes immutable objects to a realtime reader without ever freeing memory on the
// realtime thread: replaced objects are parked until only this class still references them.
template <typename T>
class RtShared
{
public:
    void publish (std::shared_ptr<const T> p)
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (current)
            graveyard.push_back (current);
        current = std::move (p);
        gen.fetch_add (1, std::memory_order_release);
        collectLocked ();
    }

    std::shared_ptr<const T> latest ()
    {
        std::lock_guard<std::mutex> lock (mutex);
        return current;
    }

    // Realtime side: never blocks. Returns true if `local` changed.
    bool fetch (std::shared_ptr<const T>& local, uint32_t& localGen)
    {
        const uint32_t g = gen.load (std::memory_order_acquire);
        if (g == localGen)
            return false;
        if (!mutex.try_lock ())
            return false;
        // The previous `local` is also held by `graveyard` or `current`, so dropping it here can't free.
        local = current;
        localGen = g;
        mutex.unlock ();
        return true;
    }

    void collectGarbage ()
    {
        std::lock_guard<std::mutex> lock (mutex);
        collectLocked ();
    }

private:
    void collectLocked ()
    {
        for (size_t i = 0; i < graveyard.size ();)
        {
            if (graveyard[i].use_count () == 1)
            {
                graveyard[i] = graveyard.back ();
                graveyard.pop_back ();
            }
            else
                ++i;
        }
    }

    std::mutex mutex;
    std::shared_ptr<const T> current;
    std::vector<std::shared_ptr<const T>> graveyard;
    std::atomic<uint32_t> gen {1};
};

class Bridge
{
public:
    Bridge () = default;

    void retain () { refs.fetch_add (1); }
    void release ()
    {
        if (refs.fetch_sub (1) == 1)
            delete this;
    }

    // --- sample (non-realtime) --------------------------------------------------
    // Loads and publishes a sample. On failure the previous sample stays loaded, but the
    // requested path is remembered so that saving the project doesn't lose the reference.
    bool loadSample (const std::string& path, const SampleOps& ops, std::string& error);
    void clearSample ();
    SamplePtr sample () { return samples.latest (); }
    std::string samplePath () const;
    SampleOps sampleOps () const;
    bool sampleMissing () const;
    std::string lastError () const;

    // --- slice edits -------------------------------------------------------------
    void setEdits (SliceEdits e)
    {
        edits.publish (std::make_shared<const SliceEdits> (std::move (e)));
        changeCounter.fetch_add (1);
    }
    SliceEditsPtr editsNow () { return edits.latest (); }

    // --- realtime side -----------------------------------------------------------
    bool fetchSample (SamplePtr& local, uint32_t& gen) { return samples.fetch (local, gen); }
    bool fetchEdits (SliceEditsPtr& local, uint32_t& gen) { return edits.fetch (local, gen); }

    void collectGarbage ()
    {
        samples.collectGarbage ();
        edits.collectGarbage ();
    }

    // Audition notes from the editor (single producer / single consumer).
    bool pushPreview (int note, float velocity);
    bool popPreview (int& note, float& velocity);

    static constexpr int kMaxPlayheads = 16;
    std::array<std::atomic<float>, kMaxPlayheads> playheads {};
    std::atomic<int> numPlayheads {0};
    std::atomic<double> hostBpm {120.0};
    std::atomic<bool> hostPlaying {false};
    std::atomic<uint32_t> changeCounter {0}; // bumps whenever sample/edits change

private:
    ~Bridge () = default;

    std::atomic<int> refs {1};
    RtShared<SampleData> samples;
    RtShared<SliceEdits> edits;

    mutable std::mutex infoMutex;
    std::string path, error;
    SampleOps ops;
    bool missing = false;

    static constexpr int kPreviewSize = 64;
    struct Preview
    {
        int note;
        float velocity;
    };
    std::array<Preview, kPreviewSize> previews {};
    std::atomic<int> previewWrite {0}, previewRead {0};
};

} // namespace simplr

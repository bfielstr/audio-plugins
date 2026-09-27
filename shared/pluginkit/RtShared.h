// Publishes immutable objects from a non-realtime thread to a realtime reader.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace pk {

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

} // namespace pk

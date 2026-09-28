// A process-wide directory of plug-in instances, so the instances of one plug-in running in the
// same host process can see each other (every plug-in module has its own copy, so a plug-in only
// ever sees its own kind).
//
// Fixed size and lock-free: a slot is claimed with a compare-and-swap and holds only atomics (an
// id, a heartbeat and the Payload, which must be default-constructible from atomics and have a
// clear()). The owner publishes into its slot and beats the heartbeat once per block; readers poll
// the slots. Liveness is judged by each reader in its own processing time (Liveness below), so no
// clock is read on the audio thread: a slot whose heartbeat has not moved for a second of the
// reader's audio counts as gone (a host that stopped processing an instance without releasing it).
//
// Only instances in one process see each other. The slots are plain atomics in one array, so the
// same layout can later live in named shared memory for hosts that run plug-ins in separate
// processes; then only where the array lives (global ()) changes.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace pk {

template <typename Payload, int N = 64>
class InstanceRegistry
{
public:
    static constexpr int kSlots = N;

    struct Slot
    {
        std::atomic<uint32_t> state {0}; // 0 free, 1 being claimed, 2 in use
        std::atomic<uint64_t> id {0};
        std::atomic<uint32_t> heartbeat {0};
        Payload data;
    };

    // The registry of this plug-in module.
    static InstanceRegistry& global ()
    {
        static InstanceRegistry r;
        return r;
    }

    // Ids are never reused, so a reader can tell a new instance in a recycled slot.
    uint64_t newId () { return nextId.fetch_add (1, std::memory_order_relaxed); }

    // Claims a free slot for `id`: its index, or -1 when all are taken. Not for the audio thread.
    int claim (uint64_t id)
    {
        for (int i = 0; i < N; ++i)
        {
            uint32_t expected = 0;
            Slot& s = slots[(size_t)i];
            if (s.state.compare_exchange_strong (expected, 1, std::memory_order_acq_rel))
            {
                s.data.clear ();
                s.heartbeat.store (0, std::memory_order_relaxed);
                s.id.store (id, std::memory_order_relaxed);
                s.state.store (2, std::memory_order_release);
                return i;
            }
        }
        return -1;
    }

    void release (int i)
    {
        if (i < 0 || i >= N)
            return;
        slots[(size_t)i].id.store (0, std::memory_order_relaxed);
        slots[(size_t)i].state.store (0, std::memory_order_release);
    }

    void beat (int i) { slots[(size_t)i].heartbeat.fetch_add (1, std::memory_order_release); }

    bool inUse (int i) const { return slots[(size_t)i].state.load (std::memory_order_acquire) == 2; }
    Slot& slot (int i) { return slots[(size_t)i]; }
    const Slot& slot (int i) const { return slots[(size_t)i]; }

    int used () const
    {
        int n = 0;
        for (int i = 0; i < N; ++i)
            n += inUse (i) ? 1 : 0;
        return n;
    }

private:
    std::array<Slot, N> slots;
    std::atomic<uint64_t> nextId {1};
};

// Which slots are alive, judged in the reader's own time: call update () once per block with the
// samples just processed. A new instance counts as alive at once; one whose heartbeat stood still
// for `timeoutSeconds` of the reader's audio counts as gone until it beats again.
template <int N = 64>
class Liveness
{
public:
    template <typename Registry>
    void update (const Registry& reg, int samples, double sampleRate, double timeoutSeconds = 1.0)
    {
        const double limit = timeoutSeconds * sampleRate;
        for (int i = 0; i < N; ++i)
        {
            const size_t k = (size_t)i;
            const uint64_t id = reg.inUse (i) ? reg.slot (i).id.load (std::memory_order_acquire) : 0;
            const uint32_t hb = reg.slot (i).heartbeat.load (std::memory_order_acquire);
            if (id != ids[k] || hb != beats[k])
            {
                ids[k] = id;
                beats[k] = hb;
                idle[k] = 0.0;
            }
            else
                idle[k] = std::min (idle[k] + samples, 1e15);
            live[k] = id != 0 && idle[k] < limit;
        }
    }
    bool alive (int i) const { return live[(size_t)i]; }
    uint64_t id (int i) const { return ids[(size_t)i]; }

private:
    std::array<uint64_t, N> ids {};
    std::array<uint32_t, N> beats {};
    std::array<double, N> idle {};
    std::array<bool, N> live {};
};

} // namespace pk

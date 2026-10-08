// A single-producer, single-consumer ring of variable-length records, lock-free and wait-free on both
// sides: the audio thread pushes, the writer thread pops. The memory is allocated by allocate () (never
// on the audio thread); push () only copies bytes and moves an atomic index, so it never blocks,
// allocates or calls the system. A record is a header (its type and payload size) and the payload,
// written all or nothing: a push that does not fit returns false and leaves the ring as it was.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace probr {

class Ring
{
public:
    struct Header
    {
        uint32_t type = 0;
        uint32_t bytes = 0; // the payload's size
    };
    // a part of a record's payload
    struct Part
    {
        const void* data;
        size_t bytes;
    };

    // Not while either side runs: at least 4 KiB.
    void allocate (size_t capacityBytes)
    {
        const size_t c = std::max<size_t> (capacityBytes, 4096);
        buf.assign (c, 0);
        head.store (0, std::memory_order_relaxed);
        tail.store (0, std::memory_order_relaxed);
    }
    size_t capacity () const { return buf.size (); }
    // Not while either side runs: empties the ring.
    void clear ()
    {
        head.store (0, std::memory_order_relaxed);
        tail.store (0, std::memory_order_relaxed);
    }

    // ---- producer
    // Free bytes as the producer sees them (the consumer may free more meanwhile).
    size_t freeBytes () const { return buf.size () - (size_t)(head.load (std::memory_order_relaxed) - tail.load (std::memory_order_acquire)); }
    // Pushes a record whose payload is the parts one after another, when it fits with `keepFree` bytes to
    // spare (room kept for the records that must always get through, such as a take's end).
    bool push (uint32_t type, std::initializer_list<Part> parts, size_t keepFree = 0)
    {
        size_t payload = 0;
        for (const auto& p : parts)
            payload += p.bytes;
        const size_t need = sizeof (Header) + payload;
        if (buf.empty () || need + keepFree > freeBytes ())
            return false;
        uint64_t h = head.load (std::memory_order_relaxed);
        Header hd {type, (uint32_t)payload};
        copyIn (h, &hd, sizeof (hd));
        h += sizeof (hd);
        for (const auto& p : parts)
        {
            copyIn (h, p.data, p.bytes);
            h += p.bytes;
        }
        head.store (h, std::memory_order_release);
        return true;
    }

    // ---- consumer
    bool empty () const { return head.load (std::memory_order_acquire) == tail.load (std::memory_order_relaxed); }
    // Bytes waiting to be read.
    size_t used () const { return (size_t)(head.load (std::memory_order_acquire) - tail.load (std::memory_order_relaxed)); }
    // Pops the next record into `h` and `payload` (resized to fit; the consumer may allocate): false when
    // the ring is empty.
    bool pop (Header& h, std::vector<uint8_t>& payload)
    {
        const uint64_t hd = head.load (std::memory_order_acquire);
        uint64_t t = tail.load (std::memory_order_relaxed);
        if (hd == t)
            return false;
        copyOut (t, &h, sizeof (h));
        t += sizeof (h);
        if (payload.size () < h.bytes)
            payload.resize (h.bytes);
        copyOut (t, payload.data (), h.bytes);
        t += h.bytes;
        tail.store (t, std::memory_order_release);
        return true;
    }

private:
    void copyIn (uint64_t at, const void* src, size_t n)
    {
        const size_t i = (size_t)(at % buf.size ());
        const size_t first = std::min (n, buf.size () - i);
        std::memcpy (buf.data () + i, src, first);
        if (n > first)
            std::memcpy (buf.data (), (const uint8_t*)src + first, n - first);
    }
    void copyOut (uint64_t at, void* dst, size_t n) const
    {
        const size_t i = (size_t)(at % buf.size ());
        const size_t first = std::min (n, buf.size () - i);
        std::memcpy (dst, buf.data () + i, first);
        if (n > first)
            std::memcpy ((uint8_t*)dst + first, buf.data (), n - first);
    }

    std::vector<uint8_t> buf;
    alignas (64) std::atomic<uint64_t> head {0}; // written by the producer
    alignas (64) std::atomic<uint64_t> tail {0}; // written by the consumer
};

} // namespace probr

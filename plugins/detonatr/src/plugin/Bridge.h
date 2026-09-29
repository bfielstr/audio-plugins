// Shared, in-process state between the processor (audio thread) and the controller/editor (UI
// thread): the Tone stage's recordings and the displays' levels. The processor owns it and hands a
// pointer to the controller in a connection message, so both must live in the same process.
#pragma once

#include "Engine.h"

#include "pluginkit/RtShared.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace detonatr {

using CarrierPtr = std::shared_ptr<const Carrier>;

// Recordings longer than this are cut (a carrier loops, and the project keeps its audio).
constexpr double kMaxCarrierSeconds = 10.0;

class Bridge
{
public:
    void retain () { refs.fetch_add (1); }
    void release ()
    {
        if (refs.fetch_sub (1) == 1)
            delete this;
    }

    // --- recordings (UI thread) ---------------------------------------------------
    // Decodes an audio file into a slot; on failure the slot keeps what it had.
    bool loadCarrier (int slot, const std::string& path, std::string& error);
    void setCarrier (int slot, CarrierPtr c, const std::string& name); // null: empty
    CarrierPtr carrier (int slot) { return carriers[slot].latest (); }
    std::string carrierName (int slot) const;
    void collectGarbage ()
    {
        for (auto& c : carriers)
            c.collectGarbage ();
    }

    // --- audio thread -------------------------------------------------------------
    bool fetchCarrier (int slot, CarrierPtr& local, uint32_t& gen) { return carriers[slot].fetch (local, gen); }

    std::atomic<uint32_t> changeCounter {0}; // bumps whenever a recording changes
    std::atomic<int> latency {0};            // what the processor reports
    Meters meters;

private:
    ~Bridge () = default;
    std::atomic<int> refs {1};
    pk::RtShared<Carrier> carriers[kCarrierSlots];
    mutable std::mutex nameMutex;
    std::string names[kCarrierSlots];
};

} // namespace detonatr

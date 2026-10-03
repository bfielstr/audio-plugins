// Shared, in-process state between the processor (audio thread) and the controller/editor
// (UI thread): the loaded sample, slice edits, playhead positions and audition notes.
// The processor owns it and hands a pointer to the controller through a connection message,
// so both components must live in the same process (the plug-in is not distributable).
#pragma once

#include "SampleData.h"
#include "Slices.h"

#include "pluginkit/RtShared.h"
#include "pluginkit/ScopeBuffer.h"

#include "Modulation.h"
#include "Rack.h"

#include "smacheratr/src/core/Engine.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace smemplr {

using pk::RtShared;

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

    // --- the modulation LFOs' mappings (the editor edits them, the state saves them) ---
    void setMods (ModMap m)
    {
        mods.publish (std::make_shared<const ModMap> (std::move (m)));
        modsChanged.fetch_add (1);
    }
    ModMapPtr modsNow () { return mods.latest (); }

    // --- realtime side -----------------------------------------------------------
    bool fetchSample (SamplePtr& local, uint32_t& gen) { return samples.fetch (local, gen); }
    bool fetchEdits (SliceEditsPtr& local, uint32_t& gen) { return edits.fetch (local, gen); }
    bool fetchMods (ModMapPtr& local, uint32_t& gen) { return mods.fetch (local, gen); }

    void collectGarbage ()
    {
        samples.collectGarbage ();
        edits.collectGarbage ();
        mods.collectGarbage ();
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

    // the effects' displays and the output scope (audio thread -> editor)
    RackMeters rack;
    smacheratr::Meters satMeters; // the saturator at the very end
    std::atomic<int> latency {0};  // what the processor reports; the editor tells the host when it changes
    static constexpr int kScopeSize = 65536;
    pk::ScopeBuffer<kScopeSize> outScope; // the final output
    std::atomic<double> sampleRate {48000.0};
    // the modulation as it plays (audio thread -> editor): the LFOs' values and phases, each mapping's
    // offset (normalized; NaN while it does not work), and the mappings those are for (modsChanged's
    // count when the processor took them: the editor shows the offsets once they are its own)
    std::array<std::atomic<float>, kModLfos> lfoValue {}, lfoPhase {};
    std::array<std::atomic<float>, kMaxModMappings> modOffset {};
    std::atomic<uint32_t> modsChanged {0}, modsPlaying {0};

private:
    ~Bridge () = default;

    std::atomic<int> refs {1};
    RtShared<SampleData> samples;
    RtShared<SliceEdits> edits;
    RtShared<ModMap> mods;

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

} // namespace smemplr

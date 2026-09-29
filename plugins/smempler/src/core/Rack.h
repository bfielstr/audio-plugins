// Smempler's effects rack: kRackSlots slots after the sampler (Params.h), each Empty or one of the
// suite's effects (para, multidyn, m/s eq, smacheratr, widr), in any order; the same effect may sit
// in several slots. Every slot owns one engine of each kind, allocated up front, so loading or
// moving an effect never allocates on the audio thread; only the slot's current kind runs.
//
// A slot's block of parameters is stored normalized and read through the kind's own table (fxTable),
// so a knob of the slot's effect in the editor, the host's automation and a saved project all see
// the effect's real ranges. The latency is the sum of the slots' effects' (Multidyn's look-ahead,
// Smacheratr's oversampling), whether they are on or not, so switching one off does not move it.
#pragma once

#include "MsEq.h"
#include "Params.h"

#include "multidyn/src/core/Engine.h"
#include "multidyn/src/plugin/Meters.h"
#include "para/src/core/Engine.h"
#include "smacheratr/src/core/Engine.h"
#include "widr/src/core/Engine.h"

#include "pluginkit/ParamTable.h"

#include <array>
#include <atomic>
#include <memory>

namespace smempler {

// The M/S EQ's own parameters (a slot of kind kFxMsEq).
namespace mseq {
enum ParamId : uint32_t { kSideHp = 0, kSlope, kSideGain, kMidGain, kNumParams };
const pk::ParamTable& paramTable ();
} // namespace mseq

const char* fxName (int type); // "para", ...; "" for Empty
// The table a kind reads its block through (an empty table for Empty).
const pk::ParamTable& fxTable (int type); // the effect's own table, by its own IDs
// A slot's block holds the effect's parameters by their own IDs, except that Multidyn's parameters
// added after the rack (RMS Window, Soften) sit where its saturator's are (not used in the rack):
// the block has room for 62. fxBlockTable is the table by block position; fxIdAt and fxBlockOf
// convert (-1: none).
const pk::ParamTable& fxBlockTable (int type);
int64_t fxIdAt (int type, uint32_t block);
int64_t fxBlockOf (int type, uint32_t id);

// What the rack's effects show in the editor, per slot.
struct RackMeters
{
    std::array<para::Meters, kRackSlots> para;
    std::array<multidyn::Meters, kRackSlots> multidyn;
    std::array<std::atomic<float>, kRackSlots> msMid {}, msSide {};
    std::array<smacheratr::Meters, kRackSlots> sat;
    std::array<widr::Meters, kRackSlots> widr;
};

class Rack
{
public:
    Rack ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    // A rack parameter (isRackParam): plain is the Smempler parameter's plain value (the block's
    // values are normalized).
    void setParam (uint32_t id, double plain);
    void process (float* L, float* R, int n);
    int latency () const;
    int type (int slot) const { return slots[(size_t)slot]->type; }

    // Para's envelope follows the sampler's notes.
    void noteOn (int note);
    void setPitchBend (float bipolar);

    void setMeters (RackMeters* m);

private:
    struct Slot
    {
        int type = kFxEmpty;
        bool on = true;
        std::array<double, kSlotBlock> norm {};
        para::Engine para {false};
        multidyn::Engine multidyn {false};
        MsEq ms;
        smacheratr::Engine sat;
        widr::Engine widr {false};
    };
    void apply (Slot& s, uint32_t j);      // one value to the slot's current effect
    void applyAll (Slot& s);               // every value (a new kind), and a clean start
    void publish (int slot);               // meters for the editor

    std::array<std::unique_ptr<Slot>, kRackSlots> slots;
    RackMeters* meters = nullptr;
    double sr = 48000.0;
    int maxBlock = 512;
};

} // namespace smempler

// Smemplr's effects rack: kRackSlots slots after the sampler (Params.h), each Empty or one of the
// suite's effects (para, multidyn, m/s eq, smacheratr, widr, wubr, levlr), in any order; the same effect may sit
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
#include "wubr/src/core/Engine.h"
#include "levlr/src/core/Engine.h"

#include "pluginkit/ParamTable.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace smemplr {

// The M/S EQ's own parameters (a slot of kind kFxMsEq).
namespace mseq {
enum ParamId : uint32_t { kSideHp = 0, kSlope, kSideGain, kMidGain, kNumParams };
const pk::ParamTable& paramTable ();
// A slope stored over the three choices of states before version 9 (normalized: 6, 12, 24 dB), as a
// value of the slope now (MsEq::Slope).
double slopeFromThreeChoices (double oldNorm);
} // namespace mseq

const char* fxName (int type); // "para", ...; "" for Empty
// The table a kind reads its block through (an empty table for Empty).
const pk::ParamTable& fxTable (int type); // the effect's own table, by its own IDs
// A slot's block holds the effect's parameters by their own IDs, except that Multidyn's parameters
// added after the rack (RMS Window, Soften) sit where its saturator's are (not used in the rack):
// the block has room for 62; and Wubr's (79, without its own saturator) run on into the slot's
// extension. fxBlockTable is the table by block position; fxIdAt and fxBlockOf convert (-1: none).
const pk::ParamTable& fxBlockTable (int type);
int64_t fxIdAt (int type, uint32_t block);
int64_t fxBlockOf (int type, uint32_t id);

// The effect parameters that its rack page deliberately does not show, and why. Every other parameter
// of the effect must have a control on the page (the host test checks it), so an effect that gains a
// parameter gets it in Smemplr too.
struct RackHidden
{
    uint32_t first, last; // a range of the effect's own IDs
    const char* why;
};
const std::vector<RackHidden>& rackHiddenParams (int type);

// The saturator after the rack before 0.9 (Smemplr's kTailBase and kTailExtBase parameters, now "Old
// End") as a Smacheratr slot with the same settings: set (Smemplr ID, normalized value) is called for
// the slot's Type, On and every block position; norm reads the old saturator's values (normalized, by
// Smemplr ID). Old projects that had it on get it in the rack this way when they load (StateIO.cpp).
void endSaturatorToSlot (int slot, const std::function<double (uint32_t)>& norm, const std::function<void (uint32_t, double)>& set);
// Where the old end saturator goes: the first empty slot after the last used one (the end of the
// chain), or -1 when the last slot is used. typeOf gives a slot's effect.
int slotAfterChain (const std::function<int (int)>& typeOf);
// A state from before 0.9 (version 9), as normalized values and whether the state had them: its rack
// is what it was (the slots it does not have are empty, not a new Smemplr's Smacheratr), and its
// saturator after the rack, if it was on (states without it played it on), goes to the end of the
// chain (slotAfterChain) with the same settings and is switched off. With no room there (the last slot
// is used) the old saturator stays on and keeps running after the rack, as before.
void moveEndSaturatorIntoRack (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has);

// States from before version 12 (before 11: Gently's Advanced mode too): the places in a rack
// Smacheratr's block that Gently's later parameters take (Advanced .. Drive Amount, then the Sub band's)
// held nothing that was used; they get their defaults (Advanced and Sub off: the same sound).
void migrateGentlyInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version);

// What the rack's effects show in the editor, per slot.
struct RackMeters
{
    std::array<para::Meters, kRackSlots> para;
    std::array<multidyn::Meters, kRackSlots> multidyn;
    std::array<std::atomic<float>, kRackSlots> msMid {}, msSide {};
    std::array<smacheratr::Meters, kRackSlots> sat;
    std::array<widr::Meters, kRackSlots> widr;
    std::array<wubr::Meters, kRackSlots> wubr;
    std::array<levlr::Meters, kRackSlots> levlr;
};

class Rack
{
public:
    Rack ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();
    // A rack parameter (isRackParam): plain is the Smemplr parameter's plain value (the block's
    // values are normalized).
    void setParam (uint32_t id, double plain);
    void process (float* L, float* R, int n);
    int latency () const;
    int type (int slot) const { return slots[(size_t)slot]->type; }

    // Para's envelope follows the sampler's notes; Wubr's envelopes start with them.
    void noteOn (int note);
    void noteOff (int note);
    void allNotesOff ();
    // the host's transport for the next block (Wubr's synced shapes follow the song)
    void setTransport (double bpm, double ppq, bool playing);
    void setPitchBend (float bipolar);

    void setMeters (RackMeters* m);

private:
    struct Slot
    {
        int type = kFxEmpty;
        bool on = true;
        std::array<double, kSlotBlockAll> norm {};
        para::Engine para {false};
        multidyn::Engine multidyn {false};
        MsEq ms;
        smacheratr::Engine sat;
        widr::Engine widr {false};
        wubr::Engine wubr {false};
        levlr::Engine levlr {false};
    };
    void apply (Slot& s, uint32_t j);      // one value to the slot's current effect
    void applyAll (Slot& s);               // every value (a new kind), and a clean start
    void publish (int slot);               // meters for the editor

    std::array<std::unique_ptr<Slot>, kRackSlots> slots;
    RackMeters* meters = nullptr;
    double sr = 48000.0;
    int maxBlock = 512;
};

} // namespace smemplr

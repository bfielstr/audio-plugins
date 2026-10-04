// Smemplr's effects rack: kRackSlots slots after the sampler (Params.h), each Empty or one of the
// suite's effects (para, multidyn, m/s eq, smacheratr, widr, wubr, levlr, gentlr, smoothr), in any order; the same effect may sit
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
#include "smoothr/src/core/Engine.h"
#include "gentlr/src/core/Engine.h"

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
const char* fxFormerName (int type); // the name a renamed kind had ("gently" for gentlr), for pasting its old settings; "" for none
// The table a kind reads its block through (an empty table for Empty).
const pk::ParamTable& fxTable (int type); // the effect's own table, by its own IDs
// A slot's block holds the effect's parameters by their own IDs, except that Multidyn's parameters
// added after the rack (RMS Window, Soften) sit where its saturator's are (not used in the rack):
// the block has room for 62; Wubr's (79, without its own saturator) run on into the slot's
// extension; and Para's after its low-pass drive (Low-Pass Slope and the Gain Locks, IDs 62 .. 64) are
// at block positions 62 .. 64 too, the first of the slot's extension (its block position is its ID
// throughout). fxBlockTable is the table by block position; fxIdAt and fxBlockOf convert (-1: none).
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

// States from before version 17 (before 12: the Sub band's too; before 11: Gentlr's Advanced mode too):
// the places in a rack Smacheratr's block that Gentlr's later parameters take (Advanced .. Drive Amount,
// the Sub band's, then the High band's and No Overlap) held nothing that was used; they get their
// defaults (Advanced, Sub, High and No Overlap off: the same sound). A Gentlr slot's High band and No
// Overlap (17) get theirs the same way.
void migrateGentlrInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version);

// States from before version 19: the Sub and High bands of the rack's Smacheratrs and Gentlrs had a
// button each (off by default) and Ranges of 8 and 6 dB by default; now a band works while its Range is
// above 0 dB. A band that was off gets Range 0, one that was on keeps its Range (or, not saved, the old
// default): smacheratr::subHighStateToRange. Same sound.
void migrateSubHighInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version);

// States from before version 20: the rack's Smacheratrs and Gentlrs had one band shape, before Gentlr's
// Slope; their Slope gets Classic, that shape (the places held nothing that was used, or a default set
// by the migrations before this one). Same sound; a new slot gets 12 / 12.
void migrateSlopeInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version);

// States from before version 21: the rack's Smacheratrs and Gentlrs had no glue; their glue switches get
// off, their default (the places held nothing that was used). Same sound.
void migrateGlueInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version);

// States from before version 22: the rack's Smacheratrs had the Hi-Quality switch where Oversampling is
// (on -> 4x, off -> Off: smacheratr::oversamplingFromHiQuality), and so had the old saturator after the
// rack; a Levlr slot's drives' Oversampling (its place held nothing that was used) gets 4x, what the
// drives always ran at. Same sound.
void migrateOversamplingInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version);

// States from before version 13: a Multidyn slot's gain staging (its preset's gains baked in were an
// "OTT pushed further" then: multidyn::migrateOldBaked moves the difference into its controls) and its
// later parameters (Slope, Soften Color, the Sub band: defaults, the same sound).
void migrateMultidynInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version);
// States from before version 13: a Para slot's slope (one of 12 / 18 / 24 dB then) on the longer list,
// and its one drive becomes both filters' drives (para::upgradeToPerBandDrive). Then, for states from
// before version 15 (those too): the low-pass gets the slot's one slope, the high-pass's Gain Lock is on
// unless its gain is above 0 dB, the low-pass's off, and Fade (1 .. 36 semitones then) keeps its
// semitones (para::upgradeToSeparateSlopes).
void migrateParaInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version);
// States from before version 13: a Levlr slot's Bands and band drives (added in 0.7) read 0 there,
// which is 1 band: they get their defaults (4 bands, no drive: the same sound).
void migrateLevlrInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version);

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
    std::array<smoothr::Meters, kRackSlots> smoothr;
    std::array<gentlr::Meters, kRackSlots> gentlr;
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
        smoothr::Engine smoothr;
        gentlr::Engine gentlr {false};
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

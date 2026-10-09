// Smemplr's effects rack: kRackSlots slots after the sampler (Params.h), each an FxSlot (FxSlot.h: Empty or
// one of the suite's effects, in any order; the same effect may sit in several slots). The latency is the sum
// of the slots' effects' (FxSlot::latency), whether they are on or not, so switching one off does not move it.
// The migrations of old states' rack values are here (FxSlot.h has the slot itself and the kinds' tables).
#pragma once

#include "FxSlot.h"
#include "Params.h"

#include "multidyn/src/plugin/Meters.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace smemplr {

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
// by the migrations before this one). Same sound; a new slot gets Signature (12 / 12 up to 0.24).
void migrateSlopeInSlots (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has, int version);

// States from before version 24: Gentlr's band Slope had three choices (12 / 12, Signature, Classic) before
// Alt Signature, stored normalized over those: the Slope a rack's Smacheratr or Gentlr has in the state, as
// the same choice now (smacheratr::slopeNormFromThreeChoices). Before the migrations that set the Slope
// (they set it as it is now).
void migrateSlopeChoicesInSlots (std::array<double, kNumParams>& norm, const std::array<bool, kNumParams>& has, int version);

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

// States from before version 23: a new Smemplr's first slot (Smacheratr, its own defaults: Params.cpp
// slotDefault) had Gentlr off and its Slope 12 / 12; Smacheratr has them on and Signature by default now.
// Where such a state lacks them (and the slot holds a Smacheratr, as by default), they get the old values
// (normalized 0), so it sounds as it did. (A state
// from 9 on has every rack parameter, and moveEndSaturatorIntoRack fills an older one's rack.)
void keepOldFirstSlotDefaults (std::array<double, kNumParams>& norm, std::array<bool, kNumParams>& has);

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
    void publish (int slot);               // meters for the editor

    std::array<std::unique_ptr<FxSlot>, kRackSlots> slots;
    RackMeters* meters = nullptr;
    double sr = 48000.0;
    int maxBlock = 512;
};

} // namespace smemplr

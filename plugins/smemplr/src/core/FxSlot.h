// One effects slot of smemplr's rack (library smemplr_fxslot): Empty or one of the suite's effects (multidyn,
// m/s eq, smacheratr, widr, gentlr). Smemplr's rack (Rack.h) is a row of these after the sampler. (Para, Wubr,
// Levlr and Smoothr were kinds too: their places in the Type are kept, and a slot saved with one of them loads
// Empty: fxRetired.)
//
// A slot owns one engine of each kind, allocated up front, so loading or moving an effect never allocates on
// the audio thread; only the slot's current kind runs. Its parameters are a Type, an On and a block of
// kSlotBlockAll values stored normalized and read through the kind's own table (fxBlockTable), so a knob of
// the slot's effect, the host's automation and a saved project all see the effect's real ranges whatever is
// loaded where. The latency is the kind's (Multidyn's look-ahead, Smacheratr's oversampling, ...), whether it
// is on or not, so switching it off does not move it.
#pragma once

#include "MsEq.h"

#include "gentlr/src/core/Engine.h"
#include "multidyn/src/core/Engine.h"
#include "smacheratr/src/core/Engine.h"
#include "widr/src/core/Engine.h"

#include "pluginkit/ParamTable.h"

#include <array>
#include <cstdint>
#include <vector>

namespace smemplr {

// The kinds a slot can hold. Saved in projects (a slot's Type): only ever append. The retired ones (Para, Wubr,
// Levlr, Smoothr: their plug-ins are gone) keep their places, so the others keep theirs; a slot of one of them is
// an Empty slot (fxKind), a project's loads as one, and no menu offers them (fxOffered).
enum FxType
{
    kFxEmpty = 0,
    kFxRetiredPara,
    kFxMultidyn,
    kFxMsEq,
    kFxSmacheratr,
    kFxWidr,
    kFxRetiredWubr,
    kFxRetiredLevlr,
    kFxGentlr,
    kFxRetiredSmoothr,
    kNumFxTypes
};
static_assert (kFxRetiredPara == 1 && kFxMultidyn == 2 && kFxMsEq == 3 && kFxSmacheratr == 4 && kFxWidr == 5 && kFxRetiredWubr == 6 &&
                   kFxRetiredLevlr == 7 && kFxGentlr == 8 && kFxRetiredSmoothr == 9 && kNumFxTypes == 10,
               "the slot types are saved in projects: only ever append");
constexpr bool fxRetired (int type)
{
    return type == kFxRetiredPara || type == kFxRetiredWubr || type == kFxRetiredLevlr || type == kFxRetiredSmoothr;
}
// what a slot of a saved Type holds: the kind, or Empty (a retired kind, or out of range)
constexpr int fxKind (int type) { return type <= kFxEmpty || type >= kNumFxTypes || fxRetired (type) ? kFxEmpty : type; }
// the kinds a user can put in a slot (every kind but Empty and the retired ones)
constexpr bool fxOffered (int type) { return fxKind (type) != kFxEmpty; }
// A slot's block: room for the largest effect's parameters (Multidyn, see fxBlockTable), then an extension of
// kSlotExt more positions (block positions kSlotBlock and up: Multidyn's later parameters, Para's once; its
// size is saved in projects, from when Wubr's ran on into it).
constexpr uint32_t kSlotBlock = 62;
constexpr uint32_t kSlotExt = 24;
constexpr uint32_t kSlotBlockAll = kSlotBlock + kSlotExt; // every block position of a slot
// A slot's parameters by field: its Type, its On, then its block (kSlotParams + block position).
enum SlotField : uint32_t { kSlotType = 0, kSlotOn, kSlotParams };
constexpr uint32_t kSlotFields = kSlotParams + kSlotBlockAll; // every parameter of a slot
static_assert (kSlotBlock == 62 && kSlotExt == 24 && kSlotFields == 88, "a slot's layout is saved in projects");

// The M/S EQ's own parameters (a slot of kind kFxMsEq).
namespace mseq {
enum ParamId : uint32_t { kSideHp = 0, kSlope, kSideGain, kMidGain, kNumParams };
const pk::ParamTable& paramTable ();
// A slope stored over the three choices of states before version 9 (normalized: 6, 12, 24 dB), as a
// value of the slope now (MsEq::Slope).
double slopeFromThreeChoices (double oldNorm);
} // namespace mseq

const char* fxName (int type); // "multidyn", ...; "" for Empty (a retired kind keeps its name, for the Type's choices)
const char* fxFormerName (int type); // the name a renamed kind had ("gently" for gentlr), for pasting its old settings; "" for none
// The table a kind reads its block through (an empty table for Empty).
const pk::ParamTable& fxTable (int type); // the effect's own table, by its own IDs
// A slot's block holds the effect's parameters by their own IDs, except that Multidyn's parameters
// added after the rack (RMS Window, Soften) sit where its saturator's are (not used in the rack):
// the block has room for 62; Multidyn's later ones run on into the slot's extension. fxBlockTable is the
// table by block position (an empty table for Empty and the retired kinds); fxIdAt and fxBlockOf
// convert (-1: none).
const pk::ParamTable& fxBlockTable (int type);
int64_t fxIdAt (int type, uint32_t block);
int64_t fxBlockOf (int type, uint32_t id);

// The effect parameters that a rack page deliberately does not show, and why. Every other parameter
// of the effect must have a control on the page (smemplr's host test checks it), so an effect that gains a
// parameter gets it in the racks too.
struct RackHidden
{
    uint32_t first, last; // a range of the effect's own IDs
    const char* why;
};
const std::vector<RackHidden>& rackHiddenParams (int type);

// One slot: its kind, its On, its block (normalized) and an engine of every kind.
struct FxSlot
{
    int type = kFxEmpty;
    bool on = true;
    std::array<double, kSlotBlockAll> norm {};
    multidyn::Engine multidyn {false};
    MsEq ms;
    smacheratr::Engine sat;
    widr::Engine widr {false};
    gentlr::Engine gentlr {false};

    // every engine prepared, then every value applied (and a clean start)
    void prepare (double sampleRate, int maxBlock);
    void reset (); // every engine
    // a slot parameter by field (plain: Type the kind, On 0 / 1, a block position its normalized value)
    void setType (int t); // a new kind gets every value and a clean start (a retired kind: Empty, fxKind)
    void setOn (bool isOn);
    void setValue (uint32_t block, double normalized);
    void apply (uint32_t block); // one value to the slot's current effect
    void applyAll ();             // every value (a new kind), and a clean start
    // in place; off: the kind's bypass (its latency's delay, or fully dry at the same latency)
    void process (float* L, float* R, int n);
    int latency () const; // the kind's, on or off (0 for Empty, Widr and the M/S EQ)
};

} // namespace smemplr

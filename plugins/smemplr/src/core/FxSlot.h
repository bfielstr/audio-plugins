// One effects slot of the suite's racks (library smemplr_fxslot): Empty or one of the suite's effects (para,
// multidyn, m/s eq, smacheratr, widr, wubr, levlr, gentlr, smoothr). Smemplr's rack (Rack.h) is a row of these
// after the sampler; moistr's lab (moistr/src/core/Lab.h) puts them on its bands and after them.
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
#include "levlr/src/core/Engine.h"
#include "multidyn/src/core/Engine.h"
#include "para/src/core/Engine.h"
#include "smacheratr/src/core/Engine.h"
#include "smoothr/src/core/Engine.h"
#include "widr/src/core/Engine.h"
#include "wubr/src/core/Engine.h"

#include "pluginkit/ParamTable.h"

#include <array>
#include <cstdint>
#include <vector>

namespace smemplr {

// The kinds a slot can hold. Saved in projects (a slot's Type): only ever append.
enum FxType { kFxEmpty = 0, kFxPara, kFxMultidyn, kFxMsEq, kFxSmacheratr, kFxWidr, kFxWubr, kFxLevlr, kFxGentlr, kFxSmoothr, kNumFxTypes };
// A slot's block: room for the largest effect's parameters (Multidyn, see fxBlockTable), then an extension of
// kSlotExt more positions (Wubr's run on into it; block positions kSlotBlock and up).
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
    para::Engine para {false};
    multidyn::Engine multidyn {false};
    MsEq ms;
    smacheratr::Engine sat;
    widr::Engine widr {false};
    wubr::Engine wubr {false};
    levlr::Engine levlr {false};
    smoothr::Engine smoothr;
    gentlr::Engine gentlr {false};

    // every engine prepared, then every value applied (and a clean start)
    void prepare (double sampleRate, int maxBlock);
    void reset (); // every engine
    // a slot parameter by field (plain: Type the kind, On 0 / 1, a block position its normalized value)
    void setType (int t); // a new kind gets every value and a clean start
    void setOn (bool isOn);
    void setValue (uint32_t block, double normalized);
    void apply (uint32_t block); // one value to the slot's current effect
    void applyAll ();             // every value (a new kind), and a clean start
    // in place; off: the kind's bypass (its latency's delay, or fully dry at the same latency)
    void process (float* L, float* R, int n);
    int latency () const; // the kind's, on or off (0 for Empty, Widr, Wubr and the M/S EQ)

    // Para's envelope follows the notes; Wubr's envelopes start with them; Wubr's synced shapes follow the song.
    void noteOn (int note);
    void noteOff (int note);
    void allNotesOff ();
    void setTransport (double bpm, double ppq, bool playing);
    void setPitchBend (float bipolar);
};

} // namespace smemplr

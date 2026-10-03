// The modulation section: kModLfos LFOs (their parameters at kModLfoBase, Params.h) and the mappings
// from an LFO to a parameter, made in the editor by dragging an LFO onto a control. A mapping moves
// its parameter's normalized value by depth x the LFO (-1 .. 1), on top of the value the host has
// (which stays where it is: automation and the controls show the parameter itself); the sum is held
// to 0 .. 1. The mappings are stored in the plug-in's state (StateIO.cpp, from version 18), not in
// parameters: the editor publishes them to the processor through the Bridge (like the slice edits).
#pragma once

#include "Params.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace smemplr {

constexpr int kMaxModMappings = 24; // (the editor lists them all)

struct ModMapping
{
    int lfo = 0;         // 0 .. kModLfos - 1
    uint32_t target = 0; // a Smemplr parameter (canModulate)
    double depth = 0.0;  // -1 .. 1, a share of the target's normalized range
    // A rack parameter: the effect in its slot when it was mapped. The mapping works while that effect
    // is there (another one loaded into the slot pauses it, and the effect coming back resumes it). -1
    // for the sampler's own parameters.
    int fxType = -1;
    bool operator== (const ModMapping& o) const
    {
        return lfo == o.lfo && target == o.target && depth == o.depth && fxType == o.fxType;
    }
};

struct ModMap
{
    std::vector<ModMapping> list; // at most kMaxModMappings
};
using ModMapPtr = std::shared_ptr<const ModMap>;

// Whether an LFO may modulate a parameter; `slotType` is the effect in its slot (rack parameters).
// Numbers only (continuous or stepped): not switches or menus, a slot's Type or On, the LFOs' own
// parameters, or the old ones that no control shows any more.
bool canModulate (uint32_t id, int slotType);
// The fxType a new mapping of `id` stores (the slot's effect, or -1).
inline int modFxTypeFor (uint32_t id, int slotType) { return isRackParam (id) ? slotType : -1; }

// Editing (on the UI thread). addMapping: false when the LFO already modulates the target (that mapping
// is left as it is) or the list is full.
bool addMapping (ModMap& m, int lfo, uint32_t target, double depth, int fxType);
bool removeMapping (ModMap& m, size_t index);
// The rack's effects moved: newSlot[s] is where slot s's effect went (-1: it was removed, and its
// mappings go too). Mappings of the other parameters stay.
void remapSlots (ModMap& m, const std::array<int, kRackSlots>& newSlot);

// The mappings as bytes for the state, little-endian: a format number, the count, the size of an entry,
// then the entries (a reader skips the fields it does not know at the end of each entry, so entries can
// grow). decode drops what it cannot use (an unknown parameter or LFO) and returns false when the data
// is cut short or not this format.
std::vector<uint8_t> encodeModMap (const ModMap& m);
bool decodeModMap (const uint8_t* data, size_t size, ModMap& m);

// An LFO's shape at phase 0 .. 1 (-1 .. 1). The random shapes take the value held for this cycle (S&H)
// and glide to it from the last one over the cycle (Smooth Random).
float modShape (int shape, double phase, float lastRandom, float random);

// The LFOs. Each runs freely at its Rate (Hz) or, synced, at a note length of the host's tempo, and while
// the host plays (with a song position) a synced LFO without Retrig follows the song: the same place in
// the bar sounds the same every time. Retrig restarts an LFO at its Phase on every note. 120 BPM when
// the host gives no tempo.
class Modulator
{
public:
    void prepare (double sampleRate) { sr = sampleRate > 0.0 ? sampleRate : 48000.0; }
    void reset ();
    void noteOn () { notePending = true; }
    // The values at the start of n samples, then the phases moved on by them. `p`: plain parameter values.
    void advance (const double* p, int n, double bpm, double ppq, bool songPlaying);
    float value (int lfo) const { return lfos[(size_t)lfo].value; }
    double phase (int lfo) const { return lfos[(size_t)lfo].shown; } // where the shape is read (Phase included)

private:
    struct Lfo
    {
        double run = 0.0;   // 0 .. 1, without the Phase offset
        double shown = 0.0; // run + Phase (wrapped)
        float last = 0.0f, random = 0.0f;
        float value = 0.0f;
    };
    void nextRandom (Lfo& l);
    std::array<Lfo, kModLfos> lfos {};
    double sr = 48000.0;
    uint32_t seed = 0x2545F491u;
    bool notePending = false;
};

} // namespace smemplr

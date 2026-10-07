// Wubr parameters. IDs are persisted in projects: only ever append. The end saturator's extended
// block comes last, so it can grow; a new Wubr parameter goes in a block after it (and from then on
// the extended block stays as it is).
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace wubr {

static_assert (pk::kTailFields == 6, "Wubr's band IDs start after the six tail fields");

constexpr int kBands = 2;
constexpr int kMaxPoints = 8;

// Each band: a bell (Width octaves wide) whose gain and/or centre a drawn shape moves, at its own rate.
enum BandField : uint32_t
{
    kBandOn = 0,
    kTarget,     // Gain / Frequency / Both: what the shape moves
    kFreq,       // Hz, the band's centre
    kWidth,      // octaves between its edges
    kGain,       // dB, the band's level where the shape is at 0
    kDepth,      // dB the shape adds at +1 (and takes away at -1)
    kSweep,      // octaves the shape sweeps the centre over (+1: half of it up, -1: half down)
    kRateMode,   // Sync / Free
    kSync,       // a note division (kSyncDivisions)
    kRateHz,     // Hz, Free
    kPhase,      // degrees: where in the shape the cycle starts
    kPointCount, // 2 .. kMaxPoints
    kHold,       // Envelope: the point the shape stops at (1-based; the last point = runs to the end)
    kPoints,     // kMaxPoints x (x 0..1, y -1..1, curve -1..1 of the segment after it)
    kBandBlock = kPoints + kMaxPoints * 3
};
enum PointField : uint32_t { kPtX = 0, kPtY, kPtCurve };

enum ParamId : uint32_t
{
    kMode = 0,    // LFO / Envelope
    kTrigger,     // Envelope: MIDI / Transient
    kSensitivity, // dB a hit must rise over the recent level to trigger (Transient)
    kDryWet,
    kOutput, // dB
    kTailBase,                                   // the Smacheratr at the end of the chain: pk::kTailFields entries
    kBandBase = kTailBase + pk::kTailFields,     // kBands x kBandBlock
    kTailExtBase = kBandBase + kBands * kBandBlock, // the rest of the end Smacheratr
    // --- after the end saturator's block (which stays as it is from here on) ---
    kLinkRate = kTailExtBase + 17, // both bands run at band 1's rate (Sync/Free, division, Hz); each keeps its phase
    kTailExt2Base, // Gentlr's Advanced mode in the end Smacheratr: pk::kTailExt2Fields entries
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields, // Gentlr's High band, No Overlap and Slope in it: pk::kTailExt3Fields entries
    kTailExt4Base = kTailExt3Base + pk::kTailExt3Fields, // Gentlr's glue in it: pk::kTailExt4Fields entries (the last block)
    kNumParams = kTailExt4Base + pk::kTailExt4Fields
};

enum Mode { kLfo = 0, kEnvelope };
enum Trigger { kMidi = 0, kTransient };
enum Target { kTargetGain = 0, kTargetFreq, kTargetBoth };
enum RateMode { kSynced = 0, kFree };

constexpr int kSyncDivisions = 16;
// the note divisions of kSync, in beats (quarter notes)
inline constexpr double kSyncBeats[kSyncDivisions] = {0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0,
                                                      1.0,   1.5,       4.0 / 3.0, 2.0, 3.0,   4.0, 8.0,  16.0};

constexpr uint32_t bandParam (int band, uint32_t field) { return kBandBase + (uint32_t)band * kBandBlock + field; }
constexpr uint32_t pointParam (int band, int point, uint32_t field)
{
    return bandParam (band, kPoints + (uint32_t)point * 3 + field);
}
constexpr bool isTailParam (uint32_t id)
{
    return (id >= kTailBase && id < kTailBase + pk::kTailFields) || (id >= kTailExtBase && id < kTailExtBase + pk::kTailExtFields) ||
           (id >= kTailExt2Base && id < kTailExt2Base + pk::kTailExt2Fields) ||
           (id >= kTailExt3Base && id < kTailExt3Base + pk::kTailExt3Fields) ||
           (id >= kTailExt4Base && id < kTailExt4Base + pk::kTailExt4Fields);
}
constexpr uint32_t tailField (uint32_t id)
{
    return id >= kTailExt4Base  ? pk::kTailFields + pk::kTailExtFields + pk::kTailExt2Fields + pk::kTailExt3Fields + (id - kTailExt4Base)
           : id >= kTailExt3Base ? pk::kTailFields + pk::kTailExtFields + pk::kTailExt2Fields + (id - kTailExt3Base)
           : id >= kTailExt2Base ? pk::kTailFields + pk::kTailExtFields + (id - kTailExt2Base)
           : id >= kTailExtBase ? pk::kTailFields + (id - kTailExtBase)
                                : id - kTailBase;
}

const pk::ParamTable& paramTable ();
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return paramTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }

// Menu > Defaults (pluginkit/GentlrDefaults.h): the parameters Gentlr On by Default and Advanced On by
// Default set in a new instance: the end saturator's Saturator and Gentlr switches and Gentlr's Advanced
inline constexpr pk::GentlrIds kGentlrIds = pk::tailGentlrIds (kTailBase, kTailExtBase, kTailExt2Base);

} // namespace wubr

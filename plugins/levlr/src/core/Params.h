// Levlr parameters. IDs are persisted in projects: only ever append. The end saturator's blocks
// (kTailBase, kTailExtBase, kTailExt2Base) are closed now: the band count and the bands' drives come
// after them, at 49 (pinned below, so a saturator block that grows can't move them), and the end
// saturator's fourth block (Gentlr's High band and No Overlap) after those, at 58, its fifth (the glue)
// at 64, then the drives' Oversampling at 69 (so the fifth block is closed in too).
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cstdint>

namespace levlr {

static_assert (pk::kTailFields == 6, "Levlr's band IDs start after the six tail fields");

constexpr int kBands = 4;
constexpr int kCrossovers = kBands - 1;
constexpr double kMinXoverHz = 20.0, kMaxXoverHz = 20000.0;
constexpr double kMinGapOct = 1.0 / 6.0; // crossovers stay at least this far apart

// Each band: its level, and whether it is heard.
enum BandField : uint32_t
{
    kGain = 0, // dB
    kMute,
    kSolo,
    kBandBlock
};

// Each band's drive: a gain into a curve (Drive.h), after the band's level.
enum DriveField : uint32_t
{
    kDriveDb = 0, // dB, 0 = off (the band stays clean)
    kDriveType,   // the curve (DriveType)
    kDriveBlock
};

enum DriveType
{
    kDriveAnalog = 0, // Smacheratr's curve
    kDriveTape,       // tanh
    kDriveTube,       // asymmetric: even harmonics
    kDriveHard,       // hard clip
    kDriveFold,       // a sine wavefolder
    kNumDriveTypes
};
constexpr double kMaxDriveDb = 36.0;

enum ParamId : uint32_t
{
    kSlope = 0, // the crossovers' Linkwitz-Riley slope (Slope: 12 .. 96 dB/oct in 12 dB steps)
    kOutput,    // dB, before the Smacheratr at the end
    kXover,     // kCrossovers x Hz: band k's top edge is kXover + k
    kTailBase = kXover + kCrossovers,               // the Smacheratr at the end of the chain: pk::kTailFields entries
    kBandBase = kTailBase + pk::kTailFields,        // kBands x kBandBlock
    kTailExtBase = kBandBase + kBands * kBandBlock, // the rest of the end Smacheratr
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // Gentlr's Advanced mode in the end Smacheratr: pk::kTailExt2Fields entries
    kBandCount = 49,                                // Bands: how many are in use (a choice: 1 .. 4, 4 by default)
    kDriveBase,                                     // kBands x kDriveBlock: each band's drive
    kTailExt3Base = kDriveBase + kBands * kDriveBlock, // Gentlr's High band, No Overlap and Slope in the end Smacheratr: pk::kTailExt3Fields entries
    kTailExt4Base = kTailExt3Base + pk::kTailExt3Fields, // Gentlr's glue in it: pk::kTailExt4Fields entries
    kDriveOversampling = kTailExt4Base + pk::kTailExt4Fields, // the band drives' oversampling: Off, 2x, 4x (4x before it, and by default)
    kNumParams
};
static_assert (kTailExt2Base + pk::kTailExt2Fields == kBandCount, "the end saturator's blocks end where the band count starts");
static_assert (kBandCount == 49 && kDriveBase == 50 && kTailExt3Base == 58 && kTailExt4Base == 64 && kDriveOversampling == 69 &&
                   kNumParams == 70,
               "saved IDs: Bands at 49, the drives at 50 .. 57, the end saturator's fourth block at 58 .. 63, its fifth at 64 .. 68, "
               "the drives' Oversampling at 69");

// kDriveOversampling's choices (in this order: persisted; the same as Smacheratr's Oversampling) and the
// factor a plain value picks (1, 2 or 4)
enum DriveOversampling : int
{
    kDriveOsOff = 0,
    kDriveOs2x,
    kDriveOs4x
};
inline int driveOversamplingFactor (double plain)
{
    const int m = (int)(plain + 0.5);
    return m <= kDriveOsOff ? 1 : m == kDriveOs2x ? 2 : 4;
}

constexpr uint32_t bandParam (int band, uint32_t field) { return kBandBase + (uint32_t)band * kBandBlock + field; }
constexpr uint32_t xoverParam (int k) { return kXover + (uint32_t)k; }
constexpr uint32_t driveParam (int band, uint32_t field) { return kDriveBase + (uint32_t)band * kDriveBlock + field; }
// the Bands choice (0 .. 3) as a count of bands (1 .. 4)
inline int bandsOf (double choicePlain)
{
    const int c = (int)(choicePlain + 0.5) + 1;
    return c < 1 ? 1 : (c > kBands ? kBands : c);
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

// The state's version: 2 (0.6.0) had Slope's eight choices; 3 adds Bands and the bands' drives; 4: the
// end saturator's Sub and High bands have no buttons (they work while their Range is above 0 dB); 5: the
// end saturator's Gentlr Slope; 6: Oversampling (the drives' and the end saturator's, Off / 2x / 4x); 7: the
// end saturator and its Gentlr on by default, Gentlr's Slope Signature (State.cpp gives a state saved before
// that the old defaults where it lacks them: smacheratr::tailOldDefaults).
constexpr int kStateVersion = 8;
constexpr int kOversamplingChoiceVersion = 6;
constexpr int kNewDefaultsVersion = 7;
// 8: Gentlr's Slope has a fourth choice, Alt Signature (State.cpp reads a Slope saved before as the same choice)
constexpr int kAltSignatureVersion = 8;
// Brings the normalized values of a state saved by `version` to this one: version 1's three slopes
// among the eight; before 3, four bands and every drive off (the sound it was saved with); before 4,
// the end saturator's Sub and High bands that were off get Range 0 (smacheratr::subHighStateToRange);
// before 5, the end saturator's Gentlr Slope is Classic (the shape there was); before 6, the end
// saturator's Oversampling was its Hi-Quality switch (on 4x, off Off), and the drives were always 4x (the
// new Oversampling's default, so a state without it needs nothing).
// `has`: the IDs the state had (the rest hold their defaults).
void migrateState (int version, double norm[kNumParams], const bool has[kNumParams]);
// The parameters added after 0.6.0 (Bands and the drives). A host that stored Levlr's parameters
// without them and reads them as normalized 0 (Smemplr's rack slots) sets each to its default:
// Bands' default is 4 (normalized 1), not 0 (1 band); the drives' defaults are normalized 0.
constexpr uint32_t kFirstAddedAfter060 = kBandCount, kEndAddedAfter060 = kNumParams;

// Menu > Defaults (pluginkit/GentlrDefaults.h): the parameters Gentlr On by Default and Advanced On by
// Default set in a new instance: the end saturator's Saturator and Gentlr switches and Gentlr's Advanced
inline constexpr pk::GentlrIds kGentlrIds = pk::tailGentlrIds (kTailBase, kTailExtBase, kTailExt2Base);

} // namespace levlr

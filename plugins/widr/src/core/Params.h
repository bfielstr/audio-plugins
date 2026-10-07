// Widr parameters. IDs are persisted in projects: only ever append.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include <cmath>
#include <cstdint>

namespace widr {

enum ParamId : uint32_t
{
    kWidth = 0,  // 0 .. 2 (0 = bypass, 1 = 100 %)
    kCharacter,  // Tight / Wide / Epic / Surround
    kSize,       // 0 .. 1: the Haas delay and the early-reflection pattern
    kSpace,      // 0 .. 1: the short stereo reverb
    kDecay,      // ms
    kPreDelay,   // ms
    kDamping,    // Hz
    kAir,        // dB, high shelf on the side
    kBeyond,     // 0 .. 1: side lift around 4 kHz (images beyond the speakers)
    kMonoBelow,  // Hz: mono below this
    kGuard,      // 0 .. 1: mono guard
    kRole,       // Anchor / Support / Wide / Ambient
    kAware,      // 0 .. 1: how much this instance yields to the others in its group
    kGroup,      // 1 .. 8
    kMonoCheck,  // listen in mono
    kOutput,     // dB
    kTailBase,   // the Smacheratr at the end of the chain: pk::kTailFields entries
    kContrast = kTailBase + pk::kTailFields, // 0 .. 1: mid / side contrast (scaled by the Character)
    kDryLevel,   // dB (bottom = -inf): the input, in parallel with...
    kWetLevel,   // dB (bottom = -inf): ...what Widr adds (the voices and the reverb)
    kTailExtBase, // the rest of the end-of-chain Smacheratr: pk::kTailExtFields entries
    kTailExt2Base = kTailExtBase + pk::kTailExtFields, // Gentlr's Advanced mode in the end Smacheratr: pk::kTailExt2Fields entries
    kTailExt3Base = kTailExt2Base + pk::kTailExt2Fields, // Gentlr's High band, No Overlap and Slope in it: pk::kTailExt3Fields entries
    kTailExt4Base = kTailExt3Base + pk::kTailExt3Fields, // Gentlr's glue in it: pk::kTailExt4Fields entries
    // Widr's parameters through 0.19: the block another plug-in hosting Widr's engine holds (Smemplr's
    // rack: paramTable ()); the cinema stage after them is the plug-in's own (pluginParamTable ())
    kNumParams = kTailExt4Base + pk::kTailExt4Fields,
    // 0.20: the cinema stage (all of it off while Cinema is 0: Widr as before, bit for bit)
    kCinema = kNumParams, // 0 .. 1: blends in the element lanes, Depth and Theatre
    kDepth,      // 0 .. 1: sub-octave and a slow low shelf on the Bass lane (scaled by Cinema)
    kTheatre,    // 0 .. 1: a large dark hall fed by the Wide and Beyond lanes (scaled by Cinema)
    kLaneBase,   // per element lane (Lane): its Position (Centre / Wide / Beyond), then its Width (0 .. 1)
    kNumPluginParams = kLaneBase + 2 * 5
};
static_assert (kNumParams == 62 && kCinema == 62 && kLaneBase == 65 && kNumPluginParams == 75, "parameter IDs are persisted: only ever append");

// The element lanes the cinema stage separates the input into (Lanes.h).
enum Lane { kLaneVoice = 0, kLaneBass, kLaneHits, kLaneTones, kLaneAmbience, kNumLanes };
enum Position { kPosCentre = 0, kPosWide, kPosBeyond, kNumPositions };
constexpr uint32_t lanePosition (int lane) { return (uint32_t)(kLaneBase + 2 * lane); }
constexpr uint32_t laneWidth (int lane) { return (uint32_t)(kLaneBase + 2 * lane + 1); }
static_assert (laneWidth (kNumLanes - 1) == kNumPluginParams - 1, "two parameters per lane");

enum Character { kTight = 0, kWide, kEpic, kSurround, kNumCharacters };
enum Role { kAnchor = 0, kSupport, kWideRole, kAmbient, kNumRoles };

constexpr double kLevelMinDb = -60.0; // the bottom of the Dry / Wet levels is -inf
inline double levelGain (double db) { return db <= kLevelMinDb + 0.01 ? 0.0 : std::pow (10.0, db / 20.0); }

// Widr's parameters through 0.19 (kNumParams: what Smemplr's rack hosts), and the plug-in's: those and
// the cinema stage (kNumPluginParams). The first is the start of the second, entry for entry.
const pk::ParamTable& paramTable ();
const pk::ParamTable& pluginParamTable ();
inline double toPlain (uint32_t id, double n) { return pluginParamTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double p) { return pluginParamTable ().toNormalized (id, p); }
inline double defaultNormalized (uint32_t id) { return pluginParamTable ().defaultNormalized (id); }

} // namespace widr

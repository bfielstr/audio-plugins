// Parameter table shared by the processor, the controller and the tests.
// IDs are persisted in presets/projects: only ever append new IDs.
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/TailParams.h"

#include "multidyn/src/core/Params.h"
#include "para/src/core/Params.h"

#include <cstdint>
#include <string>
#include <vector>

namespace smempler {

// The effects rack after the sampler: kRackSlots slots, each Empty or one of the suite's effects, in
// any order (the same effect may sit in several). A slot is a Type, an On and a block of kSlotBlock
// parameters that the slot's effect reads through its own table (Rack.h: fxTable), so the IDs stay
// the same whatever is loaded where.
enum FxType { kFxEmpty = 0, kFxPara, kFxMultidyn, kFxMsEq, kFxSmacheratr, kFxWidr, kNumFxTypes };
constexpr int kRackSlots = 8;
constexpr uint32_t kSlotBlock = 62; // the largest effect's parameter count (Multidyn, see fxBlockTable)
// Multidyn's parameter count in 0.5, when it was fixed after the sampler (its later ones are not there)
constexpr uint32_t kLegacyMdParams = 62;
enum SlotField : uint32_t { kSlotType = 0, kSlotOn, kSlotParams };
constexpr uint32_t kSlotSize = kSlotParams + kSlotBlock;

enum ParamId : uint32_t
{
    // Sample / playback modes
    kMode = 0,
    kSampleStart,
    kSampleEnd,
    kStart,
    kLength,
    kLoopOn,
    kLoopLen, // unused since 0.6: Length is the loop length
    kLoopFade,
    kSnap,
    kGain,
    kVoices,
    kRetrig,
    kTriggerGate,
    kFadeIn,
    kFadeOut,
    kSliceBy,
    kSensitivity,
    kDivision,
    kRegions,
    kSlicePlayback,
    // Warp
    kWarp,
    kWarpMode,
    kWarpBeats,
    kBeatsPreserve,
    kBeatsLoop,
    kBeatsEnvelope,
    kTonesGrain,
    kTextureGrain,
    kTextureFlux,
    kFormants,
    kCproEnvelope,
    // Filter
    kFilterOn,
    kFilterType,
    kFilterCircuit,
    kFilterSlope,
    kFilterFreq,
    kFilterRes,
    kFilterDrive,
    kFilterMorph,
    kFilterVel,
    kFilterKey,
    kFilterEnvAmt,
    // Envelopes
    kAmpA,
    kAmpD,
    kAmpS,
    kAmpR,
    kFiltA,
    kFiltD,
    kFiltS,
    kFiltR,
    kPitchA,
    kPitchD,
    kPitchS,
    kPitchR,
    kPitchEnvAmt,
    kAmpLoopMode,
    kAmpLoopTime,
    kAmpLoopRate,
    // LFO
    kLfoOn,
    kLfoWave,
    kLfoSync,
    kLfoRate,
    kLfoSyncRate,
    kLfoAttack,
    kLfoRetrig,
    kLfoOffset,
    kLfoKey,
    kLfoVol,
    kLfoPitch,
    kLfoPan,
    kLfoFilter,
    // Global
    kPan,
    kPanRand,
    kSpread,
    kVolume,
    kVelVol,
    kTranspose,
    kDetune,
    kPbRange,
    kGlideMode,
    kGlideTime,
    // --- added in 0.2 (append only) ---
    kLoopFadePower, // constant-power (on) or linear loop crossfade
    kEnvExtBase,    // per-envelope curves and breakpoints, see envParam()
    // --- added in 0.5 (append only): the built-in effects after the sampler, see fxParam() ---
    kFxParaOn = kEnvExtBase + 3 * 22,
    kFxParaBase, // para::kHostedParams entries (Para's own IDs, offset)
    kFxMdOn = kFxParaBase + para::kHostedParams,
    kFxMdBase, // kLegacyMdParams entries (its own end-of-chain saturator is not used here)
    kMsOn = kFxMdBase + kLegacyMdParams, // mid/side EQ after the effects
    kMsSideHp,   // Hz, high-pass on the side signal
    kMsSlope,    // 6 / 12 / 24 dB per octave
    kMsSideGain, // dB
    kMsMidGain,  // dB
    kRootKey,    // the root note: the sample plays at its own pitch on this note
    kTailBase,   // the Smacheratr at the very end: pk::kTailFields entries
    kParaTransposeLock = kTailBase + pk::kTailFields, // unused since Para stopped tracking notes
    kParaDragGain,      // Para's display: dragging a handle moves its gain with the resonance
    kParaLiquid,        // Para's Liquid movement
    kParaFade,          // Para's Vocal fade range
    kParaNotch,         // Para's Liquid notch
    // --- added in 0.6: the effects rack (Rack.h); the fixed Para / Multidyn / M/S EQ above are
    // no longer used (old projects are moved into the rack when they load) ---
    kRackBase,
    // --- added in 0.7: the rest of the end-of-chain Smacheratr (smacheratr/src/core/TailExt.h) ---
    kTailExtBase = kRackBase + kRackSlots * kSlotSize,

    kNumParams = kTailExtBase + pk::kTailExtFields
};

constexpr uint32_t slotParam (int slot, uint32_t field) { return kRackBase + (uint32_t)slot * kSlotSize + field; }
constexpr uint32_t slotBlockParam (int slot, uint32_t id) { return slotParam (slot, kSlotParams + id); }
constexpr bool isRackParam (uint32_t id) { return id >= kRackBase && id < kTailExtBase; }
// the end-of-chain Smacheratr's parameters (both blocks), and their field in smacheratr::Tail
constexpr bool isTailParam (uint32_t id)
{
    return (id >= kTailBase && id < kTailBase + pk::kTailFields) || (id >= kTailExtBase && id < kTailExtBase + pk::kTailExtFields);
}
constexpr uint32_t tailField (uint32_t id) { return id >= kTailExtBase ? pk::kTailFields + (id - kTailExtBase) : id - kTailBase; }

constexpr uint32_t paraParam (uint32_t id) { return kFxParaBase + id; }

constexpr uint32_t multidynParam (uint32_t id) { return kFxMdBase + id; }

// Breakpoint-envelope parameters. Each envelope (0 amp, 1 filter, 2 pitch) owns a block of
// 22 IDs: curves for attack/decay/release, the breakpoint count, then (time, level, curve)
// for up to kMaxEnvPoints breakpoints that sit between the attack peak and the decay segment.
constexpr int kMaxEnvPoints = 6;
constexpr int kEnvBlock = 22;
enum EnvField { kEnvCurveA = 0, kEnvCurveD, kEnvCurveR, kEnvPointCount, kEnvPointsStart };
enum PointField { kPtTime = 0, kPtLevel, kPtCurve };
constexpr uint32_t envParam (int env, int field) { return (uint32_t)(kEnvExtBase + env * kEnvBlock + field); }
constexpr uint32_t envPointParam (int env, int point, int field)
{
    return envParam (env, kEnvPointsStart + point * 3 + field);
}
// First ADSR parameter (attack) of each envelope; D, S, R follow.
constexpr uint32_t envAdsrBase (int env) { return env == 0 ? kAmpA : (env == 1 ? kFiltA : kPitchA); }

// Hidden parameters that receive MIDI controllers through IMidiMapping.
enum MidiParamId : uint32_t
{
    kMidiPitchBend = 1000,
    kMidiSustain = 1001,
    kMidiModWheel = 1002,
};
static_assert (kNumParams <= kMidiPitchBend, "the parameters run into the hidden MIDI ones");

using pk::Curve;
using pk::Disp;
using pk::ParamInfo;
using pk::PType;

const pk::ParamTable& paramTable ();
inline const ParamInfo& paramInfo (uint32_t id) { return paramTable ().info (id); }
inline bool isValidParam (uint32_t id) { return id < kNumParams; }
inline double toPlain (uint32_t id, double n) { return paramTable ().toPlain (id, n); }
inline double toNormalized (uint32_t id, double plain) { return paramTable ().toNormalized (id, plain); }
inline double defaultNormalized (uint32_t id) { return paramTable ().defaultNormalized (id); }
inline std::string toText (uint32_t id, double plain) { return paramTable ().toText (id, plain); }
inline bool fromText (uint32_t id, const std::string& t, double& out) { return paramTable ().fromText (id, t, out); }

// Enumerations -------------------------------------------------------------
enum Mode { kModeClassic = 0, kModeOneShot, kModeSlicing };
enum SliceBy { kSliceTransient = 0, kSliceBeat, kSliceRegion, kSliceManual };
enum SlicePlayback { kSliceMono = 0, kSlicePoly, kSliceThru };
enum WarpMode { kWarpBeatsMode = 0, kWarpTones, kWarpTexture, kWarpRePitch, kWarpComplex, kWarpComplexPro };
enum FilterType { kLowpass = 0, kHighpass, kBandpass, kNotch, kMorph };
enum FilterCircuit { kClean = 0, kOSR, kMS2, kSMP, kPRD };
enum AmpLoop { kAmpLoopNone = 0, kAmpLoopTrigger, kAmpLoopLoop, kAmpLoopBeat, kAmpLoopSync };
enum LfoWave { kSine = 0, kSquare, kTriangle, kSawDown, kSawUp, kRandom };
enum GlideMode { kGlideOff = 0, kGlideMono, kGlidePorta };

int voicesFromIndex (int index);
double syncDivisionBeats (int index); // length in quarter-note beats
int regionsFromIndex (int index);
double divisionBeats (int index);     // slicing "Beat" divisions
double preserveBeats (int index);     // warp Beats "Preserve" (0 = transients)
bool circuitSupported (int type, int circuit);

// Slices map chromatically upwards from this note (C1 in Live's naming).
constexpr int kSliceBaseNote = 36;
// Notes play the sample at its original pitch at C3 (MIDI 60).
constexpr int kRootNote = 60;

} // namespace smempler

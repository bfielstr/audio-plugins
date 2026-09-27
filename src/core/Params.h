// Parameter table shared by the processor, the controller and the tests.
// IDs are persisted in presets/projects: only ever append new IDs.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace simplr {

enum ParamId : uint32_t
{
    // Sample / playback modes
    kMode = 0,
    kSampleStart,
    kSampleEnd,
    kStart,
    kLength,
    kLoopOn,
    kLoopLen,
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

    kNumParams = kEnvExtBase + 3 * 22
};

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

enum class PType { Float, Int, Choice, Bool };
enum class Curve { Linear, Log, Power3 };
enum class Disp { Percent, Hz, Ms, Db, DbGain, Semis, Cents, Pan, Plain, Beats, Degrees, Choice, OnOff, Sustain, Curve };

struct ParamInfo
{
    ParamId id;
    const char* name;
    const char* shortName;
    PType type;
    double min, max, def; // plain values (choices: index)
    Curve curve;
    Disp disp;
    std::vector<const char*> choices;

    int stepCount () const; // 0 = continuous
};

const ParamInfo& paramInfo (uint32_t id);
bool isValidParam (uint32_t id);

double toPlain (uint32_t id, double normalized);
double toNormalized (uint32_t id, double plain);
double defaultNormalized (uint32_t id);
std::string toText (uint32_t id, double plain);
bool fromText (uint32_t id, const std::string& text, double& plainOut);

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

} // namespace simplr

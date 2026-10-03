#include "Params.h"

#include "smacheratr/src/core/TailExt.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace smemplr {

namespace {

const std::vector<const char*> kSyncDivisionNames = {
    "1/64", "1/48", "1/32", "1/24", "1/16", "1/12", "1/8", "1/6", "3/16", "1/4", "5/16",
    "1/3", "3/8", "1/2", "3/4", "1 Bar", "1.5 Bars", "2 Bars", "3 Bars", "4 Bars", "6 Bars", "8 Bars"};
const double kSyncDivisionBeats[] = {
    1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 1.0 / 2, 2.0 / 3, 3.0 / 4, 1.0, 5.0 / 4,
    4.0 / 3, 3.0 / 2, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0};
constexpr int kDefaultSyncIndex = 9; // 1/4

const std::vector<const char*> kVoiceNames = {"1", "2", "3", "4", "5", "6", "7", "8",
                                              "10", "12", "14", "16", "20", "24", "32"};
const int kVoiceCounts[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 32};

const std::vector<const char*> kDivisionNames = {"1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars"};
const double kDivisionBeats[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};

const std::vector<const char*> kRegionNames = {"2", "4", "8", "16", "32", "64"};
const int kRegionCounts[] = {2, 4, 8, 16, 32, 64};

const std::vector<const char*> kPreserveNames = {"Transients", "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/32"};
const double kPreserveBeats[] = {0.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125};

using P = ParamInfo;

// A new Smemplr's rack: Smacheratr in the first slot with its own defaults, the other slots empty. A
// slot's block is stored normalized, and Smacheratr's block positions are its own IDs.
double slotDefault (int slot, uint32_t j)
{
    const auto& st = smacheratr::paramTable ();
    return slot == 0 && j < st.size () ? st.defaultNormalized (j) : 0.0;
}

std::vector<ParamInfo> buildTable ()
{
    std::vector<ParamInfo> t;
    auto add = [&] (ParamInfo p) { t.push_back (std::move (p)); };
    auto choice = [] (ParamId id, const char* n, const char* sn, std::vector<const char*> c, int def) {
        return P {id, n, sn, PType::Choice, 0.0, double (c.size () - 1), double (def), Curve::Linear, Disp::Choice, std::move (c)};
    };
    auto toggle = [] (ParamId id, const char* n, const char* sn, bool def) {
        return P {id, n, sn, PType::Bool, 0.0, 1.0, def ? 1.0 : 0.0, Curve::Linear, Disp::OnOff, {}};
    };
    auto pct = [] (ParamId id, const char* n, const char* sn, double def) {
        return P {id, n, sn, PType::Float, 0.0, 1.0, def, Curve::Linear, Disp::Percent, {}};
    };
    auto fl = [] (ParamId id, const char* n, const char* sn, double mn, double mx, double def, Curve c, Disp d) {
        return P {id, n, sn, PType::Float, mn, mx, def, c, d, {}};
    };
    auto in = [] (ParamId id, const char* n, const char* sn, double mn, double mx, double def, Disp d) {
        return P {id, n, sn, PType::Int, mn, mx, def, Curve::Linear, d, {}};
    };

    add (choice (kMode, "Playback Mode", "Mode", {"Classic", "One-Shot", "Slicing"}, 0));
    add (pct (kSampleStart, "Sample Start", "S.Start", 0.0));
    add (pct (kSampleEnd, "Sample End", "S.End", 1.0));
    add (pct (kStart, "Start", "Start", 0.0));
    add (pct (kLength, "Length", "Length", 1.0)); // the loop's length
    add (toggle (kLoopOn, "Loop On", "Loop", true));
    add (pct (kLoopLen, "Loop Length (unused)", "Unused", 1.0)); // kept for old projects; Length sets the loop
    add (pct (kLoopFade, "Loop Fade", "Fade", 0.0));
    add (toggle (kSnap, "Snap", "Snap", false));
    add (fl (kGain, "Sample Gain", "Gain", -36.0, 24.0, 0.0, Curve::Linear, Disp::DbGain));
    add (choice (kVoices, "Voices", "Voices", kVoiceNames, 0)); // 1 voice
    add (toggle (kRetrig, "Retrigger", "Retrig", false));
    add (choice (kTriggerGate, "Trigger Mode", "Trig/Gate", {"Trigger", "Gate"}, 0));
    add (fl (kFadeIn, "Fade In", "Fade In", 0.0, 2000.0, 0.0, Curve::Power3, Disp::Ms));
    add (fl (kFadeOut, "Fade Out", "Fade Out", 0.0, 2000.0, 0.1, Curve::Power3, Disp::Ms));
    add (choice (kSliceBy, "Slice By", "Slice By", {"Transient", "Beat", "Region", "Manual"}, 0));
    add (pct (kSensitivity, "Slice Sensitivity", "Sensitivity", 0.5));
    add (choice (kDivision, "Slice Division", "Division", kDivisionNames, 0));
    add (choice (kRegions, "Slice Regions", "Regions", kRegionNames, 2));
    add (choice (kSlicePlayback, "Slice Playback", "Playback", {"Mono", "Poly", "Thru"}, 0));

    add (toggle (kWarp, "Warp", "Warp", false));
    add (choice (kWarpMode, "Warp Mode", "Warp Mode",
                 {"Beats", "Tones", "Texture", "Re-Pitch", "Complex", "Complex Pro"}, 0));
    add (in (kWarpBeats, "Warp Length", "Warp As", 1.0, 1024.0, 16.0, Disp::Beats));
    add (choice (kBeatsPreserve, "Preserve", "Preserve", kPreserveNames, 0));
    add (choice (kBeatsLoop, "Transient Loop Mode", "Loop Mode", {"Off", "Forward", "Back-Forth"}, 1));
    add (fl (kBeatsEnvelope, "Transient Envelope", "Envelope", 0.0, 100.0, 100.0, Curve::Linear, Disp::Plain));
    add (fl (kTonesGrain, "Tones Grain Size", "Grain", 1.0, 100.0, 30.0, Curve::Linear, Disp::Plain));
    add (fl (kTextureGrain, "Texture Grain Size", "Grain", 1.0, 100.0, 65.0, Curve::Linear, Disp::Plain));
    add (fl (kTextureFlux, "Texture Flux", "Flux", 0.0, 100.0, 25.0, Curve::Linear, Disp::Plain));
    add (fl (kFormants, "Formants", "Formants", 0.0, 100.0, 100.0, Curve::Linear, Disp::Plain));
    add (in (kCproEnvelope, "Spectral Envelope", "Envelope", 8.0, 256.0, 128.0, Disp::Plain));

    add (toggle (kFilterOn, "Filter On", "Filter", true));
    add (choice (kFilterType, "Filter Type", "Type", {"Lowpass", "Highpass", "Bandpass", "Notch", "Morph"}, 0));
    add (choice (kFilterCircuit, "Filter Circuit", "Circuit", {"Clean", "OSR", "MS2", "SMP", "PRD"}, 0));
    add (choice (kFilterSlope, "Filter Slope", "Slope", {"12 dB", "24 dB"}, 0));
    add (fl (kFilterFreq, "Filter Frequency", "Freq", 30.0, 22000.0, 22000.0, Curve::Log, Disp::Hz));
    add (fl (kFilterRes, "Filter Resonance", "Res", 0.0, 1.0, 0.0, Curve::Linear, Disp::Percent));
    add (fl (kFilterDrive, "Filter Drive", "Drive", 0.0, 24.0, 0.0, Curve::Linear, Disp::Db));
    add (pct (kFilterMorph, "Filter Morph", "Morph", 0.0));
    add (pct (kFilterVel, "Filter Velocity", "Vel", 0.0));
    add (pct (kFilterKey, "Filter Key Track", "Key", 0.0));
    add (fl (kFilterEnvAmt, "Filter Envelope Amount", "Env", -72.0, 72.0, 0.0, Curve::Linear, Disp::Semis));

    add (fl (kAmpA, "Amp Attack", "Attack", 0.1, 20000.0, 0.1, Curve::Log, Disp::Ms));
    add (fl (kAmpD, "Amp Decay", "Decay", 1.0, 60000.0, 600.0, Curve::Log, Disp::Ms));
    add (fl (kAmpS, "Amp Sustain", "Sustain", 0.0, 1.0, 1.0, Curve::Linear, Disp::Sustain));
    add (fl (kAmpR, "Amp Release", "Release", 1.0, 60000.0, 50.0, Curve::Log, Disp::Ms));
    add (fl (kFiltA, "Filter Env Attack", "Attack", 0.1, 20000.0, 0.1, Curve::Log, Disp::Ms));
    add (fl (kFiltD, "Filter Env Decay", "Decay", 1.0, 60000.0, 600.0, Curve::Log, Disp::Ms));
    add (fl (kFiltS, "Filter Env Sustain", "Sustain", 0.0, 1.0, 0.0, Curve::Linear, Disp::Sustain));
    add (fl (kFiltR, "Filter Env Release", "Release", 1.0, 60000.0, 50.0, Curve::Log, Disp::Ms));
    add (fl (kPitchA, "Pitch Env Attack", "Attack", 0.1, 20000.0, 0.1, Curve::Log, Disp::Ms));
    add (fl (kPitchD, "Pitch Env Decay", "Decay", 1.0, 60000.0, 600.0, Curve::Log, Disp::Ms));
    add (fl (kPitchS, "Pitch Env Sustain", "Sustain", 0.0, 1.0, 0.0, Curve::Linear, Disp::Sustain));
    add (fl (kPitchR, "Pitch Env Release", "Release", 1.0, 60000.0, 50.0, Curve::Log, Disp::Ms));
    add (fl (kPitchEnvAmt, "Pitch Envelope Amount", "Amount", -48.0, 48.0, 0.0, Curve::Linear, Disp::Semis));
    add (choice (kAmpLoopMode, "Amp Loop Mode", "Loop", {"None", "Trigger", "Loop", "Beat", "Sync"}, 0));
    add (fl (kAmpLoopTime, "Amp Loop Time", "Time", 0.1, 20000.0, 100.0, Curve::Log, Disp::Ms));
    add (choice (kAmpLoopRate, "Amp Loop Rate", "Rate", kSyncDivisionNames, kDefaultSyncIndex));

    add (toggle (kLfoOn, "LFO On", "LFO", false));
    add (choice (kLfoWave, "LFO Waveform", "Wave", {"Sine", "Square", "Triangle", "Saw Down", "Saw Up", "Random"}, 0));
    add (choice (kLfoSync, "LFO Rate Type", "Hz/Sync", {"Hz", "Sync"}, 0));
    add (fl (kLfoRate, "LFO Rate", "Rate", 0.01, 30.0, 1.0, Curve::Log, Disp::Hz));
    add (choice (kLfoSyncRate, "LFO Sync Rate", "Rate", kSyncDivisionNames, kDefaultSyncIndex));
    add (fl (kLfoAttack, "LFO Attack", "Attack", 0.0, 15000.0, 0.0, Curve::Power3, Disp::Ms));
    add (toggle (kLfoRetrig, "LFO Retrigger", "R", false));
    add (fl (kLfoOffset, "LFO Offset", "Offset", 0.0, 360.0, 0.0, Curve::Linear, Disp::Degrees));
    add (pct (kLfoKey, "LFO Key", "Key", 0.0));
    add (pct (kLfoVol, "LFO > Volume", "Volume", 0.0));
    add (pct (kLfoPitch, "LFO > Pitch", "Pitch", 0.0));
    add (pct (kLfoPan, "LFO > Pan", "Pan", 0.0));
    add (pct (kLfoFilter, "LFO > Filter", "Filter", 0.0));

    add (fl (kPan, "Pan", "Pan", -1.0, 1.0, 0.0, Curve::Linear, Disp::Pan));
    add (pct (kPanRand, "Random Pan", "Rand", 0.0));
    add (pct (kSpread, "Spread", "Spread", 0.0));
    add (fl (kVolume, "Volume", "Volume", -70.0, 6.0, -12.0, Curve::Linear, Disp::DbGain));
    add (pct (kVelVol, "Velocity > Volume", "Vel>Vol", 0.35));
    add (in (kTranspose, "Transpose", "Transp", -48.0, 48.0, 0.0, Disp::Semis));
    add (fl (kDetune, "Detune", "Detune", -50.0, 50.0, 0.0, Curve::Linear, Disp::Cents));
    add (in (kPbRange, "Pitch Bend Range", "PB Range", 0.0, 48.0, 5.0, Disp::Semis));
    add (choice (kGlideMode, "Glide Mode", "Glide", {"Off", "Glide", "Portamento"}, 0));
    add (fl (kGlideTime, "Glide Time", "Time", 1.0, 10000.0, 50.0, Curve::Log, Disp::Ms));
    add (toggle (kLoopFadePower, "Loop Fade Constant Power", "Const Pwr", true));

    // Envelope curves and breakpoints. Names are generated, so keep them alive in a deque.
    auto keep = [] (std::string s) { return pk::make::keep (std::move (s)); };
    const char* envNames[] = {"Amp", "Filter", "Pitch"};
    for (int e = 0; e < 3; ++e)
    {
        const std::string en = envNames[e];
        auto curve = [&] (uint32_t id, const std::string& seg, double def) {
            add (fl ((ParamId)id, keep (en + " Env " + seg + " Curve"), keep (seg + " Curve"), -1.0, 1.0, def,
                     Curve::Linear, Disp::Curve));
        };
        curve (envParam (e, kEnvCurveA), "Attack", 0.0);
        curve (envParam (e, kEnvCurveD), "Decay", -0.5);
        curve (envParam (e, kEnvCurveR), "Release", -0.5);
        add (in ((ParamId)envParam (e, kEnvPointCount), keep (en + " Env Breakpoints"), "Points", 0.0,
                 (double)kMaxEnvPoints, 0.0, Disp::Plain));
        for (int i = 0; i < kMaxEnvPoints; ++i)
        {
            const std::string pn = en + " Env Point " + std::to_string (i + 1);
            const std::string sn = "P" + std::to_string (i + 1);
            add (fl ((ParamId)envPointParam (e, i, kPtTime), keep (pn + " Time"), keep (sn + " Time"), 0.1, 20000.0,
                     100.0, Curve::Log, Disp::Ms));
            add (fl ((ParamId)envPointParam (e, i, kPtLevel), keep (pn + " Level"), keep (sn + " Level"), 0.0, 1.0,
                     0.5, Curve::Linear, Disp::Percent));
            add (fl ((ParamId)envPointParam (e, i, kPtCurve), keep (pn + " Curve"), keep (sn + " Curve"), -1.0, 1.0,
                     0.0, Curve::Linear, Disp::Curve));
        }
    }
    // the fixed effects of 0.5 (kept so old projects load; they move into the rack)
    add (toggle (kFxParaOn, "Old Para On", "Para", false));
    for (uint32_t i = 0; i < para::kHostedParams; ++i)
    {
        ParamInfo pi = para::paramTable ().info (i);
        pi.id = paraParam (i);
        // (Para's one Slope of then is its High-Pass Slope now)
        pi.name = keep (std::string ("Old Para ") + (i == para::kHpSlope ? "Slope" : pi.name));
        add (pi);
    }
    add (toggle (kFxMdOn, "Old Multidyn On", "Multidyn", false));
    for (uint32_t i = 0; i < kLegacyMdParams; ++i)
    {
        ParamInfo pi = multidyn::paramTable ().info (i);
        pi.id = multidynParam (i);
        pi.name = keep (std::string ("Old Multidyn ") + pi.name);
        add (pi);
    }
    // after the effects: the mid/side EQ, the root note, the end-of-chain Smacheratr
    add (toggle (kMsOn, "Old M/S EQ On", "M/S", false));
    add (fl (kMsSideHp, "Old Side High-Pass", "Side HP", 20.0, 2000.0, 150.0, Curve::Log, Disp::Hz));
    add (choice (kMsSlope, "Old Side High-Pass Slope", "Slope", {"6 dB", "12 dB", "24 dB"}, 2));
    add (fl (kMsSideGain, "Old Side Gain", "Side", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
    add (fl (kMsMidGain, "Old Mid Gain", "Mid", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
    add (P {kRootKey, "Root Note", "Root", PType::Int, 0.0, 127.0, (double)kRootNote, Curve::Linear, Disp::Note, {}});
    // the old saturator after the rack (before 0.9; a Smacheratr slot does its job now, and old projects
    // get one when they load): off, not in the editor (its other controls are at kTailExtBase)
    auto markOld = [&] (size_t from) {
        for (size_t i = from; i < t.size (); ++i)
            t[i].name = keep (std::string ("Old End ") + t[i].name);
    };
    const size_t tailAt = t.size ();
    pk::addTailParams (t, kTailBase, false);
    markOld (tailAt);
    add (toggle (kParaTransposeLock, "Old Para Transpose Lock", "Lock", false));
    add (toggle (kParaDragGain, "Old Para Drag Gain", "Drag Gain", false));
    add (toggle (kParaLiquid, "Old Para Liquid", "Liquid", false));
    // Para's Fade as it was in 0.5 (1 .. 36 semitones, 12 by default; Para's is 1 .. 60 now): its saved
    // values keep their meaning (StateIO.cpp moves it into a slot, migrateParaInSlots converts it)
    add (fl (kParaFade, "Old Para Vocal Fade", "Fade", 1.0, para::kFadeOldMax, para::kFadeOldDefault, Curve::Log, Disp::Semis));
    add (toggle (kParaNotch, "Old Para Liquid Notch", "Notch", false));
    // the effects rack: per slot a Type, an On and a block of values (normalized; each effect reads
    // them through its own table, and the controller shows them that way). A new Smemplr starts with
    // Smacheratr in the first slot, with its own defaults (kDefaultSlotType, slotDefault).
    for (int s = 0; s < kRackSlots; ++s)
    {
        const std::string fx = "FX " + std::to_string (s + 1);
        add (choice ((ParamId)slotParam (s, kSlotType), keep (fx + " Type"), keep (fx), {"Empty", "para", "multidyn", "m/s eq", "smacheratr", "widr", "wubr", "levlr", "gently", "smoothr"},
                     s == 0 ? kDefaultSlotType : kFxEmpty));
        add (toggle ((ParamId)slotParam (s, kSlotOn), keep (fx + " On"), keep (fx + " On"), true));
        for (uint32_t j = 0; j < kSlotBlock; ++j)
        {
            const std::string n = fx + " " + std::to_string (j + 1);
            add (fl ((ParamId)slotBlockParam (s, j), keep (n), keep (n), 0.0, 1.0, slotDefault (s, j), Curve::Linear, Disp::Percent));
        }
    }
    // the rest of the old end-of-chain Smacheratr
    const size_t tailExtAt = t.size ();
    smacheratr::addTailExtParams (t, kTailExtBase);
    markOld (tailExtAt);
    // the slots' extensions
    for (int s = 0; s < kRackSlots; ++s)
        for (uint32_t j = kSlotBlock; j < kSlotBlockAll; ++j)
        {
            const std::string n = "FX " + std::to_string (s + 1) + " " + std::to_string (j + 1);
            add (fl ((ParamId)slotBlockParam (s, j), keep (n), keep (n), 0.0, 1.0, slotDefault (s, j), Curve::Linear, Disp::Percent));
        }
    // the high-pass that follows Transpose (off: a new or old project sounds as before)
    add (toggle (kTransHpOn, "Transpose HP", "HP", false));
    add (fl (kTransHpFreq, "Transpose HP Frequency", "HP Freq", 10.0, 200.0, 20.0, Curve::Log, Disp::Hz));
    add (choice (kTransHpSlope, "Transpose HP Slope", "HP Slope", {"6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB"},
                 kTransHp24));
    // the modulation LFOs (what they modulate is in the state: Modulation.h). Sync is Off (Rate in Hz) or a
    // note length of the host's tempo.
    std::vector<const char*> syncNames {"Off"};
    syncNames.insert (syncNames.end (), kSyncDivisionNames.begin (), kSyncDivisionNames.end ());
    for (int l = 0; l < kModLfos; ++l)
    {
        const std::string n = "Mod LFO " + std::to_string (l + 1);
        add (choice ((ParamId)modLfoParam (l, kModShape), keep (n + " Shape"), "Shape",
                     {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Random"}, kModSine));
        add (fl ((ParamId)modLfoParam (l, kModRate), keep (n + " Rate"), "Rate", 0.01, 40.0, 1.0, Curve::Log, Disp::Hz));
        add (choice ((ParamId)modLfoParam (l, kModSync), keep (n + " Sync"), "Sync", syncNames, 0));
        add (fl ((ParamId)modLfoParam (l, kModPhase), keep (n + " Phase"), "Phase", 0.0, 360.0, 0.0, Curve::Linear, Disp::Degrees));
        add (toggle ((ParamId)modLfoParam (l, kModRetrig), keep (n + " Retrigger"), "Retrig", false));
    }
    // the filter and pitch envelopes locked to the loop (Engine.cpp: Voice::render)
    add (choice (kFiltLoopLock, "Filter Env Loop Lock", "Lock", {"Off", "Restart", "Fit"}, kLoopLockOff));
    add (choice (kPitchLoopLock, "Pitch Env Loop Lock", "Lock", {"Off", "Restart", "Fit"}, kLoopLockOff));
    return t;
}

} // namespace

const pk::ParamTable& paramTable ()
{
    static const pk::ParamTable t (buildTable ());
    return t;
}

int voicesFromIndex (int i) { return kVoiceCounts[std::clamp (i, 0, 14)]; }
double syncDivisionBeats (int i) { return kSyncDivisionBeats[std::clamp (i, 0, 21)]; }
int regionsFromIndex (int i) { return kRegionCounts[std::clamp (i, 0, 5)]; }
double divisionBeats (int i) { return kDivisionBeats[std::clamp (i, 0, 6)]; }
double preserveBeats (int i) { return kPreserveBeats[std::clamp (i, 0, 6)]; }

bool circuitSupported (int type, int circuit)
{
    if (circuit == kClean || circuit == kOSR)
        return true;
    return type == kLowpass || type == kHighpass;
}

} // namespace smemplr

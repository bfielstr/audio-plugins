// The instrument: voice allocation, playback modes and per-voice processing. Framework-free so
// it can be driven by the VST3 processor and by the headless tests alike.
#pragma once

#include "Envelope.h"
#include "Filter.h"
#include "Lfo.h"
#include "MsEq.h"
#include "Params.h"
#include "SampleData.h"
#include "Slices.h"
#include "Warp.h"

#include "multidyn/src/core/Engine.h"
#include "para/src/core/Engine.h"
#include "smacheratr/src/core/Tail.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace smempler {

struct HostInfo
{
    double bpm = 120.0;
    double ppq = 0.0; // quarter notes at the start of the block
    bool playing = false;
    bool ppqValid = false;
};

using ParamArray = std::array<double, kNumParams>; // plain values

ParamArray defaultParams ();

// The root note: the sample plays at its own pitch on it.
inline int rootOf (const ParamArray& p) { return std::clamp ((int)std::lround (p[kRootKey]), 0, 127); }

// Computes the [start, end) flag region in frames (with snapping applied).
void flagRegion (const SampleData& s, const ParamArray& p, double& fs, double& fe);
double sampleBpmFor (const SampleData& s, const ParamArray& p);
// Classic-mode playback region (Start/Length/Loop inside the flags).
bool classicRegion (const SampleData& s, const ParamArray& p, PlayRegion& r);
SliceSettings sliceSettingsFor (const SampleData& s, const ParamArray& p);
// Envelope settings (ADSR, curves, breakpoints) for env 0 amp, 1 filter, 2 pitch.
EnvSettings envSettingsFor (const ParamArray& p, int env);

struct BlockCtx
{
    const SampleData* sample = nullptr;
    const ParamArray* p = nullptr;
    double sr = 44100.0;
    HostInfo host;
    double srcRate = 1.0;      // sample rate conversion factor
    double srcPerOut = 1.0;    // warp timeline speed
    float bendSemis = 0.0f;
    bool constantPowerFade = true;
    double ppqPerSample = 0.0;
    double globalLfoPhase = 0.0;
};

class Voice
{
public:
    enum class Source { Classic, Beats, Grain, Pv };

    struct Start
    {
        int note = 60;
        float velocity = 1.0f; // 0..1
        int mode = kModeClassic;
        PlayRegion region;
        bool warp = false;
        int warpMode = 0;
        bool gate = false;
        double pitchBase = 0.0; // semitones relative to the root note
        double glideFrom = 0.0; // semitone offset that glides to zero
        float spreadSemis = 0.0f;
        float panOffset = 0.0f; // random pan
        float sidePan = 0.0f;   // spread voices: -1 / +1
        int group = 0;
        uint64_t age = 0;
        double lfoPhase = 0.0;
        uint32_t seed = 1;
        const std::vector<int>* beatBounds = nullptr;
    };

    void prepare (double sr);
    void start (const Start& s, const SampleData& sample, const ParamArray& p);
    void release (const ParamArray& p);
    void kill (); // fast fade (voice stealing / retrigger)
    void hardStop () { active = false; }
    void glideTo (int newNote, double newPitchBase, double glideMs);
    void updateLoop (const PlayRegion& r);

    void render (float* outL, float* outR, int n, const BlockCtx& c);

    bool isActive () const { return active; }
    bool isReleased () const { return released; }
    bool isKilling () const { return killing; }
    int note () const { return st.note; }
    int group () const { return st.group; }
    uint64_t age () const { return st.age; }
    double displayPos () const;
    bool sustained = false;

private:
    float sourceRender (float* L, float* R, int n, const BlockCtx& c, double pitchRatio);
    double remainingOut () const; // output samples until the region end (large if looping)
    double lastSrcPerOut = 1.0;

    Start st;
    bool active = false, released = false, killing = false;
    Source source = Source::Classic;
    double sr = 44100.0;

    // classic (resampling) playback
    double pos = 0.0, lastRate = 1.0;
    bool srcDone = false;
    BeatsWarp beats;
    GrainWarp grain;
    PvWarp pv;

    Envelope ampEnv, filtEnv, pitchEnv;
    Lfo lfo;
    MultiFilter filter;
    double glideOffset = 0.0, glideStep = 0.0;
    double beatAcc = 0.0;
    long long lastSyncSlot = -1;
    long long elapsed = 0;
    float killGain = 1.0f, killStep = 0.0f;
    float gateGain = 1.0f, gateStep = 0.0f;
    bool gateFading = false;
    float prevGainL = 0.0f, prevGainR = 0.0f;
    bool firstBlock = true;
    float tmpL[16] {}, tmpR[16] {};
};

class Engine
{
public:
    static constexpr int kMaxVoices = 72;
    static constexpr int kMaxDisplay = 16;

    Engine ();
    void prepare (double sampleRate, int maxBlock);
    void reset ();

    void setSample (SamplePtr s);
    void setSliceEdits (SliceEditsPtr e);
    const SamplePtr& sample () const { return smp; }

    void setParam (uint32_t id, double plain);
    double param (uint32_t id) const { return p[id]; }
    const ParamArray& params () const { return p; }

    void setPitchBend (float bipolar)
    {
        bend = bipolar;
        fxPara.setPitchBend (bipolar);
    }
    // After the sampler: Para, Multidyn, the mid/side EQ and the Smacheratr at the very end. The
    // latency is theirs (Multidyn's look-ahead and the saturator's) and constant.
    int latency () const { return fxMultidyn.latency () + tail.latency (); }
    // Destinations for the editor's displays (may be null).
    void setFxMeters (para::Meters* pm, smacheratr::Meters* sm)
    {
        fxPara.setMeters (pm);
        tail.setMeters (sm);
    }
    const multidyn::BandMeter& fxMultidynMeter (int band) const { return fxMultidyn.meter (band); }
    float msMidPeak () const { return ms.midPeak; }
    float msSidePeak () const { return ms.sidePeak; }
    void setSustain (bool on);
    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allNotesOff ();

    // Renders (adds nothing: overwrites) n samples. Split blocks at events in the caller.
    void render (float* L, float* R, int n, const HostInfo& host);

    int activeVoices () const;
    int playPositions (float* out, int max) const; // normalized positions of sounding voices
    const SliceList& currentSlices () { updateSlices (); return slices; }

private:
    int polyLimit () const;
    bool isMono () const;
    void startNote (int note, float velocity, bool legatoCheck);
    void killGroup (int group);
    void updateSlices ();
    void makeCtx (const HostInfo& host, BlockCtx& c) const;
    void renderEffects (float* L, float* R, int n);
    bool regionFor (int note, PlayRegion& r) const;
    void computeBeatBounds (const PlayRegion& r);

    ParamArray p;
    SamplePtr smp;
    SliceEditsPtr edits;
    SliceList slices;
    SliceSettings sliceKey;
    bool slicesDirty = true;
    std::vector<Voice> voices;
    std::vector<int> beatBounds;
    double sr = 44100.0;
    float bend = 0.0f;
    bool sustain = false;
    uint64_t ageCounter = 0;
    int groupCounter = 0;
    int lastNote = -1;
    double lastPitch = 0.0;
    std::vector<int> monoStack;
    uint32_t seed = 0x1234567u;
    double globalLfoPhase = 0.0;
    float volGain = 0.0f;
    std::vector<float> scratchL, scratchR;
    // built in without their own end-of-chain saturators (Smempler has one at the very end)
    para::Engine fxPara {false};
    multidyn::Engine fxMultidyn {false};
    MsEq ms;
    smacheratr::Tail tail;
};

} // namespace smempler

#include "Engine.h"

#include "Interp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace smempler {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
inline bool on (double v) { return v >= 0.5; }
inline int idx (double v) { return (int)std::lround (v); }
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramInfo (i).def;
    return p;
}

void flagRegion (const SampleData& s, const ParamArray& p, double& fs, double& fe)
{
    const double len = s.length;
    fs = std::clamp (p[kSampleStart], 0.0, 1.0) * len;
    fe = std::clamp (p[kSampleEnd], 0.0, 1.0) * len;
    if (fe < fs)
        std::swap (fs, fe);
    const double minLen = std::min (len, 32.0);
    if (fe - fs < minLen)
    {
        fe = std::min (len, fs + minLen);
        fs = fe - minLen;
    }
    if (on (p[kSnap]))
    {
        fs = s.snapToZero ((int)fs);
        fe = std::max (fs + minLen, (double)s.snapToZero ((int)fe));
        fe = std::min (fe, len);
    }
}

double sampleBpmFor (const SampleData& s, const ParamArray& p)
{
    double fs, fe;
    flagRegion (s, p, fs, fe);
    const double secs = std::max (1e-3, (fe - fs) / s.sampleRate);
    return std::max (1.0, p[kWarpBeats]) * 60.0 / secs;
}

bool classicRegion (const SampleData& s, const ParamArray& p, PlayRegion& r)
{
    double fs, fe;
    flagRegion (s, p, fs, fe);
    const bool snap = on (p[kSnap]);
    r = PlayRegion {};
    const double span = fe - fs;
    double rs = fs + std::clamp (p[kStart], 0.0, 1.0) * span;
    rs = std::min (rs, fe - 16.0);
    if (snap)
        rs = std::min ((double)s.snapToZero ((int)rs), fe - 16.0);
    // the region always plays to the end flag; Length is the loop's length from Start (a share of
    // the flagged region), so it never shortens the sample. The loop is filled in with Loop off
    // too, for the display.
    const double re = fe;
    r.start = std::max (0.0, rs);
    r.end = re;
    r.loop = on (p[kLoopOn]);
    const double ls = rs;
    double le = std::min (re, rs + std::max (16.0, p[kLength] * span));
    if (snap)
        le = std::min (re, (double)s.snapToZero ((int)le));
    if (le - ls < 16.0)
        le = std::min (re, ls + 16.0);
    r.loopStart = ls;
    r.loopEnd = le;
    return r.end > r.start;
}

EnvSettings envSettingsFor (const ParamArray& p, int env)
{
    const uint32_t b = envAdsrBase (env);
    EnvSettings s;
    s.attackMs = (float)p[b];
    s.decayMs = (float)p[b + 1];
    s.sustain = (float)p[b + 2];
    s.releaseMs = (float)p[b + 3];
    s.curveA = (float)p[envParam (env, kEnvCurveA)];
    s.curveD = (float)p[envParam (env, kEnvCurveD)];
    s.curveR = (float)p[envParam (env, kEnvCurveR)];
    s.points = std::clamp (idx (p[envParam (env, kEnvPointCount)]), 0, kMaxEnvPoints);
    for (int i = 0; i < kMaxEnvPoints; ++i)
    {
        s.ptMs[i] = (float)p[envPointParam (env, i, kPtTime)];
        s.ptLevel[i] = (float)p[envPointParam (env, i, kPtLevel)];
        s.ptCurve[i] = (float)p[envPointParam (env, i, kPtCurve)];
    }
    if (env == 0)
    {
        s.loopMode = idx (p[kAmpLoopMode]);
        s.loopMs = (float)p[kAmpLoopTime];
    }
    return s;
}

SliceSettings sliceSettingsFor (const SampleData& s, const ParamArray& p)
{
    SliceSettings k;
    k.sliceBy = idx (p[kSliceBy]);
    k.sensitivity = p[kSensitivity];
    k.division = idx (p[kDivision]);
    k.regions = idx (p[kRegions]);
    flagRegion (s, p, k.regionStart, k.regionEnd);
    k.warpBeats = p[kWarpBeats];
    return k;
}

//==============================================================================
void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    beats.prepare (sr);
    grain.prepare (sr);
    pv.prepare (sr);
}

void Voice::start (const Start& s, const SampleData& sample, const ParamArray& p)
{
    st = s;
    active = true;
    released = killing = sustained = srcDone = gateFading = false;
    killGain = gateGain = 1.0f;
    elapsed = 0;
    beatAcc = 0.0;
    lastSyncSlot = -1;
    firstBlock = true;
    glideOffset = s.glideFrom;
    const double glideSamples = p[kGlideTime] * 0.001 * sr;
    glideStep = glideSamples > 1.0 ? std::fabs (glideOffset) / glideSamples : 1e9;
    lastRate = 1.0;

    if (!s.warp || s.warpMode == kWarpRePitch)
    {
        source = Source::Classic;
        pos = s.region.start;
    }
    else if (s.warpMode == kWarpBeatsMode)
    {
        source = Source::Beats;
        static const std::vector<int> none;
        beats.start (sample, s.region, s.beatBounds ? *s.beatBounds : none, idx (p[kBeatsLoop]),
                     (float)(p[kBeatsEnvelope] / 100.0));
    }
    else if (s.warpMode == kWarpTones || s.warpMode == kWarpTexture)
    {
        source = Source::Grain;
        const bool tones = s.warpMode == kWarpTones;
        const float ms = tones ? (float)(4.0 + p[kTonesGrain] * 1.2) : (float)(10.0 + p[kTextureGrain] * 4.0);
        grain.start (s.region, tones, ms, tones ? 0.0f : (float)(p[kTextureFlux] / 100.0), s.seed);
    }
    else
    {
        source = Source::Pv;
        pv.start (sample, s.region, s.warpMode == kWarpComplexPro, (float)(p[kFormants] / 100.0),
                  idx (p[kCproEnvelope]));
    }

    ampEnv.reset ();
    filtEnv.reset ();
    pitchEnv.reset ();
    ampEnv.noteOn ();
    filtEnv.noteOn ();
    pitchEnv.noteOn ();
    lfo.start (s.lfoPhase, s.seed * 2654435761u);
    filter.reset ();
}

void Voice::release (const ParamArray& p)
{
    if (released)
        return;
    released = true;
    sustained = false;
    filtEnv.noteOff (0);
    pitchEnv.noteOff (0);
    if (st.mode == kModeClassic)
        ampEnv.noteOff (idx (p[kAmpLoopMode]));
    else if (st.gate)
    {
        gateFading = true;
        const double ms = std::max (1.5, p[kFadeOut]);
        gateStep = (float)(1.0 / (ms * 0.001 * sr));
    }
}

void Voice::kill ()
{
    if (killing)
        return;
    killing = true;
    released = true;
    killStep = (float)(1.0 / (0.005 * sr));
}

void Voice::glideTo (int newNote, double newPitchBase, double glideMs)
{
    const double current = st.pitchBase + glideOffset;
    st.note = newNote;
    st.pitchBase = newPitchBase;
    glideOffset = current - newPitchBase;
    const double samples = glideMs * 0.001 * sr;
    glideStep = samples > 1.0 ? std::fabs (glideOffset) / samples : 1e9;
}

void Voice::updateLoop (const PlayRegion& r)
{
    if (source != Source::Classic || st.mode != kModeClassic)
        return;
    st.region.loop = r.loop;
    st.region.loopStart = std::max (st.region.start, r.loopStart);
    st.region.loopEnd = std::max (st.region.loopStart + 16.0, std::min (st.region.end, r.loopEnd));
}

double Voice::displayPos () const
{
    switch (source)
    {
        case Source::Beats: return beats.displayPos ();
        case Source::Grain: return grain.displayPos ();
        case Source::Pv: return pv.displayPos ();
        default: return pos;
    }
}

double Voice::remainingOut () const
{
    const auto& r = st.region;
    if (r.loop)
        return 1e12;
    switch (source)
    {
        case Source::Classic: return (r.end - pos) / std::max (1e-9, lastRate);
        case Source::Beats: return (r.end - beats.virtualPos ()) / std::max (1e-9, lastSrcPerOut);
        case Source::Grain: return (r.end - grain.virtualPos ()) / std::max (1e-9, lastSrcPerOut);
        case Source::Pv: return (r.end - pv.virtualPos ()) / std::max (1e-9, lastSrcPerOut);
    }
    return 1e12;
}

float Voice::sourceRender (float* L, float* R, int n, const BlockCtx& c, double pitchRatio)
{
    const SampleData& s = *c.sample;
    lastSrcPerOut = c.srcPerOut;
    if (source != Source::Classic)
    {
        WarpRates w {c.srcPerOut, pitchRatio, c.srcRate};
        switch (source)
        {
            case Source::Beats:
                beats.render (s, L, R, n, w);
                srcDone = beats.finished;
                break;
            case Source::Grain:
                grain.render (s, L, R, n, w);
                srcDone = grain.finished;
                break;
            default:
                pv.render (s, L, R, n, w);
                srcDone = pv.finished;
                break;
        }
        return 1.0f;
    }

    const double rate = st.warp ? pitchRatio * c.srcPerOut : pitchRatio * c.srcRate;
    lastRate = rate;
    const float cutoff = (float)std::min (1.0, 1.0 / rate);
    const float* a = s.data (0);
    const float* b = s.numChannels > 1 ? s.data (1) : nullptr;
    const auto& r = st.region;
    const double loopLen = r.loopEnd - r.loopStart;
    double fade = 0.0;
    if (r.loop && !st.warp && st.mode == kModeClassic)
        fade = std::min ((*c.p)[kLoopFade] * loopLen, 0.5 * loopLen);
    // the last `fade` samples of the loop blend into its first `fade` samples, so the wrap
    // lands `fade` samples into the loop and the audio is continuous
    const double wrapLen = loopLen - fade;
    for (int i = 0; i < n; ++i)
    {
        if (srcDone)
        {
            L[i] = R[i] = 0.0f;
            continue;
        }
        if (r.loop && loopLen >= 1.0)
        {
            int guard = 0;
            while (pos >= r.loopEnd && ++guard < 64)
                pos -= wrapLen;
        }
        else if (pos >= r.end)
        {
            srcDone = true;
            L[i] = R[i] = 0.0f;
            continue;
        }
        float l, rr;
        readSinc (a, b, s.length, pos, cutoff, l, rr);
        if (fade > 1.0 && pos > r.loopEnd - fade)
        {
            const double x = (pos - (r.loopEnd - fade)) / fade;
            float l2, r2;
            readSinc (a, b, s.length, pos - wrapLen, cutoff, l2, r2);
            float ga, gb;
            if (c.constantPowerFade)
            {
                ga = (float)std::cos (x * M_PI * 0.5);
                gb = (float)std::sin (x * M_PI * 0.5);
            }
            else
            {
                ga = (float)(1.0 - x);
                gb = (float)x;
            }
            l = l * ga + l2 * gb;
            rr = rr * ga + r2 * gb;
        }
        // the loop's head fades in on the first pass as well, the way it enters on every wrap, so
        // the note does not start abruptly (after a wrap the position is always past the head)
        if (fade > 1.0 && pos < r.loopStart + fade)
        {
            const double x = std::max (0.0, (pos - r.loopStart) / fade);
            const float g = c.constantPowerFade ? (float)std::sin (x * M_PI * 0.5) : (float)x;
            l *= g;
            rr *= g;
        }
        L[i] = l;
        R[i] = rr;
        pos += rate;
    }
    return 1.0f;
}

void Voice::render (float* outL, float* outR, int n, const BlockCtx& c)
{
    const ParamArray& p = *c.p;
    const bool classic = st.mode == kModeClassic;
    const bool mono = c.sample->numChannels < 2;
    const bool filterOn = on (p[kFilterOn]);
    const EnvSettings ampS = envSettingsFor (p, 0);
    const EnvSettings filtS = envSettingsFor (p, 1);
    const EnvSettings pitchS = envSettingsFor (p, 2);
    const float fsr = (float)sr;
    const double declickLen = 0.0015 * sr;
    const float fadeInLen = (float)(p[kFadeIn] * 0.001 * sr);
    const float fadeOutLen = (float)(p[kFadeOut] * 0.001 * sr);
    const int loopMode = idx (p[kAmpLoopMode]);
    const double loopRateBeats = syncDivisionBeats (idx (p[kAmpLoopRate]));

    int done = 0;
    while (done < n && active)
    {
        const int m = std::min (16, n - done);

        // amp envelope loop retriggering (Beat / Sync)
        if (classic && (loopMode == kAmpLoopBeat || loopMode == kAmpLoopSync))
        {
            const bool synced = loopMode == kAmpLoopSync && c.host.playing && c.host.ppqValid;
            if (synced)
            {
                const double ppq = c.host.ppq + done * c.ppqPerSample;
                const long long slot = (long long)std::floor (ppq / loopRateBeats);
                if (lastSyncSlot >= 0 && slot != lastSyncSlot)
                    ampEnv.retrigger ();
                lastSyncSlot = slot;
            }
            else
            {
                beatAcc += m * c.host.bpm / 60.0 / sr;
                if (beatAcc >= loopRateBeats)
                {
                    beatAcc -= loopRateBeats;
                    ampEnv.retrigger ();
                }
            }
        }

        // LFO
        float lfoV = 0.0f;
        if (on (p[kLfoOn]))
        {
            const double keyF = std::exp2 (p[kLfoKey] * (st.note - rootOf (p)) / 12.0);
            double freq;
            if (idx (p[kLfoSync]) == 1)
            {
                const double beatsLen = syncDivisionBeats (idx (p[kLfoSyncRate]));
                freq = c.host.bpm / 60.0 / beatsLen * keyF;
                if (!on (p[kLfoRetrig]) && c.host.playing && c.host.ppqValid && p[kLfoKey] == 0.0)
                    lfo.setPhase ((c.host.ppq + done * c.ppqPerSample) / beatsLen);
            }
            else
                freq = p[kLfoRate] * keyF;
            lfoV = lfo.advance (idx (p[kLfoWave]), freq, m, fsr, (float)p[kLfoAttack]);
        }

        // glide
        if (glideOffset != 0.0)
        {
            const double step = glideStep * m;
            if (std::fabs (glideOffset) <= step)
                glideOffset = 0.0;
            else
                glideOffset -= glideOffset > 0 ? step : -step;
        }

        const double semis = st.pitchBase + p[kTranspose] + p[kDetune] / 100.0 + c.bendSemis +
                             pitchEnv.value () * p[kPitchEnvAmt] + lfoV * p[kLfoPitch] * 12.0 + glideOffset +
                             st.spreadSemis;
        const double ratio = std::exp2 (semis / 12.0);
        const double rem0 = remainingOut ();
        sourceRender (tmpL, tmpR, m, c, ratio);

        if (filterOn)
        {
            FilterSettings fset;
            fset.type = idx (p[kFilterType]);
            fset.circuit = idx (p[kFilterCircuit]);
            fset.slope24 = idx (p[kFilterSlope]) == 1;
            fset.res = (float)p[kFilterRes];
            fset.driveDb = (float)p[kFilterDrive];
            fset.morph = (float)p[kFilterMorph];
            const double mod = st.velocity * p[kFilterVel] * 48.0 + p[kFilterKey] * (st.note - rootOf (p)) +
                               filtEnv.value () * p[kFilterEnvAmt] + lfoV * p[kLfoFilter] * 48.0;
            fset.cutoff = (float)(p[kFilterFreq] * std::exp2 (mod / 12.0));
            filter.setup (fset, fsr);
        }

        const float velGain = (float)(1.0 - p[kVelVol] * (1.0 - st.velocity));
        const float lfoVol = (float)(1.0 + p[kLfoVol] * (lfoV * 0.5 - 0.5));
        const float base = velGain * dbToGain (p[kGain]) * lfoVol;
        const float pan = std::clamp ((float)(p[kPan] + st.panOffset + lfoV * p[kLfoPan]) + st.sidePan, -1.0f, 1.0f);
        const float tl = base * (pan <= 0.0f ? 1.0f : 1.0f - pan);
        const float tr = base * (pan >= 0.0f ? 1.0f : 1.0f + pan);
        if (firstBlock)
        {
            prevGainL = tl;
            prevGainR = tr;
            firstBlock = false;
        }

        for (int i = 0; i < m; ++i)
        {
            float a;
            if (classic)
                a = ampEnv.process (ampS, fsr);
            else
            {
                a = 1.0f;
                if (fadeInLen > 0.5f)
                    a *= std::min (1.0f, (float)elapsed / fadeInLen);
                if (fadeOutLen > 0.5f)
                    a *= (float)std::clamp ((rem0 - i) / fadeOutLen, 0.0, 1.0);
            }
            filtEnv.process (filtS, fsr);
            pitchEnv.process (pitchS, fsr);
            if (rem0 < 1e11)
                a *= (float)std::clamp ((rem0 - i) / declickLen, 0.0, 1.0);
            if (killing)
            {
                killGain = std::max (0.0f, killGain - killStep);
                a *= killGain;
            }
            if (gateFading)
            {
                gateGain = std::max (0.0f, gateGain - gateStep);
                a *= gateGain;
            }
            const float t = (float)(i + 1) / (float)m;
            const float gl = prevGainL + (tl - prevGainL) * t;
            const float gr = prevGainR + (tr - prevGainR) * t;
            float l = tmpL[i] * a, r = tmpR[i] * a;
            if (filterOn)
            {
                l = filter.process (l, 0);
                r = mono ? l : filter.process (r, 1);
            }
            outL[done + i] += l * gl;
            outR[done + i] += r * gr;
            ++elapsed;
        }
        prevGainL = tl;
        prevGainR = tr;

        if ((classic && ampEnv.idle ()) || srcDone || (killing && killGain <= 0.0f) ||
            (gateFading && gateGain <= 0.0f))
            active = false;
        done += m;
    }
}

//==============================================================================
Engine::Engine () : p (defaultParams ())
{
    voices.resize (kMaxVoices);
    beatBounds.reserve (4096);
    monoStack.reserve (128);
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& v : voices)
        v.prepare (sr);
    rack.prepare (sr, 512);
    tail.prepare (sr, 512);
    for (uint32_t f = 0; f < pk::kTailFields; ++f)
        tail.setParam (f, p[kTailBase + f]);
    reset ();
}

void Engine::reset ()
{
    rack.reset ();
    tail.reset ();
    for (auto& v : voices)
        v.hardStop ();
    monoStack.clear ();
    sustain = false;
    lastNote = -1;
    volGain = p[kVolume] <= -69.99 ? 0.0f : dbToGain (p[kVolume]);
}

void Engine::setSample (SamplePtr s)
{
    for (auto& v : voices)
        v.hardStop ();
    monoStack.clear ();
    smp = std::move (s);
    slicesDirty = true;
}

void Engine::setSliceEdits (SliceEditsPtr e)
{
    edits = std::move (e);
    slicesDirty = true;
}

void Engine::setParam (uint32_t id, double plain)
{
    if (id >= kNumParams)
        return;
    p[id] = plain;
    if (isRackParam (id))
        rack.setParam (id, plain);
    else if (id >= kTailBase && id < kTailBase + pk::kTailFields)
        tail.setParam (id - kTailBase, plain);
}

void Engine::setSustain (bool onOff)
{
    sustain = onOff;
    if (!onOff)
        for (auto& v : voices)
            if (v.isActive () && v.sustained)
                v.release (p);
}

void Engine::updateSlices ()
{
    if (!smp)
    {
        slices.count = 0;
        return;
    }
    const SliceSettings k = sliceSettingsFor (*smp, p);
    if (!slicesDirty && k == sliceKey)
        return;
    sliceKey = k;
    slicesDirty = false;
    computeSlices (*smp, edits.get (), k, slices);
}

bool Engine::isMono () const
{
    const int mode = idx (p[kMode]);
    if (mode == kModeOneShot)
        return true;
    if (mode == kModeSlicing)
        return idx (p[kSlicePlayback]) != kSlicePoly;
    return idx (p[kGlideMode]) == kGlideMono;
}

int Engine::polyLimit () const { return isMono () ? 1 : voicesFromIndex (idx (p[kVoices])); }

bool Engine::regionFor (int note, PlayRegion& r) const
{
    if (!smp)
        return false;
    const SampleData& s = *smp;
    double fs, fe;
    flagRegion (s, p, fs, fe);
    const int mode = idx (p[kMode]);
    r = PlayRegion {};
    if (mode == kModeClassic)
        return classicRegion (s, p, r);
    if (mode == kModeOneShot)
    {
        r.start = fs;
        r.end = fe;
        return true;
    }
    const int i = note - kSliceBaseNote;
    if (i < 0 || i >= slices.count)
        return false;
    r.start = slices.pos[i];
    const bool thru = idx (p[kSlicePlayback]) == kSliceThru;
    r.end = thru || i + 1 >= slices.count ? fe : slices.pos[i + 1];
    return r.end > r.start + 1;
}

void Engine::computeBeatBounds (const PlayRegion& r)
{
    beatBounds.clear ();
    const size_t cap = beatBounds.capacity ();
    const int preserve = idx (p[kBeatsPreserve]);
    if (preserve == 0)
    {
        for (const auto& o : smp->onsets)
            if (o.strength >= 0.1f && o.pos > r.start && o.pos < r.end && beatBounds.size () < cap)
                beatBounds.push_back (o.pos);
        return;
    }
    double fs, fe;
    flagRegion (*smp, p, fs, fe);
    const double step = (fe - fs) / std::max (1.0, p[kWarpBeats]) * preserveBeats (preserve);
    if (step < 16.0)
        return;
    for (double x = fs + step; x < r.end && beatBounds.size () < cap; x += step)
        if (x > r.start)
            beatBounds.push_back ((int)std::lround (x));
}

void Engine::killGroup (int g)
{
    for (auto& v : voices)
        if (v.isActive () && v.group () == g)
            v.kill ();
}

void Engine::noteOn (int note, float velocity)
{
    if (velocity <= 0.0f)
    {
        noteOff (note);
        return;
    }
    rack.noteOn (note); // Para's envelope follows the sampler's notes
    if (!smp)
        return;
    updateSlices ();
    const int mode = idx (p[kMode]);
    const bool legatoGlide = mode == kModeClassic && idx (p[kGlideMode]) == kGlideMono;
    if (legatoGlide)
    {
        monoStack.erase (std::remove (monoStack.begin (), monoStack.end (), note), monoStack.end ());
        if (monoStack.size () < monoStack.capacity ())
            monoStack.push_back (note);
        bool glided = false;
        for (auto& v : voices)
            if (v.isActive () && !v.isReleased () && !v.isKilling ())
            {
                v.glideTo (note, note - rootOf (p), p[kGlideTime]);
                glided = true;
            }
        if (glided)
        {
            lastNote = note;
            lastPitch = note - rootOf (p);
            return;
        }
    }
    startNote (note, velocity, false);
}

void Engine::startNote (int note, float velocity, bool)
{
    PlayRegion r;
    if (!regionFor (note, r))
        return;
    const int mode = idx (p[kMode]);

    if (isMono ())
    {
        for (auto& v : voices)
            if (v.isActive () && !v.isKilling ())
                v.kill ();
    }
    else
    {
        if (on (p[kRetrig]))
            for (auto& v : voices)
                if (v.isActive () && !v.isKilling () && v.note () == note)
                    v.kill ();
        const int limit = polyLimit ();
        for (;;)
        {
            int groups = 0, oldestGroup = -1;
            uint64_t oldestAge = UINT64_MAX;
            int seen[kMaxVoices];
            for (auto& v : voices)
            {
                if (!v.isActive () || v.isKilling ())
                    continue;
                bool dup = false;
                for (int i = 0; i < groups; ++i)
                    dup |= seen[i] == v.group ();
                if (!dup)
                    seen[groups++] = v.group ();
                // prefer stealing released voices, then the oldest
                const uint64_t a = v.age () + (v.isReleased () ? 0ull : (1ull << 62));
                if (a < oldestAge)
                {
                    oldestAge = a;
                    oldestGroup = v.group ();
                }
            }
            if (groups < limit || oldestGroup < 0)
                break;
            killGroup (oldestGroup);
        }
    }

    const bool warp = on (p[kWarp]);
    const int warpMode = idx (p[kWarpMode]);
    if (warp && warpMode == kWarpBeatsMode)
        computeBeatBounds (r);

    const double base = mode == kModeSlicing ? 0.0 : (double)(note - rootOf (p));
    double glideFrom = 0.0;
    if (idx (p[kGlideMode]) != kGlideOff && lastNote >= 0)
        glideFrom = lastPitch - base;
    lastNote = note;
    lastPitch = base;

    const double spread = p[kSpread];
    const int count = spread > 0.001 ? 2 : 1;
    const int group = ++groupCounter;
    const float randPan = (float)(p[kPanRand] * randomBipolar (seed));
    const double lfoPhase = on (p[kLfoRetrig]) ? p[kLfoOffset] / 360.0 : globalLfoPhase;

    for (int k = 0; k < count; ++k)
    {
        Voice* slot = nullptr;
        for (auto& v : voices)
            if (!v.isActive ())
            {
                slot = &v;
                break;
            }
        if (!slot) // pool exhausted: take the oldest voice outright
        {
            for (auto& v : voices)
                if (!slot || v.age () < slot->age ())
                    slot = &v;
            slot->hardStop ();
        }
        Voice::Start s;
        s.note = note;
        s.velocity = std::clamp (velocity, 0.0f, 1.0f);
        s.mode = mode;
        s.region = r;
        s.warp = warp;
        s.warpMode = warpMode;
        s.gate = mode != kModeClassic && idx (p[kTriggerGate]) == 1;
        s.pitchBase = base;
        s.glideFrom = glideFrom;
        s.spreadSemis = count == 2 ? (float)((k == 0 ? -1.0 : 1.0) * spread * 0.25) : 0.0f;
        s.sidePan = count == 2 ? (k == 0 ? -1.0f : 1.0f) : 0.0f;
        s.panOffset = randPan;
        s.group = group;
        s.age = ++ageCounter;
        s.lfoPhase = lfoPhase;
        s.seed = seed = seed * 1664525u + 1013904223u;
        s.beatBounds = &beatBounds;
        slot->start (s, *smp, p);
    }
}

void Engine::noteOff (int note)
{
    const int mode = idx (p[kMode]);
    if (mode == kModeClassic && idx (p[kGlideMode]) == kGlideMono)
    {
        monoStack.erase (std::remove (monoStack.begin (), monoStack.end (), note), monoStack.end ());
        bool playingThis = false;
        for (auto& v : voices)
            if (v.isActive () && !v.isReleased () && v.note () == note)
                playingThis = true;
        if (playingThis && !monoStack.empty ())
        {
            const int top = monoStack.back ();
            for (auto& v : voices)
                if (v.isActive () && !v.isReleased ())
                    v.glideTo (top, top - rootOf (p), p[kGlideTime]);
            lastNote = top;
            lastPitch = top - rootOf (p);
            return;
        }
    }
    for (auto& v : voices)
        if (v.isActive () && !v.isReleased () && v.note () == note)
        {
            if (sustain)
                v.sustained = true;
            else
                v.release (p);
        }
}

void Engine::allNotesOff ()
{
    monoStack.clear ();
    for (auto& v : voices)
        if (v.isActive ())
            v.release (p);
}

void Engine::makeCtx (const HostInfo& host, BlockCtx& c) const
{
    c.sample = smp.get ();
    c.p = &p;
    c.sr = sr;
    c.host = host;
    if (c.host.bpm <= 0.0)
        c.host.bpm = 120.0;
    c.srcRate = smp->sampleRate / sr;
    c.srcPerOut = c.srcRate * c.host.bpm / sampleBpmFor (*smp, p);
    c.bendSemis = (float)(bend * p[kPbRange]);
    c.constantPowerFade = on (p[kLoopFadePower]);
    c.ppqPerSample = c.host.bpm / 60.0 / sr;
    c.globalLfoPhase = globalLfoPhase;
}

void Engine::render (float* L, float* R, int n, const HostInfo& host)
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    globalLfoPhase += p[kLfoRate] * n / sr;
    globalLfoPhase -= std::floor (globalLfoPhase);
    if (!smp)
    {
        renderEffects (L, R, n);
        return;
    }
    updateSlices ();
    BlockCtx c;
    makeCtx (host, c);

    PlayRegion loopRegion;
    const bool liveLoop = idx (p[kMode]) == kModeClassic && regionFor (rootOf (p), loopRegion);
    for (auto& v : voices)
    {
        if (!v.isActive ())
            continue;
        if (liveLoop)
            v.updateLoop (loopRegion);
        v.render (L, R, n, c);
    }

    const float target = p[kVolume] <= -69.99 ? 0.0f : dbToGain (p[kVolume]);
    const float coef = 1.0f - std::exp (-1.0f / (0.01f * (float)sr));
    for (int i = 0; i < n; ++i)
    {
        volGain += (target - volGain) * coef;
        L[i] *= volGain;
        R[i] *= volGain;
    }
    renderEffects (L, R, n);
}

void Engine::renderEffects (float* L, float* R, int n)
{
    rack.process (L, R, n);
    tail.process (L, R, n);
}

int Engine::activeVoices () const
{
    int n = 0;
    for (auto& v : voices)
        n += v.isActive () ? 1 : 0;
    return n;
}

int Engine::playPositions (float* out, int max) const
{
    if (!smp || smp->length <= 0)
        return 0;
    int k = 0;
    for (auto& v : voices)
        if (v.isActive () && !v.isKilling () && k < max)
            out[k++] = (float)(v.displayPos () / smp->length);
    return k;
}

} // namespace smempler

#include "Engine.h"

#include "Interp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace smemplr {

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

namespace {
// a loop from Start (a share of the flagged region) Length long (a share of it too), to the end flag
bool loopRegion (const SampleData& s, const ParamArray& p, double start, double length, PlayRegion& r)
{
    double fs, fe;
    flagRegion (s, p, fs, fe);
    const bool snap = on (p[kSnap]);
    r = PlayRegion {};
    const double span = fe - fs;
    double rs = fs + std::clamp (start, 0.0, 1.0) * span;
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
    double le = std::min (re, rs + std::max (16.0, length * span));
    if (snap)
        le = std::min (re, (double)s.snapToZero ((int)le));
    if (le - ls < 16.0)
        le = std::min (re, ls + 16.0);
    r.loopStart = ls;
    r.loopEnd = le;
    return r.end > r.start;
}
} // namespace

bool classicRegion (const SampleData& s, const ParamArray& p, PlayRegion& r) { return loopRegion (s, p, p[kStart], p[kLength], r); }

bool headRegion (const SampleData& s, const ParamArray& p, int head, PlayRegion& r)
{
    return loopRegion (s, p, p[headParam (head, kHeadStart)], p[headParam (head, kHeadLength)], r);
}

int playheadsFor (const ParamArray& p)
{
    return idx (p[kMode]) == kModeClassic ? std::clamp (idx (p[kPlayheads]) + 1, 1, kMaxPlayheads) : 1;
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
    for (auto& x : extra)
    {
        x.beats.prepare (sr);
        x.grain.prepare (sr);
    }
}

void Voice::start (const Start& s, const SampleData& sample, const ParamArray& p)
{
    st = s;
    active = true;
    released = killing = sustained = srcDone = gateFading = false;
    head.start (s.region);
    tailing = false;
    tailLeft = 0;
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
        source = Source::Classic;
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
    // the extra playheads, read the way the main one is (the Complex modes have none)
    numHeads = source == Source::Pv ? 1 : std::clamp (s.heads, 1, kMaxPlayheads);
    monoOut = sample.numChannels < 2 && numHeads == 1;
    headPanSet = false;
    for (int k = 0; k + 1 < numHeads; ++k)
    {
        ExtraHead& x = extra[(size_t)k];
        const PlayRegion& r = s.headRegions[k];
        x.classic.start (r);
        if (source == Source::Beats)
        {
            static const std::vector<int> none;
            x.beats.start (sample, r, s.beatBounds ? *s.beatBounds : none, idx (p[kBeatsLoop]), (float)(p[kBeatsEnvelope] / 100.0));
        }
        else if (source == Source::Grain)
        {
            const bool tones = s.warpMode == kWarpTones;
            const float ms = tones ? (float)(4.0 + p[kTonesGrain] * 1.2) : (float)(10.0 + p[kTextureGrain] * 4.0);
            // (a seed of its own: Texture's scatter differs from the main playhead's)
            x.grain.start (r, tones, ms, tones ? 0.0f : (float)(p[kTextureFlux] / 100.0), s.seed ^ (0x9E3779B9u * (uint32_t)(k + 1)));
        }
    }

    ampEnv.reset ();
    filtEnv.reset ();
    pitchEnv.reset ();
    ampEnv.noteOn ();
    filtEnv.noteOn ();
    pitchEnv.noteOn ();
    lfo.start (s.lfoPhase, s.seed * 2654435761u);
    filter.reset ();
    transHp.reset ();
    transHpRunning = false;
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
    killStep = (float)(1.0 / (0.015 * sr));
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

void Voice::ClassicHead::start (const PlayRegion& r)
{
    region = r;
    liveStart = nextStart = r.loopStart;
    liveEnd = nextEnd = r.loopEnd;
    pos = r.start;
    done = wrapped = false;
    wraps = 0;
    jumpLeft = 0;
}

void Voice::ClassicHead::follow (const PlayRegion& r)
{
    region.loop = r.loop;
    // the loop follows Start both ways: moved before where the note started, the region starts there
    // too (held at the note's start, a Start moved back would leave a loop of a few samples: a beep)
    region.start = std::min (region.start, r.loopStart);
    liveStart = std::max (region.start, r.loopStart);
    liveEnd = std::max (liveStart + 16.0, std::min (region.end, r.loopEnd));
    // not looping, there is no pass to finish: the loop (for when Loop comes on) is where it is now
    if (!r.loop)
    {
        region.loopStart = nextStart = liveStart;
        region.loopEnd = nextEnd = liveEnd;
    }
}

void Voice::updateLoop (const PlayRegion& r, const PlayRegion* extraRegions)
{
    if (source != Source::Classic || st.mode != kModeClassic)
        return;
    head.follow (r);
    if (extraRegions)
        for (int k = 0; k + 1 < numHeads; ++k)
            extra[(size_t)k].classic.follow (extraRegions[k]);
}

int Voice::displayPositions (double* out, int max) const
{
    int n = 0;
    if (n < max)
        out[n++] = displayPos ();
    for (int k = 0; k + 1 < numHeads && n < max; ++k)
    {
        const ExtraHead& x = extra[(size_t)k];
        out[n++] = source == Source::Beats ? x.beats.displayPos () : source == Source::Grain ? x.grain.displayPos () : x.classic.pos;
    }
    return n;
}

bool Voice::headDone (int k) const
{
    if (k == 0)
        return source == Source::Classic ? head.done : source == Source::Beats ? beats.finished : source == Source::Grain ? grain.finished : pv.finished;
    const ExtraHead& x = extra[(size_t)k - 1];
    return source == Source::Classic ? x.classic.done : source == Source::Beats ? x.beats.finished : x.grain.finished;
}

double Voice::headRemaining (int k) const
{
    if (k == 0)
        return mainRemaining ();
    const ExtraHead& x = extra[(size_t)k - 1];
    const auto& r = x.classic.region;
    if (r.loop)
        return 1e12;
    switch (source)
    {
        case Source::Classic: return (r.end - x.classic.pos) / std::max (1e-9, lastRate);
        case Source::Beats: return (r.end - x.beats.virtualPos ()) / std::max (1e-9, lastSrcPerOut);
        case Source::Grain: return (r.end - x.grain.virtualPos ()) / std::max (1e-9, lastSrcPerOut);
        default: return 1e12;
    }
}

double Voice::displayPos () const
{
    switch (source)
    {
        case Source::Beats: return beats.displayPos ();
        case Source::Grain: return grain.displayPos ();
        case Source::Pv: return pv.displayPos ();
        default: return head.pos;
    }
}

double Voice::remainingOut () const
{
    const auto& r = head.region;
    if (r.loop)
        return 1e12;
    if (numHeads > 1)
    {
        // (the voice ends with its last playhead: each fades out on its own, mixHeads)
        double most = 0.0;
        for (int k = 1; k < numHeads; ++k)
            most = std::max (most, headRemaining (k));
        return std::max (most, mainRemaining ());
    }
    return mainRemaining ();
}

double Voice::mainRemaining () const
{
    const auto& r = head.region;
    if (r.loop)
        return 1e12;
    switch (source)
    {
        case Source::Classic: return (r.end - head.pos) / std::max (1e-9, lastRate);
        case Source::Beats: return (r.end - beats.virtualPos ()) / std::max (1e-9, lastSrcPerOut);
        case Source::Grain: return (r.end - grain.virtualPos ()) / std::max (1e-9, lastSrcPerOut);
        case Source::Pv: return (r.end - pv.virtualPos ()) / std::max (1e-9, lastSrcPerOut);
    }
    return 1e12;
}

double Voice::loopPassLen (const ParamArray& p) const
{
    const auto& r = head.region;
    const double loopLen = r.loopEnd - r.loopStart;
    if (!r.loop || loopLen < 1.0)
        return 0.0;
    const double fade = !st.warp && st.mode == kModeClassic ? std::min (p[kLoopFade] * loopLen, 0.5 * loopLen) : 0.0;
    return std::max (1.0, loopLen - fade);
}

float Voice::sourceRender (float* L, float* R, int n, const BlockCtx& c, double pitchRatio)
{
    const SampleData& s = *c.sample;
    // more playheads, or the main one reading one channel: mixHeads after the main one is read (with
    // one playhead reading both channels none of it runs: the voice plays as it always has)
    const bool heads = numHeads > 1 || idx ((*c.p)[headChannelParam (0)]) != kHeadStereo;
    const double rem0 = heads && numHeads > 1 ? headRemaining (0) : 1e12; // (before this block moves it)
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
        if (heads)
            mixHeads (L, R, n, c, pitchRatio, rem0);
        return 1.0f;
    }

    const double rate = st.warp ? pitchRatio * c.srcPerOut : pitchRatio * c.srcRate;
    lastRate = rate;
    renderClassic (head, L, R, n, c, rate);
    srcDone = head.done;
    if (heads)
        mixHeads (L, R, n, c, pitchRatio, rem0);
    return 1.0f;
}

void Voice::mixHeads (float* L, float* R, int n, const BlockCtx& c, double pitchRatio, double rem0)
{
    const ParamArray& p = *c.p;
    const bool stereoSrc = c.sample->numChannels > 1;
    // Spread: 0, every playhead in the centre; turned up, they move apart across the stereo field,
    // evenly from the left (the main playhead) to the right (the last), all the way at 100 %; turned
    // down, the other way round
    const double spread = std::clamp (p[kHeadSpread], -1.0, 1.0);
    std::array<float, kMaxPlayheads> target {};
    for (int k = 0; k < numHeads; ++k)
        target[(size_t)k] = numHeads > 1 ? (float)(spread * (-1.0 + 2.0 * k / (numHeads - 1))) : 0.0f;
    if (!headPanSet)
    {
        headPan = target;
        headPanSet = true;
    }
    const double declick = 0.0015 * sr;
    // a playhead: its channel (a stereo sample's left or right in both), its end (with more playheads
    // each fades out on its own as it reaches the end flag), its place (gliding over the block to where
    // Spread puts it now). The place: the two channels drawn together as the playhead moves out (all the
    // way at the edge) and panned at constant power, unchanged in the centre.
    auto shape = [&] (int k, float* a, float* b, double rem) {
        const int ch = idx (p[headChannelParam (k)]);
        if (stereoSrc && ch == kHeadLeft)
            std::copy (a, a + n, b);
        else if (stereoSrc && ch == kHeadRight)
            std::copy (b, b + n, a);
        if (numHeads > 1 && rem < 1e11)
            for (int i = 0; i < n; ++i)
            {
                const float g = (float)std::clamp ((rem - i) / declick, 0.0, 1.0);
                a[i] *= g;
                b[i] *= g;
            }
        const float from = headPan[(size_t)k], to = target[(size_t)k];
        headPan[(size_t)k] = to;
        if (from == 0.0f && to == 0.0f)
            return;
        for (int i = 0; i < n; ++i)
        {
            const float pan = from + (to - from) * (float)(i + 1) / (float)n;
            const float w = std::fabs (pan);
            const float mid = 0.5f * (a[i] + b[i]);
            const float l = a[i] + (mid - a[i]) * w, r = b[i] + (mid - b[i]) * w;
            const float angle = (pan + 1.0f) * (float)(M_PI * 0.25);
            const float gl = pan >= 1.0f ? 0.0f : std::cos (angle) * (float)M_SQRT2; // (exactly 0 at the edge)
            const float gr = pan <= -1.0f ? 0.0f : std::sin (angle) * (float)M_SQRT2;
            a[i] = l * gl;
            b[i] = r * gr;
        }
    };
    shape (0, L, R, rem0);
    bool done = headDone (0);
    const WarpRates w {c.srcPerOut, pitchRatio, c.srcRate};
    for (int k = 1; k < numHeads; ++k)
    {
        ExtraHead& x = extra[(size_t)k - 1];
        const double rem = headRemaining (k);
        switch (source)
        {
            case Source::Beats: x.beats.render (*c.sample, hL, hR, n, w); break;
            case Source::Grain: x.grain.render (*c.sample, hL, hR, n, w); break;
            default: renderClassic (x.classic, hL, hR, n, c, lastRate); break;
        }
        shape (k, hL, hR, rem);
        for (int i = 0; i < n; ++i)
        {
            L[i] += hL[i];
            R[i] += hR[i];
        }
        done = done && headDone (k);
    }
    srcDone = done;
}

void Voice::renderClassic (ClassicHead& h, float* L, float* R, int n, const BlockCtx& c, double rate) const
{
    // band-limited at any speed (the sample's levels when it is read fast)
    const SampleReader rd (*c.sample, rate);
    auto& r = h.region;
    double& pos = h.pos;
    const double fadeShare = (*c.p)[kLoopFade];
    const bool fades = !st.warp && st.mode == kModeClassic;
    // the last `fade` samples of the loop blend into its first `fade` samples, so the wrap
    // lands `fade` samples into the loop and the audio is continuous
    double loopLen = 0.0, fade = 0.0, wrapLen = 0.0;
    auto setup = [&] {
        loopLen = r.loopEnd - r.loopStart;
        fade = r.loop && fades ? std::min (fadeShare * loopLen, 0.5 * loopLen) : 0.0;
        wrapLen = loopLen - fade;
    };
    setup ();
    // The loop this pass ends into: where it is now, unless the crossfade into it has begun (moved
    // again during the crossfade, it waits for the next pass). Moved: the playhead finishes this pass,
    // the loop's last samples crossfading into the moved loop's head as they would into its own (over
    // as long a crossfade as the moved loop has room for), and goes on there; without a crossfade, it
    // jumps with a short one (5 ms) from where it was. Moved again and again within a pass, it goes to
    // wherever the loop is when the pass ends.
    if (!(fade > 1.0 && pos > r.loopEnd - fade && pos < r.loopEnd))
    {
        h.nextStart = h.liveStart;
        h.nextEnd = h.liveEnd;
    }
    bool moved = r.loop && (h.nextStart != r.loopStart || h.nextEnd != r.loopEnd);
    double xf = moved ? std::min (fade, 0.5 * (h.nextEnd - h.nextStart)) : 0.0;
    const int jumpSamples = std::max (1, (int)(0.005 * sr));
    auto adoptNext = [&] {
        r.loopStart = h.nextStart;
        r.loopEnd = h.nextEnd;
        setup ();
        moved = false;
        xf = 0.0;
    };
    h.wraps = 0;
    for (int i = 0; i < n; ++i)
    {
        if (h.done)
        {
            L[i] = R[i] = 0.0f;
            continue;
        }
        if (r.loop && loopLen >= 1.0 && pos >= r.loopEnd)
        {
            const double over = pos - r.loopEnd;
            if (over > rate + 1.0 && h.jumpLeft == 0)
            {
                // not a wrap but the playhead past the loop (Loop switched on behind it): jump into it
                // with a short crossfade from where it was
                h.jumpFrom = pos;
                h.jumpLen = h.jumpLeft = jumpSamples;
                if (moved)
                {
                    adoptNext ();
                    pos = r.loopStart + fade;
                }
                else
                    pos -= (std::floor (over / std::max (1.0, wrapLen)) + 1.0) * std::max (1.0, wrapLen);
            }
            else
            {
                ++h.wraps;
                h.wrapped = true;
                if (!moved)
                    pos -= (std::floor (over / std::max (1.0, wrapLen)) + 1.0) * std::max (1.0, wrapLen);
                else
                {
                    // the pass is over: on at the moved loop's start (as far in as the crossfade took it)
                    const double into = xf > 1.0 ? xf : 0.0;
                    if (into <= 0.0)
                    {
                        h.jumpFrom = pos;
                        h.jumpLen = h.jumpLeft = jumpSamples;
                    }
                    adoptNext ();
                    pos = r.loopStart + into + over;
                    if (pos >= r.loopEnd)
                        pos = r.loopStart + std::fmod (pos - r.loopStart, std::max (1.0, loopLen));
                }
            }
        }
        else if (pos >= r.end)
        {
            h.done = true;
            L[i] = R[i] = 0.0f;
            continue;
        }
        float l, rr;
        rd.read (pos, l, rr);
        if (moved ? xf > 1.0 && pos > r.loopEnd - xf : fade > 1.0 && pos > r.loopEnd - fade)
        {
            // the loop's end crossfading into its head (or into the head of the loop it moved to)
            const double zone = moved ? xf : fade;
            const double x = (pos - (r.loopEnd - zone)) / zone;
            float l2, r2;
            rd.read (moved ? h.nextStart + (pos - (r.loopEnd - zone)) : pos - wrapLen, l2, r2);
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
        if (fade > 1.0 && !h.wrapped && pos < r.loopStart + fade)
        {
            const double x = std::max (0.0, (pos - r.loopStart) / fade);
            const float g = c.constantPowerFade ? (float)std::sin (x * M_PI * 0.5) : (float)x;
            l *= g;
            rr *= g;
        }
        if (h.jumpLeft > 0)
        {
            // the old place fading out under the new one (equal gain: the two are unrelated)
            const float x = (float)h.jumpLeft / (float)h.jumpLen;
            float l2 = 0.0f, r2 = 0.0f;
            if (h.jumpFrom < r.end)
                rd.read (h.jumpFrom, l2, r2);
            l = l * (1.0f - x) + l2 * x;
            rr = rr * (1.0f - x) + r2 * x;
            h.jumpFrom += rate;
            --h.jumpLeft;
        }
        L[i] = l;
        R[i] = rr;
        pos += rate;
    }
}

void Voice::render (float* outL, float* outR, int n, const BlockCtx& c)
{
    const ParamArray& p = *c.p;
    const bool classic = st.mode == kModeClassic;
    const bool mono = monoOut; // (a mono sample with more playheads is played in stereo: they spread)
    const bool filterOn = on (p[kFilterOn]);
    const EnvSettings ampS = envSettingsFor (p, 0);
    EnvSettings filtS = envSettingsFor (p, 1);
    EnvSettings pitchS = envSettingsFor (p, 2);
    const float fsr = (float)sr;
    // the filter and pitch envelopes locked to the loop: restarted at every pass (Restart), and with Fit
    // their attack, points and decay stretched (or squeezed) to the length of a pass at the speed the
    // sample plays (what the release does is the note's, not the loop's: it stays)
    const int filtLock = idx (p[kFiltLoopLock]), pitchLock = idx (p[kPitchLoopLock]);
    // (Classic only: the loop is Classic's, read at lastRate; warped playback has its own timing)
    const double passLen = classic && !st.warp && (filtLock != kLoopLockOff || pitchLock != kLoopLockOff) ? loopPassLen (p) : 0.0;
    const EnvSettings filtS0 = filtS, pitchS0 = pitchS;
    auto fit = [] (EnvSettings& s, const EnvSettings& s0, double passMs) {
        double total = s0.attackMs + s0.decayMs;
        for (int k = 0; k < s0.points; ++k)
            total += s0.ptMs[k];
        const float scale = (float)(passMs / std::max (0.01, total));
        s.attackMs = s0.attackMs * scale;
        s.decayMs = s0.decayMs * scale;
        for (int k = 0; k < s0.points; ++k)
            s.ptMs[k] = s0.ptMs[k] * scale;
    };
    const double declickLen = 0.0015 * sr;
    const float fadeInLen = (float)(p[kFadeIn] * 0.001 * sr);
    const float fadeOutLen = (float)(p[kFadeOut] * 0.001 * sr);
    const int loopMode = idx (p[kAmpLoopMode]);
    const double loopRateBeats = syncDivisionBeats (idx (p[kAmpLoopRate]));
    const bool transHpOn = on (p[kTransHpOn]);
    if (!transHpOn)
        transHpRunning = false;

    if (tailing)
    {
        renderTail (outL, outR, n, mono);
        return;
    }
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
        if (passLen > 0.0)
        {
            // a pass of the loop just ended: the locked envelopes start again (within this sub-block of
            // 16 samples: at most 0.4 ms after the wrap)
            if (head.wraps > 0)
            {
                if (filtLock != kLoopLockOff)
                    filtEnv.retrigger ();
                if (pitchLock != kLoopLockOff)
                    pitchEnv.retrigger ();
            }
            // a pass at the speed the sample is read now (the pitch, the warp)
            const double passMs = 1000.0 * passLen / std::max (1e-6, lastRate) / sr;
            if (filtLock == kLoopLockFit)
                fit (filtS, filtS0, passMs);
            if (pitchLock == kLoopLockFit)
                fit (pitchS, pitchS0, passMs);
        }

        // the high-pass that follows the transposition: what the note is transposed by (not the key
        // played, nor glide or spread), so the sample's own low end moves with the pitch and stays
        // out of the way; it glides (4 ms) so a jump in the pitch does not click
        if (transHpOn)
        {
            const double target = p[kTranspose] + p[kDetune] / 100.0 + c.bendSemis +
                                  pitchEnv.value () * p[kPitchEnvAmt] + lfoV * p[kLfoPitch] * 12.0;
            if (!transHpRunning)
            {
                transHp.reset ();
                transHpSemis = target;
                transHpRunning = true;
            }
            else
                transHpSemis += (target - transHpSemis) * (1.0 - std::exp (-m / (0.004 * sr)));
            transHp.setup (idx (p[kTransHpSlope]), p[kTransHpFreq] * std::exp2 (transHpSemis / 12.0), sr);
            for (int i = 0; i < m; ++i)
            {
                tmpL[i] = transHp.process (tmpL[i], 0);
                tmpR[i] = mono ? tmpL[i] : transHp.process (tmpR[i], 1);
            }
        }

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
            // the fast fades (a steal, a gate) act after the filter: what it is still ringing out fades
            // with them (before it, a low-pass at a low cutoff kept sounding and was cut: a click)
            float post = 1.0f;
            if (killing)
            {
                killGain = std::max (0.0f, killGain - killStep);
                post *= killGain;
            }
            if (gateFading)
            {
                gateGain = std::max (0.0f, gateGain - gateStep);
                post *= gateGain;
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
            l *= post;
            r *= post;
            outL[done + i] += l * gl;
            outR[done + i] += r * gr;
            ++elapsed;
        }
        prevGainL = tl;
        prevGainR = tr;

        if ((killing && killGain <= 0.0f) || (gateFading && gateGain <= 0.0f))
            active = false;
        else if ((classic && ampEnv.idle ()) || srcDone)
        {
            // the note is over, but the filter may still be ringing out what it was given (a low
            // cutoff, resonance): it rings on, fed silence, fading over 30 ms, instead of being cut
            if (filterOn && !killing)
            {
                tailLen = tailLeft = std::max (1, (int)(0.03 * sr));
                tailing = true;
                released = true; // (stolen first when voices run out)
                tailGainL = tl;
                tailGainR = tr;
                done += m;
                renderTail (outL + done, outR + done, n - done, mono);
                return;
            }
            active = false;
        }
        done += m;
    }
}

void Voice::renderTail (float* outL, float* outR, int n, bool mono)
{
    for (int i = 0; i < n && tailLeft > 0; ++i)
    {
        float l = filter.process (0.0f, 0);
        float r = mono ? l : filter.process (0.0f, 1);
        const float x = (float)tailLeft / (float)tailLen; // 1 -> 0
        const float g = x * x * (3.0f - 2.0f * x);          // smooth
        if (killing)
        {
            killGain = std::max (0.0f, killGain - killStep);
            l *= killGain;
            r *= killGain;
        }
        outL[i] += l * g * tailGainL;
        outR[i] += r * g * tailGainR;
        --tailLeft;
        if (killing && killGain <= 0.0f)
            tailLeft = 0;
    }
    if (tailLeft <= 0)
    {
        tailing = false;
        active = false;
    }
}

//==============================================================================
Engine::Engine () : p (defaultParams ()), base (p)
{
    voices.resize (kMaxVoices);
    beatBounds.reserve (4096);
    monoStack.reserve (128);
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    mod.prepare (sr);
    for (auto& v : voices)
        v.prepare (sr);
    rack.prepare (sr, 512);
    tail.prepare (sr, 512);
    // the saturator after the rack (before 0.9) has no parameters for Gentlr's band Slope: Classic, the
    // shape its bands had (an old project's sound)
    tail.setParam (smacheratr::kTailExt3First + pk::kTailExt3Slope, smacheratr::kSlopeClassic);
    // the effects as the parameters have them (a new Smemplr: Smacheratr in the first slot)
    for (uint32_t id = 0; id < kNumParams; ++id)
        if (isRackParam (id))
            rack.setParam (id, p[id]);
        else if (isTailParam (id))
            tail.setParam (tailField (id), p[id]);
    reset ();
}

void Engine::reset ()
{
    rack.reset ();
    tail.reset ();
    mod.reset ();
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
    if (!isValidParam (id)) // (the hidden MIDI parameters are events: Processor.cpp)
        return;
    base[id] = plain;
    applyParam (id, plain);
    // a modulated parameter: set again (base and modulation) at the next step
    for (int i = 0; i < numMods; ++i)
        if (mods[(size_t)i].target == id)
            modState[(size_t)i].stale = true;
}

void Engine::applyParam (uint32_t id, double plain)
{
    p[id] = plain;
    if (isRackParam (id))
        rack.setParam (id, plain);
    else if (isTailParam (id))
    {
        // the old saturator after the rack runs only while it is on (an old project whose rack was
        // full); switched on, it starts from silence
        const bool was = tail.isOn ();
        tail.setParam (tailField (id), plain);
        if (!was && tail.isOn ())
            tail.reset ();
    }
}

void Engine::setSustain (bool onOff)
{
    sustain = onOff;
    if (!onOff)
    {
        for (auto& v : voices)
            if (v.isActive () && v.sustained)
                v.release (p);
        for (int n = 0; n < 128; ++n)
            if (pedalHeld.test ((size_t)n))
                rack.noteOff (n);
        pedalHeld.reset ();
    }
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
    mod.noteOn ();      // (the LFOs with Retrig on start again)
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
    // the extra playheads' regions (Classic); Beats cuts the sample from the earliest of them on
    const int heads = playheadsFor (p);
    PlayRegion headRegions[kMaxPlayheads - 1];
    PlayRegion cut = r;
    for (int k = 1; k < heads; ++k)
    {
        headRegion (*smp, p, k, headRegions[k - 1]);
        cut.start = std::min (cut.start, headRegions[k - 1].start);
    }
    if (warp && warpMode == kWarpBeatsMode)
        computeBeatBounds (cut);

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
        s.heads = heads;
        for (int h = 0; h + 1 < heads; ++h)
            s.headRegions[h] = headRegions[h];
        slot->start (s, *smp, p);
    }
}

void Engine::noteOff (int note)
{
    // Wubr: letting go plays the rest of its envelopes (with the pedal down: when it comes up, as the sound)
    if (sustain)
        pedalHeld.set ((size_t)std::clamp (note, 0, 127));
    else
        rack.noteOff (note);
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
    rack.allNotesOff ();
    pedalHeld.reset ();
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

void Engine::setModMappings (const ModMapping* list, int count)
{
    // the parameters the old mappings moved go back to their own values; a mapping kept (the same LFO
    // and target) keeps its smoothed offset, so changing a depth glides
    std::array<ModState, kMaxModMappings> next {};
    count = std::clamp (count, 0, kMaxModMappings);
    for (int i = 0; i < count; ++i)
        for (int j = 0; j < numMods; ++j)
            if (mods[(size_t)j].lfo == list[i].lfo && mods[(size_t)j].target == list[i].target)
            {
                next[(size_t)i] = modState[(size_t)j];
                next[(size_t)i].stale = true;
            }
    for (int j = 0; j < numMods; ++j)
        if (modState[(size_t)j].on)
            applyParam (mods[(size_t)j].target, base[mods[(size_t)j].target]);
    for (int i = 0; i < count; ++i)
        mods[(size_t)i] = list[i];
    numMods = count;
    modState = next;
}

bool Engine::modWorks (const ModMapping& m) const
{
    if (m.lfo < 0 || m.lfo >= kModLfos)
        return false;
    if (!isRackParam (m.target))
        return m.fxType < 0 && canModulate (m.target, kFxEmpty);
    const int type = rack.type (rackField (m.target).slot);
    return m.fxType == type && canModulate (m.target, type);
}

void Engine::modulate (int n)
{
    // each mapping's offset glides to depth x its LFO (4 ms); a mapping that stopped working (another
    // effect in its slot) gives its parameter back its own value
    const double coef = 1.0 - std::exp (-n / (0.004 * sr));
    bool restored = false;
    for (int i = 0; i < numMods; ++i)
    {
        const ModMapping& m = mods[(size_t)i];
        ModState& st = modState[(size_t)i];
        if (!modWorks (m))
        {
            if (st.on)
            {
                applyParam (m.target, base[m.target]);
                restored = true;
            }
            st.on = false;
            continue;
        }
        const double target = m.depth * mod.value (m.lfo);
        st.smooth = st.on ? st.smooth + (target - st.smooth) * coef : target;
        st.on = true;
    }
    // the parameters, each once: its value plus every working mapping's offset, held to its range
    for (int i = 0; i < numMods; ++i)
    {
        ModState& st = modState[(size_t)i];
        if (!st.on)
            continue;
        const uint32_t id = mods[(size_t)i].target;
        bool first = true;
        for (int j = 0; j < i && first; ++j)
            first = !(modState[(size_t)j].on && mods[(size_t)j].target == id);
        if (!first)
            continue;
        double sum = 0.0;
        bool stale = st.stale || restored;
        for (int j = i; j < numMods; ++j)
            if (modState[(size_t)j].on && mods[(size_t)j].target == id)
            {
                sum += modState[(size_t)j].smooth;
                stale = stale || modState[(size_t)j].stale;
                modState[(size_t)j].stale = false;
            }
        const double v = toPlain (id, std::clamp (toNormalized (id, base[id]) + sum, 0.0, 1.0));
        if (stale || v != st.applied)
        {
            applyParam (id, v);
            st.applied = v;
        }
    }
}

void Engine::render (float* L, float* R, int n, const HostInfo& host)
{
    if (numMods == 0)
    {
        mod.advance (base.data (), n, host.bpm, host.ppq, host.playing && host.ppqValid);
        renderStep (L, R, n, host);
        return;
    }
    const double ppqPerSample = (host.bpm > 0.0 ? host.bpm : 120.0) / 60.0 / sr;
    for (int pos = 0; pos < n;)
    {
        const int m = std::min (kModStep, n - pos);
        HostInfo h = host;
        h.ppq = host.ppq + pos * ppqPerSample;
        mod.advance (base.data (), m, h.bpm, h.ppq, h.playing && h.ppqValid);
        modulate (m);
        renderStep (L + pos, R + pos, m, h);
        pos += m;
    }
}

void Engine::renderStep (float* L, float* R, int n, const HostInfo& host)
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    globalLfoPhase += p[kLfoRate] * n / sr;
    globalLfoPhase -= std::floor (globalLfoPhase);
    if (!smp)
    {
        renderEffects (L, R, n, host);
        return;
    }
    updateSlices ();
    BlockCtx c;
    makeCtx (host, c);

    PlayRegion loopRegion;
    const bool liveLoop = idx (p[kMode]) == kModeClassic && regionFor (rootOf (p), loopRegion);
    // the extra playheads' regions follow their parameters as the main loop follows Start / Length
    PlayRegion headRegions[kMaxPlayheads - 1];
    bool liveHeads = false;
    if (liveLoop)
        for (const auto& v : voices)
            if (v.isActive () && v.heads () > 1)
            {
                for (int k = 1; k < kMaxPlayheads; ++k)
                    headRegion (*smp, p, k, headRegions[k - 1]);
                liveHeads = true;
                break;
            }
    for (auto& v : voices)
    {
        if (!v.isActive ())
            continue;
        if (liveLoop)
            v.updateLoop (loopRegion, liveHeads ? headRegions : nullptr);
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
    renderEffects (L, R, n, host);
}

void Engine::renderEffects (float* L, float* R, int n, const HostInfo& host)
{
    rack.setTransport (host.bpm > 0.0 ? host.bpm : 120.0, host.ppq, host.playing && host.ppqValid);
    rack.process (L, R, n);
    if (tail.isOn ())
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
    double heads[kMaxPlayheads];
    for (auto& v : voices)
        if (v.isActive () && !v.isKilling () && k < max)
        {
            // (every playhead of the voice)
            const int nh = v.displayPositions (heads, kMaxPlayheads);
            for (int h = 0; h < nh && k < max; ++h)
                out[k++] = (float)(heads[h] / smp->length);
        }
    return k;
}

} // namespace smemplr

#include "Engine.h"

#include "pluginkit/NoDenormals.h"

#include <algorithm>
#include <cmath>

namespace ciphr {

namespace {
inline float dbToGain (double db) { return (float)std::pow (10.0, db / 20.0); }
inline double smoothstep (double t) { return t * t * (3.0 - 2.0 * t); }
} // namespace

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

Engine::Engine () { applyVariant (); }

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    for (auto& v : voices)
        v.prepare (sr);
    applyEnvelopes ();
    space.prepare (sr);
    for (uint32_t id = kSpace; id <= kShift; ++id)
        setParam (id, p[id]);
    setParam (kCharacter, p[kCharacter]);
    disperse.prepare (sr);
    for (uint32_t id = kDisperseOn; id <= kDisperseMix; ++id)
        setParam (id, p[id]);
    applyVariant ();
    tail.prepare (sr, std::max (1, maxBlock));
    for (uint32_t f = 0; f < smacheratr::kTailAllFields; ++f)
    {
        const uint32_t id = smacheratr::tailParamOf (f, {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base});
        tail.setParam (f, p[id]);
    }
    smooth = (float)(1.0 - std::exp (-1.0 / (0.02 * sr)));
    sliceSmooth = 1.0 - std::exp (-(double)kSlice / (0.03 * sr));
    reset ();
}

void Engine::reset ()
{
    for (auto& v : voices)
        v.kill ();
    timbre = p[kTimbre];
    cross = (float)p[kCross];
    cutoffLog = std::log (p[kCutoff]);
    stretch = p[kStretch];
    blend = (float)p[kBlend];
    out = dbToGain (p[kOutput]);
    // Drift starts from the same place for a Variant every time
    driftRng = Rng (0xD71F7ull + 977ull * (uint64_t)(uint32_t)current.variant);
    for (int j = 0; j < kDriftChannels; ++j)
    {
        driftFrom[j] = 0.0;
        driftTo[j] = driftRng.range (-1.0, 1.0);
        driftT[j] = (double)j / kDriftChannels; // (the channels turn at different times)
    }
    prepareBlock (kSlice); // (Drift's tap offsets first: the processor's reset puts the taps where they belong)
    space.reset ();
    disperse.reset ();
    tail.reset ();
}

void Engine::applyEnvelopes ()
{
    for (auto& v : voices)
        v.setEnvelopes (p[kAttack], p[kDecay], p[kSustain], p[kRelease], p[kFilterAttack], p[kFilterDecay], p[kFilterSustain],
                        p[kFilterRelease]);
}

void Engine::applyVariant ()
{
    current = makePatch (std::clamp ((int)std::lround (p[kVariant]), kMinVariant, kMaxVariant),
                         std::clamp ((int)std::lround (p[kWaveSet]), 0, kNumWaveSets - 1));
    space.setPattern (current.taps);
}

void Engine::setParam (uint32_t id, double plain)
{
    if (id >= kNumParams)
        return;
    p[id] = plain;
    if (id >= kStretch)
        switch (id)
        {
            case kWaveSet: applyVariant (); break;
            case kDisperseOn: disperse.setOn (plain >= 0.5); break;
            case kDisperse: disperse.setAmount (plain); break;
            case kDisperseBands: disperse.setBands ((int)std::lround (plain)); break;
            case kDisperseSeed: disperse.setSeed ((int)std::lround (plain)); break;
            case kDisperseWidth: disperse.setWidth (plain); break;
            case kDisperseMix: disperse.setMix (plain); break;
            default: break; // (Stretch: read each slice)
        }
    else if (id >= kTailExt4Base)
        tail.setParam (smacheratr::kTailExt4First + (id - kTailExt4Base), plain);
    else if (id >= kTailExt3Base)
        tail.setParam (smacheratr::kTailExt3First + (id - kTailExt3Base), plain);
    else if (id >= kTailExt2Base)
        tail.setParam (smacheratr::kTailExt2First + (id - kTailExt2Base), plain);
    else if (id >= kTailExtBase)
        tail.setParam (pk::kTailFields + (id - kTailExtBase), plain);
    else if (id >= kTailBase)
        tail.setParam (id - kTailBase, plain);
    else
        switch (id)
        {
            case kVariant: applyVariant (); break;
            case kCharacter: space.setCharacter (plain); break;
            case kAttack:
            case kDecay:
            case kSustain:
            case kRelease:
            case kFilterAttack:
            case kFilterDecay:
            case kFilterSustain:
            case kFilterRelease: applyEnvelopes (); break;
            case kSpace: space.setSpace (plain); break;
            case kLength: space.setLength (plain); break;
            case kMovement: space.setMovement (plain); break;
            case kRegen: space.setRegen (plain); break;
            case kShift: space.setShift (plain); break;
            default: break;
        }
}

// ---- notes

void Engine::noteOn (int note, float velocity)
{
    if (velocity <= 0.0f)
    {
        noteOff (note);
        return;
    }
    ++order;
    // the same note held: retrigger it
    for (auto& v : voices)
        if (v.gated () && v.note () == note)
        {
            v.start (note, velocity, current, false, order);
            return;
        }
    // a silent voice
    for (auto& v : voices)
        if (!v.active ())
        {
            v.start (note, velocity, current, true, order);
            return;
        }
    // steal: the quietest released voice, else the oldest held one
    Voice* pick = nullptr;
    for (auto& v : voices)
        if (!v.gated () && (!pick || v.level () < pick->level ()))
            pick = &v;
    if (!pick)
        for (auto& v : voices)
            if (!pick || v.order () < pick->order ())
                pick = &v;
    pick->start (note, velocity, current, false, order);
}

void Engine::noteOff (int note)
{
    for (auto& v : voices)
        if (v.gated () && v.note () == note)
            v.release ();
}

void Engine::allNotesOff ()
{
    for (auto& v : voices)
        v.release ();
}

int Engine::activeVoices () const
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active () ? 1 : 0;
    return n;
}

// ---- processing

void Engine::advanceDrift (int n)
{
    const double d = std::clamp (p[kDrift], 0.0, 1.0);
    if (d <= 0.0)
        return; // static: nothing moves
    const double rate = 0.05 + 0.45 * d; // new random targets per second
    const double dt = rate * (double)n / sr;
    for (int j = 0; j < kDriftChannels; ++j)
    {
        driftT[j] += dt;
        while (driftT[j] >= 1.0)
        {
            driftT[j] -= 1.0;
            driftFrom[j] = driftTo[j];
            driftTo[j] = driftRng.range (-1.0, 1.0);
        }
    }
}

void Engine::prepareBlock (int n)
{
    const double d = std::clamp (p[kDrift], 0.0, 1.0);
    double drift[kDriftChannels];
    for (int j = 0; j < kDriftChannels; ++j)
        drift[j] = d * (driftFrom[j] + (driftTo[j] - driftFrom[j]) * smoothstep (driftT[j]));

    timbre += (p[kTimbre] - timbre) * sliceSmooth;
    if (std::fabs (timbre - p[kTimbre]) < 1e-6)
        timbre = p[kTimbre];
    const double pos = std::clamp (timbre, 0.0, 1.0) * (kEntries - 1);
    const double spread = std::clamp (p[kCharacter], 0.0, 1.0) * kDetuneCents;
    // Stretch glides (a pitch move); at 0 it adds nothing at all
    const double stretchT = std::clamp (p[kStretch], 0.0, 1.0);
    stretch += (stretchT - stretch) * sliceSmooth;
    if (std::fabs (stretch - stretchT) < 1e-6)
        stretch = stretchT;
    for (int k = 0; k < kOscs; ++k)
    {
        block.oscPos[k] = std::clamp (pos + kDriftPos * drift[k], 0.0, (double)(kEntries - 1));
        const double side = kOscs > 1 ? (2.0 * k / (kOscs - 1) - 1.0) : 0.0;
        block.oscCents[k] = spread * side + kDriftCents * drift[kOscs + k];
        if (stretch != 0.0)
            block.oscCents[k] += 100.0 * stretch * kStretchSemis[k];
    }
    double tapTime[kTaps], tapGain[kTaps];
    for (int i = 0; i < kTaps; ++i)
    {
        tapTime[i] = drift[2 * kOscs + i];
        tapGain[i] = drift[2 * kOscs + kTaps + i];
    }
    space.setDrift (tapTime, tapGain);

    // Cross ramps over the slice; it lands exactly on its target (so the centre is exactly clean)
    block.crossFrom = cross;
    const float target = (float)p[kCross];
    cross += (target - cross) * (float)sliceSmooth;
    if (std::fabs (target - cross) < 1e-4f)
        cross = target;
    block.crossTo = cross;

    cutoffLog += (std::log (std::clamp (p[kCutoff], 20.0, 20000.0)) - cutoffLog) * sliceSmooth;
    block.sr = sr;
    block.tune = p[kTune];
    block.cutoff = std::exp (cutoffLog);
    block.resonance = p[kResonance];
    block.type = p[kFilterType];
    block.keyTrack = p[kKeyTrack];
    block.envAmount = p[kEnvAmount];
    block.velocity = (float)p[kVelocity];
    (void)n;
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const pk::NoDenormals guard;
    const float blendT = (float)std::clamp (p[kBlend], 0.0, 1.0), outT = dbToGain (p[kOutput]);
    const float inGain = (float)std::clamp (p[kInput], 0.0, 1.0);
    const bool viaVoices = std::lround (p[kInputPath]) == kPathVoices;
    float xl[kSlice], xr[kSlice], mono[kSlice], v[kSlice], wl[kSlice], wr[kSlice];
    for (int a = 0; a < n; a += kSlice)
    {
        const int m = std::min (kSlice, n - a);
        // the input first (the output may be the same buffer)
        for (int i = 0; i < m; ++i)
        {
            xl[i] = inL ? inL[a + i] : 0.0f;
            xr[i] = inR ? inR[a + i] : 0.0f;
            mono[i] = 0.5f * (xl[i] + xr[i]);
        }
        advanceDrift (m);
        prepareBlock (m);
        block.input = viaVoices && inGain > 0.0f ? mono : nullptr;
        block.inputGain = inGain;
        std::fill (v, v + m, 0.0f);
        for (auto& voice : voices)
            voice.render (v, m, block, current);
        const float direct = viaVoices ? 0.0f : inGain;
        for (int i = 0; i < m; ++i)
        {
            wl[i] = v[i] * kVoiceGain + xl[i] * direct;
            wr[i] = v[i] * kVoiceGain + xr[i] * direct;
        }
        if (!disperse.idle ())
            disperse.process (wl, wr, m); // (off: skipped altogether, so nothing changes)
        for (int i = 0; i < m; ++i)
        {
            xl[i] = wl[i]; // (the dry signal)
            xr[i] = wr[i];
        }
        space.process (wl, wr, m);
        for (int i = 0; i < m; ++i)
        {
            blend += (blendT - blend) * smooth;
            out += (outT - out) * smooth;
            outL[a + i] = (xl[i] + (wl[i] - xl[i]) * blend) * out;
            outR[a + i] = (xr[i] + (wr[i] - xr[i]) * blend) * out;
        }
    }
    if (meters)
    {
        constexpr auto rx = std::memory_order_relaxed;
        meters->voices.store (activeVoices (), rx);
        for (int k = 0; k < kOscs; ++k)
            meters->oscPos[(size_t)k].store ((float)block.oscPos[k], rx);
        for (int i = 0; i < kTaps; ++i)
        {
            meters->tapMs[(size_t)i].store ((float)(space.tapDelay (i) * 1000.0 / sr), rx);
            meters->tapLevel[(size_t)i].store ((float)space.tapLevel (i), rx);
        }
        meters->blocks.fetch_add (1, std::memory_order_release);
    }
    tail.process (outL, outR, n);
}

} // namespace ciphr

#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace wubr {

namespace {
inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
constexpr int kCoeffEvery = 16; // samples between band retunings
} // namespace

double bellQ (double widthOct)
{
    const double b = std::pow (2.0, std::clamp (widthOct, 0.1, 8.0));
    return std::sqrt (b) / (b - 1.0);
}

ParamArray defaultParams ()
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = paramTable ().info (i).def;
    return p;
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    tail.prepare (sr, maxBlock);
    for (uint32_t id = 0; id < kNumParams; ++id)
        if (isTailParam (id))
            tail.setParam (tailField (id), p[id]);
    fastA = 1.0 - std::exp (-1.0 / (0.001 * sr));
    fastR = 1.0 - std::exp (-1.0 / (0.010 * sr));
    slowC = 1.0 - std::exp (-1.0 / (0.150 * sr));
    smoothGain = 1.0 - std::exp (-1.0 / (0.003 * sr));
    smoothFreq = 1.0 - std::exp (-1.0 / (0.003 * sr / kCoeffEvery)); // per retuning
    if (meters)
        meters->sampleRate.store ((float)sr);
    reset ();
}

void Engine::reset ()
{
    for (int b = 0; b < kBands; ++b)
    {
        Band& band = bands[b];
        for (int c = 0; c < 2; ++c)
        {
            band.bell[c].reset ();
        }
        band.phase = 0.0;
        band.envPos = 1.0; // at rest, as if the envelope had run
        band.freqNow = std::clamp (p[bandParam (b, kFreq)], 20.0, 0.45 * sr);
        band.dbNow = 0.0;
        band.active = false;
        band.shapeDirty = true;
    }
    fastEnv = slowEnv = 0.0;
    holdOff = 0;
    notesDown.reset ();
    coeffCountdown = 0;
    mix = (float)std::clamp (p[kDryWet], 0.0, 1.0);
    out = (float)dbToGain (p[kOutput]);
    tail.reset ();
}

void Engine::setParam (uint32_t id, double plain)
{
    if (id >= kNumParams)
        return;
    p[id] = plain;
    if (isTailParam (id))
        tail.setParam (tailField (id), plain);
    else if (id >= kBandBase && id < kTailExtBase)
        bands[(id - kBandBase) / kBandBlock].shapeDirty = true;
}

void Engine::setTransport (double tempo, double songPpq, bool isPlaying)
{
    bpm = tempo > 1.0 ? tempo : 120.0;
    ppq = songPpq;
    playing = isPlaying;
}

void Engine::noteOn (int note)
{
    notesDown.set ((size_t)std::clamp (note, 0, 127));
    if (std::lround (p[kMode]) == kEnvelope && std::lround (p[kTrigger]) == kMidi)
        trigger ();
}

void Engine::noteOff (int note) { notesDown.reset ((size_t)std::clamp (note, 0, 127)); }

void Engine::allNotesOff () { notesDown.reset (); }

void Engine::trigger ()
{
    for (auto& b : bands)
        b.envPos = 0.0;
    if (meters)
        meters->triggers.fetch_add (1, std::memory_order_relaxed);
}

double Engine::cycleHz (int b) const
{
    if (std::lround (p[bandParam (b, kRateMode)]) == kFree)
        return std::max (0.001, p[bandParam (b, kRateHz)]);
    const int d = std::clamp ((int)std::lround (p[bandParam (b, kSync)]), 0, kSyncDivisions - 1);
    return bpm / 60.0 / kSyncBeats[d];
}

void Engine::refreshShape (int b)
{
    bands[b].shape = shapeOf (b, [this] (uint32_t id) { return p[id]; });
    bands[b].shapeDirty = false;
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const bool envMode = std::lround (p[kMode]) == kEnvelope;
    const bool transient = envMode && std::lround (p[kTrigger]) == kTransient;
    const float mixT = (float)std::clamp (p[kDryWet], 0.0, 1.0), outT = (float)dbToGain (p[kOutput]);
    const double sensDb = p[kSensitivity];

    bool on[kBands];
    double inc[kBands], offset[kBands];
    int target[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        Band& band = bands[b];
        if (band.shapeDirty)
            refreshShape (b);
        on[b] = p[bandParam (b, kBandOn)] >= 0.5;
        inc[b] = cycleHz (b) / sr;
        offset[b] = p[bandParam (b, kPhase)] / 360.0;
        target[b] = std::clamp ((int)std::lround (p[bandParam (b, kTarget)]), (int)kTargetGain, (int)kTargetBoth);
        // synced LFOs follow the song position while the host plays
        if (!envMode && playing && std::lround (p[bandParam (b, kRateMode)]) == kSynced)
        {
            const int d = std::clamp ((int)std::lround (p[bandParam (b, kSync)]), 0, kSyncDivisions - 1);
            const double cycles = ppq / kSyncBeats[d];
            band.phase = cycles - std::floor (cycles);
        }
    }

    for (int i = 0; i < n; ++i)
    {
        const float dryL = inL[i], dryR = inR[i];
        // a transient: the fast level jumps over the slow one by the sensitivity
        if (transient)
        {
            const double lvl = std::max (std::fabs ((double)dryL), std::fabs ((double)dryR));
            fastEnv += (lvl - fastEnv) * (lvl > fastEnv ? fastA : fastR);
            slowEnv += (lvl - slowEnv) * slowC;
            if (holdOff > 0)
                --holdOff;
            else if (fastEnv > 0.003 && 20.0 * std::log10 ((fastEnv + 1e-9) / (slowEnv + 1e-9)) > sensDb)
            {
                trigger ();
                holdOff = (int)(0.06 * sr);
                slowEnv = fastEnv; // the hit is the new level: its tail does not trigger again
            }
        }
        double x[2] = {dryL, dryR};
        const bool retune = coeffCountdown <= 0;
        coeffCountdown = retune ? kCoeffEvery - 1 : coeffCountdown - 1;
        for (int b = 0; b < kBands; ++b)
        {
            Band& band = bands[b];
            // where the band is in its shape
            double pos;
            if (envMode)
            {
                // runs to the hold point and stays (a held note, or always with transients; a hold point
                // moved earlier takes it back); let go: to the end
                const bool hold = transient || notesDown.any ();
                const double stop = hold ? band.shape.holdX () : 1.0;
                band.envPos = band.envPos < stop ? std::min (stop, band.envPos + inc[b]) : (hold ? stop : band.envPos);
                pos = band.envPos;
            }
            else
            {
                band.phase += inc[b];
                band.phase -= std::floor (band.phase);
                pos = band.phase + offset[b];
                pos -= std::floor (pos);
            }
            if (retune)
            {
                const double y = band.shape.valueAt (pos);
                band.shownPos = pos;
                band.shownValue = y;
                // an off band fades to 0 dB, then stops filtering; turned on, it starts from 0 dB
                double gainDb = 0.0;
                double freq = p[bandParam (b, kFreq)];
                if (on[b])
                {
                    gainDb = p[bandParam (b, kGain)] + (target[b] != kTargetFreq ? p[bandParam (b, kDepth)] * y : 0.0);
                    if (target[b] != kTargetGain)
                        freq *= std::pow (2.0, 0.5 * y * p[bandParam (b, kSweep)]);
                }
                freq = std::clamp (freq, 20.0, 0.45 * sr);
                if (!band.active)
                {
                    if (!on[b])
                    {
                        band.freqNow = freq;
                        continue;
                    }
                    band.active = true;
                    band.freqNow = freq;
                    band.dbNow = 0.0;
                    for (auto& f : band.bell)
                        f.reset ();
                }
                band.freqNow *= std::pow (freq / std::max (1.0, band.freqNow), smoothFreq);
                band.freqNow = std::clamp (band.freqNow, 20.0, 0.45 * sr);
                band.dbNow += (std::clamp (gainDb, -48.0, 36.0) - band.dbNow) * smoothFreq;
                if (!on[b] && std::fabs (band.dbNow) < 0.01)
                {
                    band.active = false; // faded out
                    continue;
                }
                const smacheratr::BiquadCoeffs c =
                    smacheratr::peak (sr, band.freqNow, band.dbNow, bellQ (p[bandParam (b, kWidth)]));
                for (int ch = 0; ch < 2; ++ch)
                    band.bell[ch].c = c;
                band.shownDb = band.dbNow;
            }
            if (!band.active)
                continue;
            for (int ch = 0; ch < 2; ++ch)
                x[ch] = band.bell[ch].process (x[ch]);
        }
        mix += (mixT - mix) * (float)smoothGain;
        out += (outT - out) * (float)smoothGain;
        outL[i] = (float)((dryL * (1.0f - mix) + x[0] * mix) * out);
        outR[i] = (float)((dryR * (1.0f - mix) + x[1] * mix) * out);
    }
    if (hasTail)
        tail.process (outL, outR, n);
    if (playing)
        ppq += n * bpm / 60.0 / sr; // the song moves on (a block split at notes stays in time)

    if (meters)
    {
        for (int b = 0; b < kBands; ++b)
        {
            meters->pos[b].store ((float)bands[b].shownPos, std::memory_order_relaxed);
            meters->value[b].store ((float)bands[b].shownValue, std::memory_order_relaxed);
            meters->gainDb[b].store (bands[b].active ? (float)bands[b].shownDb : 0.0f, std::memory_order_relaxed);
            meters->freqHz[b].store ((float)bands[b].freqNow, std::memory_order_relaxed);
        }
        meters->blocks.fetch_add (1, std::memory_order_relaxed);
    }
}

} // namespace wubr

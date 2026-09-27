// Breakpoint envelope: 0 -> attack -> peak (1.0) -> up to kMaxEnvPoints breakpoints -> decay ->
// sustain -> release -> 0. Every segment has a curve in [-1, 1]: 0 is linear, negative bends
// towards a fast start (exponential decay shape), positive towards a slow start.
// With no breakpoints and the default curves this is the classic ADSR.
// The amplitude envelope also implements the loop modes (Trigger/Loop return here;
// Beat/Sync retriggering is driven by the voice).
#pragma once

#include "Params.h"

#include <algorithm>
#include <cmath>

namespace smempler {

struct EnvSettings
{
    float attackMs = 0.1f;
    float decayMs = 600.0f;
    float sustain = 1.0f;
    float releaseMs = 50.0f;
    float curveA = 0.0f, curveD = -0.5f, curveR = -0.5f;
    int points = 0;
    float ptMs[kMaxEnvPoints] {};
    float ptLevel[kMaxEnvPoints] {};
    float ptCurve[kMaxEnvPoints] {};
    int loopMode = 0; // AmpLoop enum; only used by the amp envelope
    float loopMs = 100.0f;
};

// Segment shape: maps progress u in [0, 1] to 0..1 (exact at both ends).
inline float envCurve (float u, float c)
{
    u = std::clamp (u, 0.0f, 1.0f);
    if (std::fabs (c) < 1e-3f)
        return u;
    const float k = std::fabs (c) * 8.0f;
    const float e = std::exp (-k);
    if (c < 0.0f)
        return (1.0f - std::exp (-k * u)) / (1.0f - e);
    return 1.0f - (1.0f - std::exp (-k * (1.0f - u))) / (1.0f - e);
}

class Envelope
{
public:
    enum Stage { Idle, Attack, Point, Decay, Sustain, Release, LoopReturn };

    void reset ()
    {
        stage = Idle;
        val = 0.0f;
        gate = false;
        pendingRelease = false;
    }

    void noteOn ()
    {
        gate = true;
        pendingRelease = false;
        stage = Attack;
        begin (val, 1.0f);
    }

    void noteOff (int loopMode)
    {
        if (!gate)
            return;
        gate = false;
        // Trigger mode: let the current attack/decay cycle finish before releasing.
        if (loopMode == 1 && (stage == Attack || stage == Point || stage == Decay || stage == LoopReturn))
        {
            pendingRelease = true;
            return;
        }
        beginRelease ();
    }

    void retrigger ()
    {
        if (gate)
        {
            stage = Attack;
            begin (val, 1.0f);
        }
    }

    bool idle () const { return stage == Idle; }
    bool held () const { return gate; }
    float value () const { return val; }

    float process (const EnvSettings& s, float sr)
    {
        switch (stage)
        {
            case Idle: val = 0.0f; break;
            case Attack:
                if (step (s.attackMs, s.curveA, sr))
                    enterAfterPeak (s, 0);
                break;
            case Point:
                to = s.ptLevel[pointIndex];
                if (step (s.ptMs[pointIndex], s.ptCurve[pointIndex], sr))
                    enterAfterPeak (s, pointIndex + 1);
                break;
            case Decay:
                to = s.sustain;
                if (step (s.decayMs, s.curveD, sr))
                {
                    if (pendingRelease)
                        beginRelease ();
                    else if (gate && (s.loopMode == 1 || s.loopMode == 2))
                    {
                        stage = LoopReturn;
                        begin (val, 0.0f);
                    }
                    else
                        stage = Sustain;
                }
                break;
            case Sustain:
                val += (s.sustain - val) * 0.002f;
                if (pendingRelease)
                    beginRelease ();
                break;
            case LoopReturn:
                if (step (s.loopMs, 0.0f, sr))
                {
                    if (pendingRelease)
                        beginRelease ();
                    else
                    {
                        stage = Attack;
                        begin (val, 1.0f);
                    }
                }
                break;
            case Release:
                if (step (s.releaseMs, s.curveR, sr))
                {
                    val = 0.0f;
                    stage = Idle;
                }
                break;
        }
        return val;
    }

    Stage stage = Idle;

private:
    void begin (float fromValue, float toValue)
    {
        from = fromValue;
        to = toValue;
        u = 0.0f;
        rateMs = -1.0f;
    }

    void enterAfterPeak (const EnvSettings& s, int nextPoint)
    {
        if (nextPoint < std::clamp (s.points, 0, kMaxEnvPoints))
        {
            stage = Point;
            pointIndex = nextPoint;
            begin (val, s.ptLevel[nextPoint]);
        }
        else
        {
            stage = Decay;
            begin (val, s.sustain);
        }
    }

    void beginRelease ()
    {
        pendingRelease = false;
        gate = false;
        if (stage == Idle)
            return;
        stage = Release;
        begin (val, 0.0f);
    }

    // Advances the current segment by one sample. Exponential shapes are evaluated
    // incrementally (one multiply per sample). Returns true when the segment is complete.
    bool step (float ms, float curve, float sr)
    {
        if (ms != rateMs || curve != rateCurve || sr != rateSr)
        {
            rateMs = ms;
            rateCurve = curve;
            rateSr = sr;
            inc = 1.0f / std::max (1.0f, ms * 0.001f * sr);
            k = std::fabs (curve) * 8.0f;
            endY = std::exp (-k);
            mult = std::exp ((curve < 0.0f ? -k : k) * inc);
            // (re)anchor the incremental term at the current progress
            y = curve < 0.0f ? std::exp (-k * u) : std::exp (-k * (1.0f - u));
        }
        u += inc;
        if (u >= 1.0f)
        {
            val = to;
            return true;
        }
        float f;
        if (k < 0.008f)
            f = u;
        else
        {
            y *= mult;
            f = rateCurve < 0.0f ? (1.0f - y) / (1.0f - endY) : 1.0f - (1.0f - y) / (1.0f - endY);
        }
        val = from + (to - from) * f;
        return false;
    }

    float val = 0.0f, from = 0.0f, to = 0.0f, u = 0.0f;
    float inc = 1.0f, k = 0.0f, endY = 1.0f, mult = 1.0f, y = 1.0f;
    float rateMs = -1.0f, rateCurve = 0.0f, rateSr = 0.0f;
    int pointIndex = 0;
    bool gate = false, pendingRelease = false;
};

} // namespace smempler

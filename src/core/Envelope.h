// ADSR with exponential decay/release segments that land exactly on their targets, plus the
// amplitude-envelope loop modes (Trigger/Loop return; Beat/Sync retrigger is driven by the voice).
#pragma once

#include <algorithm>
#include <cmath>

namespace simplr {

struct EnvSettings
{
    float attackMs = 0.1f;
    float decayMs = 600.0f;
    float sustain = 1.0f;
    float releaseMs = 50.0f;
    int loopMode = 0; // AmpLoop enum; only used by the amp envelope
    float loopMs = 100.0f;
};

class Envelope
{
public:
    enum Stage { Idle, Attack, Decay, Sustain, Release, LoopReturn };

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
    }

    void noteOff (int loopMode)
    {
        if (!gate)
            return;
        gate = false;
        // Trigger mode: let the current attack/decay cycle finish before releasing.
        if (loopMode == 1 && (stage == Attack || stage == Decay || stage == LoopReturn))
        {
            pendingRelease = true;
            return;
        }
        beginRelease ();
    }

    void retrigger ()
    {
        if (gate)
            stage = Attack;
    }

    void forceRelease () { beginRelease (); }

    bool idle () const { return stage == Idle; }
    bool held () const { return gate; }
    float value () const { return val; }

    float process (const EnvSettings& s, float sr)
    {
        switch (stage)
        {
            case Idle: val = 0.0f; break;
            case Attack:
            {
                const float n = std::max (1.0f, s.attackMs * 0.001f * sr);
                val += 1.0f / n;
                if (val >= 1.0f)
                {
                    val = 1.0f;
                    startExp (Decay, val, s.sustain, s.decayMs, sr);
                }
                break;
            }
            case Decay:
            {
                target = s.sustain;
                updateRate (s.decayMs, sr);
                if (stepExp ())
                {
                    if (pendingRelease)
                        beginRelease ();
                    else if (gate && (s.loopMode == 1 || s.loopMode == 2))
                    {
                        stage = LoopReturn;
                        loopFrom = val;
                        loopPos = 0.0f;
                    }
                    else
                        stage = Sustain;
                }
                break;
            }
            case Sustain:
                val += (s.sustain - val) * 0.002f;
                if (pendingRelease)
                    beginRelease ();
                break;
            case LoopReturn:
            {
                const float n = std::max (1.0f, s.loopMs * 0.001f * sr);
                loopPos += 1.0f / n;
                val = loopFrom * (1.0f - std::min (1.0f, loopPos));
                if (loopPos >= 1.0f)
                {
                    if (pendingRelease)
                        beginRelease ();
                    else
                        stage = Attack;
                }
                break;
            }
            case Release:
            {
                target = 0.0f;
                updateRate (s.releaseMs, sr);
                if (stepExp ())
                {
                    val = 0.0f;
                    stage = Idle;
                }
                break;
            }
        }
        return val;
    }

    Stage stage = Idle;

private:
    static constexpr float kCurve = 4.0f;

    void beginRelease ()
    {
        pendingRelease = false;
        gate = false;
        if (stage == Idle)
            return;
        stage = Release;
        from = val;
        target = 0.0f;
        y = 1.0f;
        rateMs = -1.0f;
    }

    void startExp (Stage st, float start, float tgt, float ms, float sr)
    {
        stage = st;
        from = start;
        target = tgt;
        y = 1.0f;
        rateMs = -1.0f;
        updateRate (ms, sr);
    }

    void updateRate (float ms, float sr)
    {
        if (ms == rateMs && sr == rateSr)
            return;
        rateMs = ms;
        rateSr = sr;
        const float n = std::max (1.0f, ms * 0.001f * sr);
        mult = std::exp (-kCurve / n);
    }

    // Returns true when the segment has finished.
    bool stepExp ()
    {
        static const float endY = std::exp (-kCurve);
        y *= mult;
        if (y <= endY)
        {
            val = target;
            return true;
        }
        val = target + (from - target) * (y - endY) / (1.0f - endY);
        return false;
    }

    float val = 0.0f, from = 0.0f, target = 0.0f, y = 1.0f, mult = 1.0f;
    float rateMs = -1.0f, rateSr = 0.0f;
    float loopFrom = 0.0f, loopPos = 0.0f;
    bool gate = false, pendingRelease = false;
};

} // namespace simplr

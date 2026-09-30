// The optional Smacheratr at the end of a plug-in's chain (its parameters: pk::addTailParams). It
// is always in the path, with its dry/wet at zero when off, so the latency it adds never changes;
// while off it skips the curve and only delays the signal.
#pragma once

#include "Engine.h"
#include "TailExt.h"

#include "pluginkit/TailParams.h"

namespace smacheratr {

// every field of the tail: the three blocks
constexpr uint32_t kTailAllFields = pk::kTailFields + pk::kTailExtFields + pk::kTailExt2Fields;

class Tail
{
public:
    Tail ()
    {
        eng.setParam (kColorOn, 0.0);
        eng.setParam (kHiQuality, 1.0);
        eng.setParam (kDcFilter, 0.0);
        eng.setParam (kOutput, 0.0);
        eng.setParam (kDrive, 0.0);
        eng.setParam (kDryWet, 0.0);
    }
    void prepare (double sampleRate, int maxBlock) { eng.prepare (sampleRate, maxBlock); }
    void reset () { eng.reset (); }
    int latency () const { return eng.latency (); }
    void setMeters (Meters* m) { eng.setMeters (m); }
    bool isOn () const { return on; }
    // saturate mid and side apart (see smacheratr::kMidSide)
    void setMidSide (bool ms) { eng.setParam (kMidSide, ms ? 1.0 : 0.0); }

    // the plain value of one of the tail's fields (a pk::TailField, pk::kTailFields + a pk::TailExtField,
    // or pk::kTailFields + pk::kTailExtFields + a pk::TailExt2Field)
    void setParam (uint32_t field, double v)
    {
        if (field >= pk::kTailFields)
        {
            if (field < pk::kTailFields + pk::kTailExtFields)
                eng.setParam (kTailExtIds[field - pk::kTailFields], v);
            else if (field < kTailAllFields)
                eng.setParam (kTailExt2Ids[field - pk::kTailFields - pk::kTailExtFields], v);
            return;
        }
        switch (field)
        {
            case pk::kTailOn: on = v >= 0.5; break;
            case pk::kTailPreLimit: eng.setParam (kPreLimit, v); break;
            case pk::kTailDrive: eng.setParam (kDrive, v); break;
            case pk::kTailPostClip: eng.setParam (kPostClip, v); break;
            case pk::kTailMix: mix = v; break;
            case pk::kTailThreshold: eng.setParam (kPreLimitThreshold, v); break;
            default: return;
        }
        eng.setParam (kDryWet, on ? mix : 0.0);
    }

    // In place.
    void process (float* l, float* r, int n) { eng.process (l, r, l, r, n); }

private:
    Engine eng;
    bool on = false;
    double mix = 1.0;
};

} // namespace smacheratr

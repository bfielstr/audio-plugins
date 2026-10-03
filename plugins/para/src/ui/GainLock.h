// The Gain Locks in an editor: a ParamHost in front of another one (Para's editor, or Smemplr's host of a
// Para slot) that keeps a locked filter's gain at or below 0 dB. Every edit of a gain (the knobs, the
// display's handles, right-click / double-click resets, the mouse wheel) goes through it: with its lock
// on, a value above 0 dB's is set as 0 dB's (a knob turned past it stops there). Switching a lock on
// brings a gain above 0 dB down to 0 dB, so what the knob shows is what plays (the engine caps it too,
// for automation: lockedGainDb).
#pragma once

#include "../core/Params.h"

#include "pluginkit/ui/Widgets.h"

namespace para {

class GainLockHost : public pk::ParamHost
{
public:
    explicit GainLockHost (pk::ParamHost* inner) : in (inner) {}
    pk::ParamHost* inner () const { return in; }
    const pk::ParamTable& table () override { return in->table (); }
    double norm (uint32_t id) override { return in->norm (id); }
    double plainValue (uint32_t id) override { return in->plainValue (id); }
    void beginEdit (uint32_t id) override { in->beginEdit (id); }
    void setNorm (uint32_t id, double v) override
    {
        if (const uint32_t lock = gainLockOf (id))
            v = lockedGainNormalized (id, v, in->plainValue (lock) >= 0.5);
        in->setNorm (id, v);
        if (const uint32_t gain = gainOfLock (id); gain && v >= 0.5)
        {
            const double capped = lockedGainNormalized (gain, in->norm (gain), true);
            if (capped < in->norm (gain))
                in->setOnce (gain, capped);
        }
    }
    void endEdit (uint32_t id) override { in->endEdit (id); }
    std::string valueText (uint32_t id) override { return in->valueText (id); }
    int64_t sourceParam (uint32_t id) override { return in->sourceParam (id); }

private:
    pk::ParamHost* in;
};

} // namespace para

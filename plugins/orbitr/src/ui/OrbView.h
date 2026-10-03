// Orbitr's display (Detonatr's Motion page): the orbs seen from above, round the listener at the
// bottom, facing up, with their trails, the ball (Swarm) or the circles' reach (Orbit) round the
// centre at the Distance, and rings every metre (every 5 m when far).
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace orbitr {

class OrbView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    OrbView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle (); // follows the meters

private:
    pk::ParamHost* host;
    MeterSource meters;
    int orbs = 0;
    float x[Motion::kMaxOrbs] {}, y[Motion::kMaxOrbs] {};
    float distance = 3.0f, radius = 2.0f;
    // each orb's trail (the last positions)
    static constexpr int kTrail = 12;
    float tx[Motion::kMaxOrbs][kTrail] {}, ty[Motion::kMaxOrbs][kTrail] {};
    int trailPos = 0;
    uint32_t seen = 0;
};

} // namespace orbitr

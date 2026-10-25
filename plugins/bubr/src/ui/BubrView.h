// Bubr's display. At the left, the effect's response (the band at Freq and Q-Factor with its gains and Low Cut,
// while it is not clipping) from 10 Hz to 1 kHz, with Freq and Low Cut marked. At the right, the gate's curve:
// the band's gain against the input's peak (-72 to 0 dB), with the input's level now, the gate's gain and the
// output limiter's gain reduction while audio plays. The grid is a cached layer; the view repaints when a setting
// changed or the meters moved.
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace bubr {

class BubrView : public VSTGUI::CView
{
public:
    static constexpr double kMinHz = 10.0, kMaxHz = 1000.0, kMinDb = -24.0, kMaxDb = 48.0;
    static constexpr double kMinIn = -72.0, kMaxIn = 0.0; // the gate's box (dB)
    static constexpr double kScaleH = 14.0;               // the frequency labels' strip at the bottom
    using MeterSource = std::function<const Meters* ()>;

    BubrView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();

    VSTGUI::CRect responseRect () const; // the response's part of the view
    VSTGUI::CRect gateRect () const;     // the gate's box (empty when the view is too narrow for it)
    double xOfHz (double hz) const;
    double yOfDb (double db) const;

private:
    void paintGrid (VSTGUI::CDrawContext* ctx);
    bool live () const { return lastBlocks != 0 && idleSinceBlock < 10; }

    pk::ParamHost* host;
    MeterSource meters;
    float shownIn = -120.0f, shownGate = 0.0f, shownLimit = 0.0f;
    uint32_t lastBlocks = 0;
    int idleSinceBlock = 1 << 20;
    pk::CachedLayer gridLayer;
};

} // namespace bubr

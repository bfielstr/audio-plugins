// Ciphr's display: the patch the Variant stands for and where the sound is in it.
//
//   left   the cluster: one row per oscillator, its list's waves side by side (each drawn as a small trace,
//          its pitch offset in semitones beside it), and a cinnabar mark at the oscillator's place in the
//          list now (Timbre, moved by Drift while it plays)
//   right  the processor: its taps against time (0 .. Length), each a line as tall as its level, a haze
//          over them as thick as Space's diffusion, and the feedback's settings
//
// The waves, the taps and the haze change only with Variant, Wave Set, Character, Space, Length, Regen, Shift and the
// view's size: they are a cached layer (pk::CachedLayer); the marks and the voices' count are drawn over it.
#pragma once

#include "Engine.h"
#include "Params.h"
#include "Variant.h"

#include "pluginkit/ui/CachedLayer.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace ciphr {

class CipherView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    CipherView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle (); // follows the meters

private:
    void paintBase (VSTGUI::CDrawContext* ctx);
    const Patch& patch ();
    VSTGUI::CRect clusterArea () const;
    VSTGUI::CRect tapsArea () const;

    pk::ParamHost* host;
    MeterSource meters;
    Patch cached;
    int cachedVariant = -1, cachedSet = -1;
    float pos[kOscs] {};
    bool havePos = false;
    int voices = 0;
    uint32_t seen = 0;
    pk::CachedLayer baseLayer;
};

} // namespace ciphr

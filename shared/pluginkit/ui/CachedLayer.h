// A view's static drawing kept in an offscreen bitmap (issue #5): the grid, scales, labels and curves a
// display repaints 30 times a second while only a meter or a playhead moves. The view paints the layer
// once through draw (); later draws blit the bitmap and the view paints only what moves over it.
//
// The bitmap is rendered in device pixels: the view's rectangle as the context maps it (the frame's
// zoom, Interface Size, and the backing scale on Retina), snapped out to whole pixels with the
// fractional phase kept, so a blit lands 1:1 on the screen's pixels and looks exactly like drawing
// directly (no resampling, sharp at every zoom). It is rebuilt when the key the view passes changes
// (a hash of everything the layer depends on: parameters, modes, data revisions, see LayerKey), and
// when its size, device scale or sub-pixel phase changes. If the bitmap cannot be made (no offscreen
// support, a rotated transform, an absurd size) the layer is painted directly, as before.
#pragma once

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/crect.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

namespace pk {

struct ParamHost;

// A running FNV-1a hash of what a cached layer depends on. Doubles are hashed by their bits, so any
// change (however small) rebuilds the layer: the cache never shows stale content.
class LayerKey
{
public:
    LayerKey& add (double v)
    {
        if (v == 0.0)
            v = 0.0; // -0 and +0 draw the same
        uint64_t b;
        std::memcpy (&b, &v, sizeof (b));
        return bytes (&b, sizeof (b));
    }
    LayerKey& add (float v) { return add ((double)v); }
    // any integer, bool or enum
    template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
    LayerKey& add (T v)
    {
        const uint64_t x = (uint64_t)v;
        return bytes (&x, sizeof (x));
    }
    // a pointer by its address (a sample, a list: replaced, not changed in place)
    LayerKey& add (const void* p) { return add ((uint64_t)(uintptr_t)p); }
    LayerKey& add (const std::string& s) { return add (s.size ()).bytes (s.data (), s.size ()); }
    LayerKey& add (const char* s) { return add (std::string (s ? s : "")); }
    LayerKey& add (const VSTGUI::CRect& r) { return add (r.left).add (r.top).add (r.right).add (r.bottom); }
    // Every parameter's value through `host` (its whole table): for layers drawn from many of them. A
    // few hundred values hash in microseconds, far less than drawing.
    LayerKey& params (ParamHost* host);
    template <typename A, typename B, typename... More>
    LayerKey& add (const A& a, const B& b, const More&... more)
    {
        add (a);
        return add (b, more...);
    }
    uint64_t value () const { return h; }
    operator uint64_t () const { return h; }

private:
    LayerKey& bytes (const void* p, size_t n)
    {
        const auto* c = static_cast<const unsigned char*> (p);
        for (size_t i = 0; i < n; ++i)
            h = (h ^ c[i]) * 1099511628211ull;
        return *this;
    }
    uint64_t h = 1469598103934665603ull;
};

class CachedLayer
{
public:
    // Draws the layer covering `area` (in the coordinates draw () gets) into ctx: from the bitmap when
    // nothing changed, else through `paint (ctx)`, which draws in the same coordinates as it would
    // directly, clipped to `area`. The bitmap starts transparent, so a layer may cover only part of the
    // area and be drawn over other content. The context's state (colours, line width, font, clip) is
    // left as it was. A layer whose key changes on every draw (a handle being dragged, a resize) is
    // painted directly: the bitmap is made once the key holds for a second draw, so a change costs
    // nothing extra and the meter ticks that follow it blit.
    // Keep text in a layer that paints its own background (the bottom one): glyphs antialiased onto a
    // transparent bitmap and then blitted come out a little different from glyphs drawn onto the
    // background (sub-pixel antialiasing on Linux, font smoothing on macOS). Lines, fills and paths
    // blend the same either way. What a view draws over the layer must not depend on state the
    // layer's painting set on the context (it is set on the bitmap's context, not the window's).
    template <typename Paint>
    void draw (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& area, uint64_t key, Paint&& paint)
    {
        Target t;
        if (!ctx || !map (ctx, area, t, key))
        {
            direct (ctx, area, std::forward<Paint> (paint));
            return;
        }
        if (!valid (t) && !build (ctx, t, area, [&] (VSTGUI::CDrawContext* off) { paint (off); }))
        {
            direct (ctx, area, std::forward<Paint> (paint));
            return;
        }
        blit (ctx, t);
    }

    // Forget the bitmap (the next draw paints it again).
    void invalidate () { bitmap = nullptr; }
    // How often the layer was painted into its bitmap / blitted / painted directly (tests, measuring).
    struct Stats
    {
        uint64_t builds = 0, blits = 0, directs = 0;
    };
    const Stats& stats () const { return counts; }
    // Off: every draw paints directly (the bitmap is never made); for measuring the difference.
    static void setEnabled (bool on);
    static bool enabled ();

private:
    // Where the area lands in device pixels: the whole-pixel rectangle that holds it, the scale from the
    // view's units to pixels and the context's transform scaled to pixels.
    struct Target
    {
        double px0 = 0, py0 = 0;  // the bitmap's top-left device pixel (whole)
        int w = 0, h = 0;         // its size in pixels
        double fx = 0, fy = 0;    // the area's sub-pixel phase in it
        double sx = 1, sy = 1;    // view units to device pixels
        VSTGUI::CGraphicsTransform toDevice; // view coordinates to device pixels
        uint64_t signature = 0;   // the key with all of the above: what the bitmap shows
    };
    // false: not drawable through a bitmap (paint directly)
    static bool map (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& area, Target& t, uint64_t key);
    // The bitmap shows t. Otherwise notes t's signature (made only once it holds for a second draw).
    bool valid (const Target& t);
    // Paints a new bitmap (false when it cannot be made, or t changed since the last draw: the caller
    // paints directly).
    bool build (VSTGUI::CDrawContext* ctx, const Target& t, const VSTGUI::CRect& area,
                const std::function<void (VSTGUI::CDrawContext*)>& paint);
    void blit (VSTGUI::CDrawContext* ctx, const Target& t);
    template <typename Paint>
    void direct (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& area, Paint&& paint)
    {
        if (!ctx)
            return;
        ++counts.directs;
        ctx->saveGlobalState ();
        ctx->setClipRect (clipped (ctx, area));
        paint (ctx);
        ctx->restoreGlobalState ();
    }
    static VSTGUI::CRect clipped (VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& area);

    VSTGUI::SharedPointer<VSTGUI::CBitmap> bitmap;
    uint64_t built = 0;   // the signature the bitmap was made for
    uint64_t seen = 0;    // the one asked for by the draw before
    bool seenOnce = false; // (seen is set)
    bool buildable = false; // seen twice in a row: worth a bitmap
    Stats counts;
};

} // namespace pk

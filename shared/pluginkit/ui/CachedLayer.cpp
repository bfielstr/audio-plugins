#include "pluginkit/ui/CachedLayer.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/coffscreencontext.h"

#include <atomic>
#include <cmath>

namespace pk {

using namespace VSTGUI;

namespace {
std::atomic<bool> gEnabled {true};
// No bitmap past this many pixels (a broken transform; displays are far smaller even at 2x on Retina).
constexpr double kMaxPixels = 4096.0 * 4096.0;
// Device coordinates this close to a whole pixel count as on it (the zoom's float error).
constexpr double kSnap = 1e-6;
} // namespace

LayerKey& LayerKey::params (ParamHost* host)
{
    if (!host)
        return add (false);
    const uint32_t n = host->table ().size ();
    add (n);
    for (uint32_t id = 0; id < n; ++id)
        add (host->norm (id));
    return *this;
}

void CachedLayer::setEnabled (bool on) { gEnabled.store (on, std::memory_order_relaxed); }
bool CachedLayer::enabled () { return gEnabled.load (std::memory_order_relaxed); }

bool CachedLayer::map (CDrawContext* ctx, const CRect& area, Target& t, uint64_t key)
{
    if (area.getWidth () <= 0 || area.getHeight () <= 0)
        return false;
    // the context's transform (the containers' offsets, the frame's zoom and centring) times the
    // backing scale: view coordinates to device pixels. Only scaling and moving: no rotation, no flip.
    const CGraphicsTransform& m = ctx->getCurrentTransform ();
    const double b = ctx->getScaleFactor ();
    if (m.m12 != 0.0 || m.m21 != 0.0 || m.m11 <= 0.0 || m.m22 <= 0.0 || !(b > 0.0))
        return false;
    t.toDevice = CGraphicsTransform (m.m11 * b, 0.0, 0.0, m.m22 * b, m.dx * b, m.dy * b);
    t.sx = t.toDevice.m11;
    t.sy = t.toDevice.m22;
    const double x0 = t.sx * area.left + t.toDevice.dx, x1 = t.sx * area.right + t.toDevice.dx;
    const double y0 = t.sy * area.top + t.toDevice.dy, y1 = t.sy * area.bottom + t.toDevice.dy;
    // the whole pixels that hold the area, and where in them it starts (kept, so the blit lands on the
    // same pixel grid as direct drawing would)
    t.px0 = std::floor (x0 + kSnap);
    t.py0 = std::floor (y0 + kSnap);
    t.w = (int)(std::ceil (x1 - kSnap) - t.px0);
    t.h = (int)(std::ceil (y1 - kSnap) - t.py0);
    t.fx = x0 - t.px0;
    t.fy = y0 - t.py0;
    // what the bitmap shows: the view's key, its size in pixels, the scale and the phase (rounded so
    // the zoom's float error does not count; where on the screen it lands does not matter)
    t.signature = LayerKey ()
                      .add (key, t.w, t.h, t.sx, t.sy)
                      .add ((int64_t)std::lround (t.fx / kSnap), (int64_t)std::lround (t.fy / kSnap))
                      .value ();
    return t.w > 0 && t.h > 0 && (double)t.w * t.h <= kMaxPixels;
}

bool CachedLayer::valid (const Target& t)
{
    if (bitmap && t.signature == built)
        return true;
    // asked for something else than the bitmap shows: made only if the draw before asked for it too
    buildable = seenOnce && seen == t.signature;
    seen = t.signature;
    seenOnce = true;
    return false;
}

bool CachedLayer::build (CDrawContext* ctx, const Target& t, const CRect& area, const std::function<void (CDrawContext*)>& paint)
{
    if (!enabled () || !buildable)
        return false;
    bitmap = nullptr;
    // one device pixel per bitmap pixel (scale factor 1): the painting's transform does the scaling
    auto off = COffscreenContext::create (CPoint (t.w, t.h), 1.0);
    if (!off || !off->getBitmap ())
        return false;
    off->beginDraw ();
    off->clearRect (CRect (0, 0, t.w, t.h));
    {
        // view coordinates to the bitmap's pixels: the device mapping moved to the bitmap's corner
        CGraphicsTransform toBitmap = t.toDevice;
        toBitmap.dx -= t.px0;
        toBitmap.dy -= t.py0;
        CDrawContext::Transform tr (*off, toBitmap);
        // the state the view would have found in the window's context, so the painting comes out the
        // same as drawn directly (the layer is clipped to its area, as the view is by its container)
        off->setDrawMode (ctx->getDrawMode ());
        off->setLineWidth (ctx->getLineWidth ());
        off->setLineStyle (ctx->getLineStyle ());
        off->setFrameColor (ctx->getFrameColor ());
        off->setFillColor (ctx->getFillColor ());
        off->setFontColor (ctx->getFontColor ());
        if (auto f = ctx->getFont ())
            off->setFont (f);
        off->setClipRect (area);
        paint (off);
    }
    off->endDraw ();
    bitmap = off->getBitmap ();
    built = t.signature;
    ++counts.builds;
    return true;
}

void CachedLayer::blit (CDrawContext* ctx, const Target& t)
{
    ++counts.blits;
    // device pixels back to the context's coordinates: drawn at whole pixels, one bitmap pixel each
    const double b = ctx->getScaleFactor ();
    const CGraphicsTransform toLocal = ctx->getCurrentTransform ().inverse () * CGraphicsTransform ().scale (1.0 / b, 1.0 / b);
    CDrawContext::Transform tr (*ctx, toLocal);
    ctx->drawBitmap (bitmap, CRect (t.px0, t.py0, t.px0 + t.w, t.py0 + t.h));
}

CRect CachedLayer::clipped (CDrawContext* ctx, const CRect& area)
{
    CRect clip;
    ctx->getClipRect (clip);
    return clip.bound (area);
}

} // namespace pk

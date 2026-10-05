#include "OrbView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace orbitr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
// the orbs are the sound sources, the one lit thing in the view: cinnabar, their trails fading out
const CColor kOrbColor = theme::kEnergyLive;
constexpr double kFine = 0.2; // Shift: a fifth of the mouse's movement

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

OrbView::OrbView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

void OrbView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const uint32_t s = m->blocks.load (std::memory_order_acquire);
    if (s == seen)
        return;
    seen = s;
    orbs = std::clamp (m->orbs.load (std::memory_order_relaxed), 0, Motion::kMaxOrbs);
    grains = m->grains.load (std::memory_order_relaxed);
    // the engine's centre now (gliding to the parameters'): the orbs are kept relative to it
    const geo::Point c = geo::centreOf ({m->distance.load (std::memory_order_relaxed), m->angle.load (std::memory_order_relaxed)});
    trailPos = (trailPos + 1) % kTrail;
    for (int k = 0; k < orbs; ++k)
    {
        x[k] = m->orbX[(size_t)k].load (std::memory_order_relaxed) - (float)c.x;
        y[k] = m->orbY[(size_t)k].load (std::memory_order_relaxed) - (float)c.y;
        grain[k] = m->orbGrain[(size_t)k].load (std::memory_order_relaxed);
        tx[k][trailPos] = x[k];
        ty[k][trailPos] = y[k];
        trailSet[k][trailPos] = true;
    }
    invalid ();
}

geo::Polar OrbView::place () const { return {host->plainValue (kDistance), host->plainValue (kAngle)}; }
double OrbView::radius () const { return host->plainValue (kRadius); }

geo::Map OrbView::map () const
{
    if (drag != geo::Grab::None)
        return held;
    const CRect all = getViewSize ();
    return geo::fit (all.left, all.top, all.right, all.bottom, place (), radius ());
}

void OrbView::paintBase (CDrawContext* ctx, const geo::Map& map)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const geo::Polar pl = place ();
    const double r = radius ();
    const geo::Point c = geo::centreOf (pl);
    auto px = [&] (double mx) { return map.px (mx); };
    auto py = [&] (double my) { return map.py (my); };
    // metre rings round the listener, out to the far side of the ball
    const double span = std::max (1.0, pl.distance + r * geo::kBallMargin);
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (theme::kGridMajor);
    for (double m = 1.0; m <= span + 0.5; m += (span > 8.0 ? 5.0 : 1.0))
        ctx->drawEllipse (CRect (px (-m), py (m), px (m), py (-m)), kDrawStroked);
    const bool ballHeld = drag == geo::Grab::Ball, ballLit = ballHeld || (drag == geo::Grab::None && hover == geo::Grab::Ball);
    const bool youHeld = drag == geo::Grab::Listener, youLit = youHeld || (drag == geo::Grab::None && hover == geo::Grab::Listener);
    const double cx = px (c.x), cy = py (c.y), lx = map.ox, ly = map.oy;
    // pointed at or held: a dim line from the listener to the centre
    if (ballLit || youLit)
    {
        ctx->setFrameColor (theme::kGridZero);
        ctx->drawLine (CPoint (lx, ly), CPoint (cx, cy));
    }
    // the swarm's ball: a dashed copper contour; pointed at, pale copper and heavier with its centre's
    // handle; held, cinnabar
    ctx->setFrameColor (ballHeld ? theme::kEnergyLive : ballLit ? theme::kCopperPale : theme::kCopper);
    ctx->setLineWidth (ballLit ? 1.5 : 1.0);
    ctx->setLineStyle (theme::dashed ());
    ctx->drawEllipse (CRect (px (c.x - r), py (c.y + r), px (c.x + r), py (c.y - r)), kDrawStroked);
    ctx->setLineStyle (kLineSolid);
    ctx->setLineWidth (1.0);
    if (ballLit)
        pk::draw::handle (ctx, CPoint (cx, cy), 4.0, ballHeld);
    // the listener (a head with ears, facing up), drawn as an outline in the text colour; pointed at, a
    // pale copper ring round it; held, cinnabar
    ctx->setFrameColor (youHeld ? theme::kEnergyLive : theme::kText);
    ctx->drawEllipse (CRect (lx - 6, ly - 6, lx + 6, ly + 6), kDrawStroked);
    ctx->drawLine (CPoint (lx - 9, ly - 2), CPoint (lx - 9, ly + 2));
    ctx->drawLine (CPoint (lx + 9, ly - 2), CPoint (lx + 9, ly + 2));
    ctx->drawLine (CPoint (lx, ly - 6), CPoint (lx, ly - 11));
    if (youLit)
    {
        ctx->setFrameColor (youHeld ? theme::kEnergyLive : theme::kCopperPale);
        ctx->drawEllipse (CRect (lx - geo::kListenerGrab, ly - geo::kListenerGrab, lx + geo::kListenerGrab, ly + geo::kListenerGrab),
                          kDrawStroked);
    }
}

void OrbView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const geo::Map mp = map ();
    const geo::Polar pl = place ();
    const pk::LayerKey key = pk::LayerKey ()
                                 .add (mp.ox)
                                 .add (mp.oy)
                                 .add (mp.scale)
                                 .add (pl.distance)
                                 .add (pl.angle)
                                 .add (radius ())
                                 .add ((int)hover)
                                 .add ((int)drag);
    baseLayer.draw (ctx, all, key, [this, mp] (CDrawContext* c) { paintBase (c, mp); });
    ctx->setClipRect (all);
    // the orbs round the centre the parameters set
    const geo::Point c = geo::centreOf (pl);
    auto px = [&] (double mx) { return mp.px (c.x + mx); };
    auto py = [&] (double my) { return mp.py (c.y + my); };
    ctx->setLineWidth (1.0);
    ctx->setLineStyle (kLineSolid); // (as the ball's dashes left it)
    for (int k = 0; k < orbs; ++k)
    {
        // the trail, fading
        for (int t = 1; t < kTrail; ++t)
        {
            const int i0 = (trailPos - t + kTrail) % kTrail, i1 = (trailPos - t + 1 + kTrail) % kTrail;
            if (!trailSet[k][i0] || !trailSet[k][i1])
                continue;
            CColor tc = kOrbColor;
            tc.alpha = (uint8_t)(160 * (kTrail - t) / kTrail);
            ctx->setFrameColor (tc);
            ctx->setLineWidth (1.5);
            ctx->drawLine (CPoint (px (tx[k][i0]), py (ty[k][i0])), CPoint (px (tx[k][i1]), py (ty[k][i1])));
        }
        ctx->setLineWidth (1.0);
        ctx->setFillColor (kOrbColor);
        const double ox = px (x[k]), oy = py (y[k]);
        if (grains)
        {
            // a grain cloud: a faint ring, the orb swelling with its newest grain
            const double g = 2.5 + 3.5 * std::clamp ((double)grain[k], 0.0, 1.0);
            CColor rc = kOrbColor;
            rc.alpha = 110;
            ctx->setFrameColor (rc);
            ctx->drawEllipse (CRect (ox - 7, oy - 7, ox + 7, oy + 7), kDrawStroked);
            ctx->drawEllipse (CRect (ox - g, oy - g, ox + g, oy + g), kDrawFilled);
        }
        else
            ctx->drawEllipse (CRect (ox - 4, oy - 4, ox + 4, oy + 4), kDrawFilled);
    }
    char buf[128];
    if (grains)
        std::snprintf (buf, sizeof (buf), "ORBS FROM ABOVE  %d, %s, %.0f m/s, grains of %.0f ms", orbs,
                       host->plainValue (kPattern) >= 0.5 ? "swarm" : "orbit", host->plainValue (kSpeed), host->plainValue (kGrainSize));
    else
        std::snprintf (buf, sizeof (buf), "ORBS FROM ABOVE  %d, %s, %.0f m/s", orbs, host->plainValue (kPattern) >= 0.5 ? "swarm" : "orbit",
                       host->plainValue (kSpeed));
    text (ctx, buf, CRect (all.left + 6, all.top + 3, all.right - 6, all.top + 17), theme::kCopperPale, 10.0, kLeftText, true);
    // the swarm's place, right of the title (under it in a narrow view); in the text colour while the
    // ball or the listener is pointed at or held
    {
        char where[64];
        const double a = std::fabs (pl.angle) < 0.5 ? 0.0 : pl.angle; // (no "-0")
        std::snprintf (where, sizeof (where), "Distance %.1f m, Angle %.0f\xC2\xB0", pl.distance, a);
        const double row = all.getWidth () < 560.0 ? 15.0 : 0.0;
        const bool lit = drag != geo::Grab::None || hover != geo::Grab::None;
        text (ctx, where, CRect (all.left + 6, all.top + 3 + row, all.right - 6, all.top + 17 + row), lit ? theme::kText : theme::kTextDim,
              10.0, kRightText);
    }
    text (ctx, "you", CRect (mp.ox + 12, mp.oy - 6, mp.ox + 60, mp.oy + 8), theme::kTextDim, 9.0);
    ctx->resetClipRect ();
    setDirty (false);
}

// ---- dragging

void OrbView::setHover (geo::Grab g)
{
    if (auto* f = getFrame ())
        f->setCursor (g != geo::Grab::None ? kCursorSizeAll : kCursorDefault);
    if (g == hover)
        return;
    hover = g;
    invalid ();
}

void OrbView::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    const geo::Map mp = map ();
    const geo::Grab g = geo::grabAt (mp, place (), radius (), e.mousePosition.x, e.mousePosition.y);
    if (g == geo::Grab::None)
        return;
    e.consumed = true;
    if (e.clickCount == 2)
    {
        // double-click: Distance and Angle back to their defaults
        host->setOnce (kDistance, host->table ().defaultNormalized (kDistance));
        host->setOnce (kAngle, host->table ().defaultNormalized (kAngle));
        e.ignoreFollowUpMoveAndUpEvents (true);
        invalid ();
        return;
    }
    held = mp;
    dragStart = place ();
    last = e.mousePosition;
    dragX = dragY = 0.0;
    drag = g;
    hover = g;
    host->beginEdit (kDistance);
    host->beginEdit (kAngle);
    invalid ();
}

void OrbView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == geo::Grab::None)
    {
        setHover (geo::grabAt (map (), place (), radius (), e.mousePosition.x, e.mousePosition.y));
        return;
    }
    const double fine = e.modifiers.has (ModifierKey::Shift) ? kFine : 1.0;
    dragX += (e.mousePosition.x - last.x) * fine;
    dragY += (e.mousePosition.y - last.y) * fine;
    last = e.mousePosition;
    const geo::Polar p = geo::dragTo (held, drag, dragStart, dragX, dragY);
    host->setNorm (kDistance, host->table ().toNormalized (kDistance, p.distance));
    host->setNorm (kAngle, host->table ().toNormalized (kAngle, p.angle));
    invalid ();
    e.consumed = true;
}

void OrbView::endDrag ()
{
    if (drag == geo::Grab::None)
        return;
    host->endEdit (kDistance);
    host->endEdit (kAngle);
    drag = geo::Grab::None; // (the view fits the listener and the ball again)
    invalid ();
}

void OrbView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag == geo::Grab::None)
        return;
    endDrag ();
    setHover (geo::grabAt (map (), place (), radius (), e.mousePosition.x, e.mousePosition.y));
    e.consumed = true;
}

void OrbView::onMouseCancelEvent (MouseCancelEvent& e)
{
    endDrag ();
    e.consumed = true;
}

void OrbView::onMouseExitEvent (MouseExitEvent& e)
{
    if (drag == geo::Grab::None)
        setHover (geo::Grab::None);
    e.consumed = true;
}

void OrbView::onMouseWheelEvent (MouseWheelEvent& e)
{
    // over the ball or the listener: Distance (wheel up, farther)
    if (drag != geo::Grab::None || geo::grabAt (map (), place (), radius (), e.mousePosition.x, e.mousePosition.y) == geo::Grab::None)
        return;
    const double dn = pk::wheelStep (e, host->table (), kDistance);
    if (dn == 0.0)
        return;
    host->setOnce (kDistance, std::clamp (host->norm (kDistance) + dn, 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

} // namespace orbitr

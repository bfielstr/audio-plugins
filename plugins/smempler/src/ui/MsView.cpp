#include "MsView.h"

#include "../core/MsEq.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace smempler {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
const CColor kMidColor (110, 165, 255), kSideColor (255, 164, 40);

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
           CHoriTxtAlign a = kCenterText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

MsView::MsView (const CRect& r, pk::ParamHost* h, LevelSource l) : CView (r), host (h), levels (std::move (l)) {}

CRect MsView::plot () const
{
    CRect r = getViewSize ();
    r.right -= kMeterW;
    r.bottom -= 16;
    return r;
}

double MsView::xOfHz (double hz) const
{
    const CRect r = plot ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double MsView::yOfDb (double db) const
{
    const CRect r = plot ();
    return r.top + (kMaxDb - std::clamp (db, kMinDb, kMaxDb)) / (kMaxDb - kMinDb) * r.getHeight ();
}

CPoint MsView::handle () const { return CPoint (xOfHz (host->plainValue (kMsSideHp)), yOfDb (host->plainValue (kMsSideGain))); }

void MsView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize (), pr = plot ();
    const bool on = host->plainValue (kMsOn) >= 0.5;
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);

    // grid
    ctx->setLineWidth (1.0);
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
        ctx->setFrameColor (major ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (xOfHz (f), pr.top), CPoint (xOfHz (f), pr.bottom));
        char buf[16];
        std::snprintf (buf, sizeof (buf), f >= 1000 ? "%.0fk" : "%.0f", f >= 1000 ? f / 1000 : f);
        text (ctx, buf, CRect (xOfHz (f) - 20, pr.bottom + 1, xOfHz (f) + 20, all.bottom - 2), theme::kTextDim, 9.5);
    }
    for (double db : {-24.0, -12.0, 0.0, 12.0})
    {
        ctx->setFrameColor (db == 0.0 ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (pr.left, yOfDb (db)), CPoint (pr.right, yOfDb (db)));
    }

    // mid: flat at its gain; side: the high-pass plus its gain
    const uint8_t alpha = on ? 255 : 110;
    const double midDb = host->plainValue (kMsMidGain), sideDb = host->plainValue (kMsSideGain);
    const double hz = host->plainValue (kMsSideHp);
    const int slope = (int)std::lround (host->plainValue (kMsSlope));
    ctx->setLineWidth (2.0);
    ctx->setFrameColor (CColor (kMidColor.red, kMidColor.green, kMidColor.blue, alpha));
    ctx->drawLine (CPoint (pr.left, yOfDb (midDb)), CPoint (pr.right, yOfDb (midDb)));
    if (auto path = owned (ctx->createGraphicsPath ()))
    {
        const int steps = 160;
        path->beginSubpath (CPoint (pr.left, pr.bottom));
        for (int i = 0; i <= steps; ++i)
        {
            const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / steps);
            path->addLine (CPoint (xOfHz (f), yOfDb (sideDb + MsEq::responseDb (f, hz, slope))));
        }
        path->addLine (CPoint (pr.right, pr.bottom));
        path->closeSubpath ();
        ctx->setFillColor (CColor (kSideColor.red, kSideColor.green, kSideColor.blue, (uint8_t)(on ? 40 : 16)));
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        ctx->setLineWidth (1.8);
        ctx->setFrameColor (CColor (kSideColor.red, kSideColor.green, kSideColor.blue, alpha));
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }
    const CPoint h = handle ();
    ctx->setFillColor (CColor (kSideColor.red, kSideColor.green, kSideColor.blue, alpha));
    ctx->drawEllipse (CRect (h.x - 6, h.y - 6, h.x + 6, h.y + 6), kDrawFilled);
    ctx->setLineWidth (1.5);
    ctx->setFrameColor (theme::kTextBright);
    ctx->drawEllipse (CRect (h.x - 6, h.y - 6, h.x + 6, h.y + 6), kDrawStroked);

    // live levels: mid and side peaks (dBFS, -60 .. 0)
    const CRect meters (pr.right + 6, pr.top + 16, all.right - 4, pr.bottom);
    auto bar = [&] (int k, float level, const CColor& c, const char* name) {
        const double w = (meters.getWidth () - 4) / 2;
        const CRect slot (meters.left + k * (w + 4), meters.top, meters.left + k * (w + 4) + w, meters.bottom);
        ctx->setFillColor (theme::kControlBg);
        ctx->drawRect (slot, kDrawFilled);
        const double db = level > 1e-6f ? 20.0 * std::log10 (level) : -60.0;
        const double frac = std::clamp ((db + 60.0) / 60.0, 0.0, 1.0);
        ctx->setFillColor (c);
        ctx->drawRect (CRect (slot.left, slot.bottom - slot.getHeight () * frac, slot.right, slot.bottom), kDrawFilled);
        text (ctx, name, CRect (slot.left - 2, pr.top, slot.right + 2, pr.top + 14), theme::kTextDim, 9.0);
    };
    bar (0, shownMid, kMidColor, "M");
    bar (1, shownSide, kSideColor, "S");

    char buf[96];
    std::snprintf (buf, sizeof (buf), "%s   Side HP %s  Side %s  Mid %s", on ? "MID / SIDE" : "MID / SIDE (off)",
                   host->valueText (kMsSideHp).c_str (), host->valueText (kMsSideGain).c_str (), host->valueText (kMsMidGain).c_str ());
    text (ctx, buf, CRect (pr.left + 6, pr.top + 4, pr.right - 6, pr.top + 18), on ? theme::kTextBright : theme::kTextDim, 10.0,
          kLeftText, true);
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

void MsView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if ((!e.buttonState.isLeft () && !right) || !plot ().pointInside (e.mousePosition))
        return;
    if (e.clickCount == 2 || right)
    {
        host->setOnce (kMsSideHp, host->table ().defaultNormalized (kMsSideHp));
        host->setOnce (kMsSideGain, host->table ().defaultNormalized (kMsSideGain));
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    dragging = true;
    down = e.mousePosition;
    startHz = host->plainValue (kMsSideHp);
    startDb = host->plainValue (kMsSideGain);
    host->beginEdit (kMsSideHp);
    host->beginEdit (kMsSideGain);
    e.consumed = true;
}

void MsView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (!dragging)
        return;
    const CRect pr = plot ();
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - down.x) * fine, dy = (e.mousePosition.y - down.y) * fine;
    host->setNorm (kMsSideHp, host->table ().toNormalized (kMsSideHp, startHz * std::pow (kMaxHz / kMinHz, dx / pr.getWidth ())));
    host->setNorm (kMsSideGain,
                   host->table ().toNormalized (kMsSideGain, startDb - dy * (kMaxDb - kMinDb) / pr.getHeight ()));
    invalid ();
    e.consumed = true;
}

void MsView::onMouseWheelEvent (MouseWheelEvent& e)
{
    const CPoint h = handle ();
    const bool over = std::hypot (e.mousePosition.x - h.x, e.mousePosition.y - h.y) <= 14.0;
    if (!dragging && !(over && e.modifiers.has (ModifierKey::Shift)))
        return;
    const double dn = pk::wheelStep (e, host->table (), kMsSlope);
    if (dn == 0.0)
        return;
    host->setOnce (kMsSlope, std::clamp (host->norm (kMsSlope) + dn, 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

void MsView::onMouseUpEvent (MouseUpEvent& e)
{
    if (!dragging)
        return;
    dragging = false;
    host->endEdit (kMsSideHp);
    host->endEdit (kMsSideGain);
    e.consumed = true;
}

void MsView::idle ()
{
    if (!levels)
        return;
    float m = 0.0f, s = 0.0f;
    levels (m, s);
    auto ease = [] (float& v, float t) { v += (t - v) * (t > v ? 0.7f : 0.15f); };
    ease (shownMid, m);
    ease (shownSide, s);
    invalid ();
}

} // namespace smempler

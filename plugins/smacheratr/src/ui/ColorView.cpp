#include "ColorView.h"

#include "Color.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace smacheratr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
           CHoriTxtAlign a = kCenterText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
constexpr double kHandleRadius = 5.0;
} // namespace

ColorView::ColorView (const CRect& r, pk::ParamHost* h, Controller* c) : CView (r), host (h), controller (c) {}

double ColorView::sampleRate () const
{
    if (SharedMeters* s = controller->getShared ())
        return s->sampleRate.load (std::memory_order_relaxed);
    return 48000.0;
}

double ColorView::xOfHz (double hz) const
{
    const CRect r = getViewSize ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double ColorView::yOfDb (double db) const
{
    const CRect r = getViewSize ();
    return r.getCenter ().y - std::clamp (db, -kMaxDb, kMaxDb) / kMaxDb * (r.getHeight () * 0.5 - 12.0);
}

CPoint ColorView::loHandle () const { return CPoint (xOfHz (kColorLowHz * 0.5), yOfDb (colorDb (host->plainValue (kColorLo)))); }

CPoint ColorView::hiHandle () const
{
    return CPoint (xOfHz (host->plainValue (kColorFreq)), yOfDb (colorDb (host->plainValue (kColorHi))));
}

void ColorView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const bool on = host->plainValue (kColorOn) >= 0.5;
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);

    // grid
    ctx->setLineWidth (1.0);
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
        ctx->setFrameColor (major ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (xOfHz (f), all.top), CPoint (xOfHz (f), all.bottom));
        if (major)
        {
            char buf[16];
            std::snprintf (buf, sizeof (buf), f >= 1000 ? "%.0fk" : "%.0f", f >= 1000 ? f / 1000 : f);
            text (ctx, buf, CRect (xOfHz (f) - 20, all.bottom - 14, xOfHz (f) + 20, all.bottom - 2), theme::kTextDim, 9.5);
        }
    }
    for (double db : {-12.0, 0.0, 12.0})
    {
        ctx->setFrameColor (db == 0.0 ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (all.left, yOfDb (db)), CPoint (all.right, yOfDb (db)));
    }

    // response
    const double sr = sampleRate ();
    const double lo = host->plainValue (kColorLo), hi = host->plainValue (kColorHi);
    const double freq = host->plainValue (kColorFreq), width = host->plainValue (kColorWidth);
    const int steps = 160;
    auto path = owned (ctx->createGraphicsPath ());
    if (path)
    {
        path->beginSubpath (CPoint (all.left, yOfDb (0.0)));
        for (int i = 0; i <= steps; ++i)
        {
            const double hz = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / steps);
            path->addLine (CPoint (xOfHz (hz), yOfDb (colorResponseDb (hz, sr, lo, hi, freq, width))));
        }
        path->addLine (CPoint (all.right, yOfDb (0.0)));
        path->closeSubpath ();
        ctx->setFillColor (on ? CColor (255, 164, 40, 40) : CColor (120, 120, 120, 20));
        ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        ctx->setLineWidth (1.6);
        ctx->setFrameColor (on ? theme::kCurve : theme::kTextDim);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }

    // handles
    for (const CPoint& h : {loHandle (), hiHandle ()})
    {
        const CRect hr (h.x - kHandleRadius, h.y - kHandleRadius, h.x + kHandleRadius, h.y + kHandleRadius);
        ctx->setFillColor (on ? theme::kTextBright : theme::kTextDim);
        ctx->drawEllipse (hr, kDrawFilled);
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (on ? theme::kAccent : theme::kKnobTrack);
        ctx->drawEllipse (hr, kDrawStroked);
    }

    // labels
    text (ctx, on ? "COLOR" : "COLOR  (off)", CRect (all.left + 6, all.top + 4, all.right - 6, all.top + 18),
          on ? theme::kTextBright : theme::kTextDim, 10.5, kLeftText, true);
    char buf[96];
    std::snprintf (buf, sizeof (buf), "Lo %s   Hi %s @ %s", host->valueText (kColorLo).c_str (),
                   host->valueText (kColorHi).c_str (), host->valueText (kColorFreq).c_str ());
    text (ctx, buf, CRect (all.left + 6, all.top + 19, all.right - 6, all.top + 32), theme::kTextDim, 9.5, kLeftText);
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

ColorView::Drag ColorView::hit (const CPoint& p) const
{
    auto near = [&] (const CPoint& h) { return std::hypot (p.x - h.x, p.y - h.y) <= kHandleRadius + 4.0; };
    if (near (hiHandle ()))
        return Drag::Hi;
    if (near (loHandle ()))
        return Drag::Lo;
    return Drag::None;
}

void ColorView::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    drag = hit (e.mousePosition);
    if (drag == Drag::None)
        return;
    if (e.clickCount == 2)
    {
        if (drag == Drag::Lo)
            host->setOnce (kColorLo, host->table ().defaultNormalized (kColorLo));
        else
        {
            host->setOnce (kColorHi, host->table ().defaultNormalized (kColorHi));
            host->setOnce (kColorFreq, host->table ().defaultNormalized (kColorFreq));
        }
        drag = Drag::None;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    down = e.mousePosition;
    startLo = host->plainValue (kColorLo);
    startHi = host->plainValue (kColorHi);
    startFreq = host->plainValue (kColorFreq);
    movedH = movedV = false;
    if (drag == Drag::Lo)
        host->beginEdit (kColorLo);
    else
    {
        host->beginEdit (kColorHi);
        host->beginEdit (kColorFreq);
    }
    e.consumed = true;
}

void ColorView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == Drag::None)
    {
        const Drag h = hit (e.mousePosition);
        if (auto* f = getFrame ())
            f->setCursor (h == Drag::Hi ? kCursorSizeAll : h == Drag::Lo ? kCursorVSize : kCursorDefault);
        return;
    }
    const CRect r = getViewSize ();
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - down.x) * fine, dy = (e.mousePosition.y - down.y) * fine;
    const double amountPerPixel = 2.0 / (r.getHeight () - 24.0); // +-100 % over the plot
    auto setPlain = [this] (uint32_t id, double v) { host->setNorm (id, host->table ().toNormalized (id, v)); };
    if (drag == Drag::Lo)
        setPlain (kColorLo, startLo - dy * amountPerPixel);
    else
    {
        // the dominant direction decides: up/down sets the amount, sideways the frequency
        if (!movedH && !movedV)
        {
            if (std::fabs (dx) > 4 && std::fabs (dx) > std::fabs (dy))
                movedH = true;
            else if (std::fabs (dy) > 4)
                movedV = true;
        }
        if (movedH)
            setPlain (kColorFreq, startFreq * std::pow (kMaxHz / kMinHz, dx / r.getWidth ()));
        else if (movedV)
            setPlain (kColorHi, startHi - dy * amountPerPixel);
    }
    invalid ();
    e.consumed = true;
}

void ColorView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag == Drag::None)
        return;
    if (drag == Drag::Lo)
        host->endEdit (kColorLo);
    else
    {
        host->endEdit (kColorHi);
        host->endEdit (kColorFreq);
    }
    drag = Drag::None;
    e.consumed = true;
}

void ColorView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

} // namespace smacheratr

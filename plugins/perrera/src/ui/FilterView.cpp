#include "FilterView.h"

#include "Engine.h"
#include "Svf.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace perrera {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
const CColor kHpColor (235, 80, 80), kLpColor (70, 200, 130);
constexpr double kHandleRadius = 6.0;

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
           CHoriTxtAlign a = kCenterText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

std::string noteName (int note)
{
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char buf[16];
    std::snprintf (buf, sizeof (buf), "%s%d", names[note % 12], note / 12 - 2);
    return buf;
}
} // namespace

FilterView::FilterView (const CRect& r, pk::ParamHost* h, Controller* c) : CView (r), host (h), controller (c)
{
    shownHp = (float)host->plainValue (kHpFreq);
    shownLp = (float)host->plainValue (kLpFreq);
}

double FilterView::xOfHz (double hz) const
{
    const CRect r = getViewSize ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double FilterView::hzOfX (double x) const
{
    const CRect r = getViewSize ();
    return kMinHz * std::pow (kMaxHz / kMinHz, std::clamp ((x - r.left) / r.getWidth (), 0.0, 1.0));
}

double FilterView::yOfDb (double db) const
{
    const CRect r = getViewSize ();
    return r.top + (kMaxDb - std::clamp (db, kMinDb, kMaxDb)) / (kMaxDb - kMinDb) * (r.getHeight () - 16.0);
}

void FilterView::cutoffs (double& hp, double& lp) const
{
    // the split parameter always shows; tracking and the envelope come from the processor's meters
    hp = shownHp > 0.0f ? shownHp : hpCutoff (host->plainValue (kHpFreq), 0.0, host->plainValue (kSplit));
    lp = shownLp > 0.0f ? shownLp : lpCutoff (host->plainValue (kLpFreq), 0.0, host->plainValue (kSplit));
}

CPoint FilterView::hpHandle () const
{
    double hp, lp;
    cutoffs (hp, lp);
    return CPoint (xOfHz (hp), yOfDb (-3.0 + 12.0 * host->plainValue (kHpRes)));
}

CPoint FilterView::lpHandle () const
{
    double hp, lp;
    cutoffs (hp, lp);
    return CPoint (xOfHz (lp), yOfDb (-3.0 + 12.0 * host->plainValue (kLpRes)));
}

void FilterView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);

    // grid
    ctx->setLineWidth (1.0);
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
        ctx->setFrameColor (major ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (xOfHz (f), all.top), CPoint (xOfHz (f), all.bottom - 16));
        char buf[16];
        std::snprintf (buf, sizeof (buf), f >= 1000 ? "%.0fk" : "%.0f", f >= 1000 ? f / 1000 : f);
        text (ctx, buf, CRect (xOfHz (f) - 20, all.bottom - 15, xOfHz (f) + 20, all.bottom - 2), theme::kTextDim, 9.5);
    }
    for (double db : {-24.0, -12.0, 0.0, 12.0})
    {
        ctx->setFrameColor (db == 0.0 ? CColor (58, 58, 64) : theme::kGrid);
        ctx->drawLine (CPoint (all.left, yOfDb (db)), CPoint (all.right, yOfDb (db)));
        char buf[8];
        std::snprintf (buf, sizeof (buf), "%.0f", db);
        text (ctx, buf, CRect (all.left + 2, yOfDb (db) - 12, all.left + 30, yOfDb (db)), theme::kTextDim, 9.0, kLeftText);
    }

    // responses
    double hp, lp;
    cutoffs (hp, lp);
    const bool slope24 = std::lround (host->plainValue (kSlope)) == kSlope24;
    const double qHp = resonanceToQ (host->plainValue (kHpRes), slope24), qLp = resonanceToQ (host->plainValue (kLpRes), slope24);
    const int steps = 200;
    auto curve = [&] (int which, const CColor& stroke, const CColor* fill, double width) {
        auto path = owned (ctx->createGraphicsPath ());
        if (!path)
            return;
        const double base = yOfDb (kMinDb);
        for (int i = 0; i <= steps; ++i)
        {
            const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / steps);
            std::complex<double> a = highPassResponse (f, hp, qHp), b = lowPassResponse (f, lp, qLp);
            if (slope24)
            {
                a *= a;
                b *= b;
            }
            // the sum uses the polarity the engine uses (inverted high-pass at 12 dB)
            const std::complex<double> h = which == 0 ? a : (which == 1 ? b : (slope24 ? a + b : b - a));
            const double db = 20.0 * std::log10 (std::max (1e-6, std::abs (h)));
            const CPoint pt (xOfHz (f), yOfDb (db));
            if (i == 0)
                path->beginSubpath (fill ? CPoint (pt.x, base) : pt);
            if (fill && i == 0)
                path->addLine (pt);
            else
                path->addLine (pt);
        }
        if (fill)
        {
            path->addLine (CPoint (all.right, base));
            path->closeSubpath ();
            ctx->setFillColor (*fill);
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        }
        ctx->setLineWidth (width);
        ctx->setFrameColor (stroke);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    };
    const CColor hpFill (235, 80, 80, 45), lpFill (70, 200, 130, 30);
    curve (0, kHpColor, &hpFill, 1.4);
    curve (1, kLpColor, &lpFill, 1.4);
    curve (2, theme::kTextBright, nullptr, 2.2);

    // handles
    for (int k = 0; k < 2; ++k)
    {
        const CPoint h = k == 0 ? hpHandle () : lpHandle ();
        const CRect hr (h.x - kHandleRadius, h.y - kHandleRadius, h.x + kHandleRadius, h.y + kHandleRadius);
        ctx->setFillColor (k == 0 ? kHpColor : kLpColor);
        ctx->drawEllipse (hr, kDrawFilled);
        ctx->setLineWidth (1.5);
        ctx->setFrameColor (theme::kTextBright);
        ctx->drawEllipse (hr, kDrawStroked);
    }

    // labels
    char buf[96];
    std::snprintf (buf, sizeof (buf), "HP %s   LP %s   Split %s", host->valueText (kHpFreq).c_str (),
                   host->valueText (kLpFreq).c_str (), host->valueText (kSplit).c_str ());
    text (ctx, buf, CRect (all.left + 36, all.top + 4, all.right - 6, all.top + 18), theme::kTextBright, 10.5, kLeftText, true);
    if (shownNote >= 0)
    {
        std::snprintf (buf, sizeof (buf), "tracking %s   now HP %.0f Hz / LP %.0f Hz   env %.0f %%", noteName (shownNote).c_str (),
                       hp, lp, 100.0 * shownEnv);
        text (ctx, buf, CRect (all.left + 36, all.top + 19, all.right - 6, all.top + 32), theme::kTextDim, 9.5, kLeftText);
    }
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

FilterView::Drag FilterView::hit (const CPoint& p) const
{
    auto near = [&] (const CPoint& h) { return std::hypot (p.x - h.x, p.y - h.y) <= kHandleRadius + 4.0; };
    if (near (hpHandle ()))
        return Drag::Hp;
    if (near (lpHandle ()))
        return Drag::Lp;
    return Drag::None;
}

void FilterView::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    drag = hit (e.mousePosition);
    if (drag == Drag::None)
        return;
    const uint32_t fId = drag == Drag::Hp ? kHpFreq : kLpFreq, rId = drag == Drag::Hp ? kHpRes : kLpRes;
    if (e.clickCount == 2)
    {
        host->setOnce (fId, host->table ().defaultNormalized (fId));
        host->setOnce (rId, host->table ().defaultNormalized (rId));
        drag = Drag::None;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    down = e.mousePosition;
    startFreq = host->plainValue (fId);
    startRes = host->plainValue (rId);
    host->beginEdit (fId);
    host->beginEdit (rId);
    e.consumed = true;
}

void FilterView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == Drag::None)
    {
        if (auto* f = getFrame ())
            f->setCursor (hit (e.mousePosition) != Drag::None ? kCursorSizeAll : kCursorDefault);
        return;
    }
    const CRect r = getViewSize ();
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - down.x) * fine, dy = (e.mousePosition.y - down.y) * fine;
    const uint32_t fId = drag == Drag::Hp ? kHpFreq : kLpFreq, rId = drag == Drag::Hp ? kHpRes : kLpRes;
    host->setNorm (fId, host->table ().toNormalized (fId, startFreq * std::pow (kMaxHz / kMinHz, dx / r.getWidth ())));
    host->setNorm (rId, std::clamp (startRes - dy / 150.0, 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

void FilterView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag == Drag::None)
        return;
    host->endEdit (drag == Drag::Hp ? kHpFreq : kLpFreq);
    host->endEdit (drag == Drag::Hp ? kHpRes : kLpRes);
    drag = Drag::None;
    e.consumed = true;
}

void FilterView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

void FilterView::idle ()
{
    SharedMeters* s = controller->getShared ();
    if (!s)
        return;
    const float hp = s->meters.hpHz.load (std::memory_order_relaxed), lp = s->meters.lpHz.load (std::memory_order_relaxed);
    const float env = s->meters.env.load (std::memory_order_relaxed);
    const int note = s->meters.note.load (std::memory_order_relaxed);
    if (std::fabs (hp - shownHp) > 0.5f || std::fabs (lp - shownLp) > 0.5f || std::fabs (env - shownEnv) > 0.01f || note != shownNote)
    {
        shownHp = hp;
        shownLp = lp;
        shownEnv = env;
        shownNote = note;
        invalid ();
    }
}

} // namespace perrera

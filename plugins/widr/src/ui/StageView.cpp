#include "StageView.h"

#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace widr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
const CColor kOther (150, 152, 158), kYield (110, 165, 255);
const char* kRoleNames[kNumRoles] = {"Anchor", "Support", "Wide", "Ambient"};
constexpr double kDeg = M_PI / 180.0;

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
           CHoriTxtAlign a = kCenterText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

StageView::StageView (const CRect& r, pk::ParamHost* h, Controller* c) : CView (r), host (h), controller (c)
{
    gains.fill (1.0f);
    yields.fill (1.0f);
}

CRect StageView::field () const
{
    CRect r = getViewSize ();
    r.bottom -= kStrip + 6.0;
    return r;
}

CRect StageView::strip () const
{
    const CRect all = getViewSize ();
    return CRect (all.left, all.bottom - kStrip, all.right, all.bottom);
}

CPoint StageView::listener () const
{
    const CRect f = field ();
    return CPoint (f.getCenter ().x, f.bottom - 14.0);
}

double StageView::radiusFor (double space) const
{
    const double rMin = 46.0, rMax = field ().getHeight () - 34.0;
    return rMin + std::clamp (space, 0.0, 1.0) * (rMax - rMin);
}

// 100 % spans the speakers (+-30 degrees); beyond, the image goes past them (+-80 at 200 %).
double StageView::angleFor (double width)
{
    width = std::clamp (width, 0.0, 2.0);
    return std::max (1.5, width <= 1.0 ? 30.0 * width : 30.0 + 50.0 * (width - 1.0));
}

double StageView::widthFor (double a)
{
    a = std::clamp (a, 0.0, 80.0);
    return a <= 30.0 ? a / 30.0 : 1.0 + (a - 30.0) / 50.0;
}

CPoint StageView::arcEnd (bool right) const
{
    const CPoint c = listener ();
    const double a = angleFor (host->plainValue (kWidth)) * kDeg, r = radiusFor (host->plainValue (kSpace));
    return CPoint (c.x + (right ? 1.0 : -1.0) * r * std::sin (a), c.y - r * std::cos (a));
}

void StageView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const CRect f = field ();
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);
    const CPoint c = listener ();

    // rings, the speaker lines and the speakers
    ctx->setLineWidth (1.0);
    for (double s : {0.0, 0.5, 1.0})
    {
        auto ring = owned (ctx->createGraphicsPath ());
        const double r = radiusFor (s);
        for (int i = 0; i <= 64; ++i)
        {
            const double a = (-90.0 + 180.0 * i / 64.0) * kDeg;
            const CPoint pt (c.x + r * std::sin (a), c.y - r * std::cos (a));
            if (i == 0)
                ring->beginSubpath (pt);
            else
                ring->addLine (pt);
        }
        ctx->setFrameColor (theme::kGrid);
        ctx->drawGraphicsPath (ring, CDrawContext::kPathStroked);
    }
    const double rSpk = radiusFor (0.55);
    for (int side : {-1, 1})
    {
        const CPoint spk (c.x + side * rSpk * std::sin (30.0 * kDeg), c.y - rSpk * std::cos (30.0 * kDeg));
        ctx->setFrameColor (CColor (58, 58, 64));
        ctx->drawLine (c, spk);
        ctx->setFillColor (CColor (70, 70, 76));
        ctx->drawRect (CRect (spk.x - 7, spk.y - 7, spk.x + 7, spk.y + 7), kDrawFilled);
        ctx->setFillColor (theme::kWaveBg);
        ctx->drawEllipse (CRect (spk.x - 4, spk.y - 4, spk.x + 4, spk.y + 4), kDrawFilled);
    }
    ctx->setFillColor (theme::kTextDim);
    ctx->drawEllipse (CRect (c.x - 5, c.y - 5, c.x + 5, c.y + 5), kDrawFilled);

    // the arcs: the others first, this one on top
    auto arc = [&] (double width, double space, const CColor& col, double lineWidth) {
        auto path = owned (ctx->createGraphicsPath ());
        const double a = angleFor (width), r = radiusFor (space);
        for (int i = 0; i <= 48; ++i)
        {
            const double t = (-a + 2.0 * a * i / 48.0) * kDeg;
            const CPoint pt (c.x + r * std::sin (t), c.y - r * std::cos (t));
            if (i == 0)
                path->beginSubpath (pt);
            else
                path->addLine (pt);
        }
        ctx->setLineWidth (lineWidth);
        ctx->setFrameColor (col);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        const double t = a * kDeg;
        return CPoint (c.x + r * std::sin (t), c.y - r * std::cos (t));
    };
    auto label = [&] (const Member& m) {
        char buf[48];
        std::snprintf (buf, sizeof (buf), "Widr %d \xC2\xB7 %s", m.number, kRoleNames[std::clamp (m.role, 0, kNumRoles - 1)]);
        return std::string (buf);
    };
    for (const auto& m : shown)
        if (!m.self)
        {
            const CPoint e = arc (m.width, m.space, CColor (kOther.red, kOther.green, kOther.blue, 170), 3.0);
            text (ctx, label (m), CRect (e.x + 6, e.y - 8, e.x + 130, e.y + 6), kOther, 9.5, kLeftText);
        }
    const double w = host->plainValue (kWidth), sp = host->plainValue (kSpace);
    const CColor glow (theme::kAccent.red, theme::kAccent.green, theme::kAccent.blue, 50);
    arc (w, sp, glow, 12.0);
    const CPoint e = arc (w, sp, theme::kAccent, 4.0);
    for (bool right : {false, true})
    {
        const CPoint h = arcEnd (right);
        ctx->setFillColor (theme::kAccent);
        ctx->drawEllipse (CRect (h.x - 5, h.y - 5, h.x + 5, h.y + 5), kDrawFilled);
    }
    int number = 1;
    for (const auto& m : shown)
        if (m.self)
            number = m.number;
    Member me;
    me.number = number;
    me.role = (int)std::lround (host->plainValue (kRole));
    text (ctx, label (me), CRect (e.x + 8, e.y - 8, e.x + 140, e.y + 6), theme::kAccent, 10.0, kLeftText, true);

    // what the numbers are
    char buf[96];
    std::snprintf (buf, sizeof (buf), "Width %s   Space %s", host->valueText (kWidth).c_str (), host->valueText (kSpace).c_str ());
    text (ctx, buf, CRect (f.left + 8, f.top + 4, f.right - 8, f.top + 18), theme::kTextBright, 10.5, kLeftText, true);
    text (ctx, "L", CRect (f.left + 8, c.y - 14, f.left + 20, c.y), theme::kTextDim, 9.5, kLeftText);
    text (ctx, "R", CRect (f.right - 20, c.y - 14, f.right - 8, c.y), theme::kTextDim, 9.5, kRightText);

    // the band strip: gain on the generated side per band (blue: given way to the group)
    const CRect s = strip ();
    ctx->setFillColor (CColor (28, 28, 30));
    ctx->drawRect (s, kDrawFilled);
    const double bw = s.getWidth () / kBands, barTop = s.top + 4.0, barBottom = s.bottom - 14.0;
    const double monoBelow = host->plainValue (kMonoBelow);
    for (int k = 0; k < kBands; ++k)
    {
        const double x0 = s.left + k * bw + 1.5, x1 = s.left + (k + 1) * bw - 1.5;
        const double hgt = barBottom - barTop;
        ctx->setFillColor (theme::kControlBg);
        ctx->drawRect (CRect (x0, barTop, x1, barBottom), kDrawFilled);
        if (bandHighHz (k) <= monoBelow)
            continue; // mono below: nothing generated
        const float g = std::clamp (gains[(size_t)k], 0.0f, 1.0f), y = std::clamp (yields[(size_t)k], 0.0f, 1.0f);
        ctx->setFillColor (theme::kAccent);
        ctx->drawRect (CRect (x0, barBottom - hgt * g, x1, barBottom), kDrawFilled);
        if (y < 0.98f)
        {
            // what the group took: from the gain up to where it would be without yielding
            const double top = std::min (1.0, (double)g / std::max (0.05f, y));
            ctx->setFillColor (kYield);
            ctx->drawRect (CRect (x0, barBottom - hgt * top, x1, barBottom - hgt * g), kDrawFilled);
        }
    }
    const std::pair<int, const char*> ticks[] = {{0, "100"}, {9, "800"}, {18, "6.4k"}};
    for (const auto& [k, name] : ticks)
        text (ctx, name, CRect (s.left + k * bw + 2, s.bottom - 13, s.left + k * bw + 42, s.bottom - 1), theme::kTextDim, 9.0,
              kLeftText);
    text (ctx, "per band: orange = side kept, blue = given to the group", CRect (s.right - 290, s.bottom - 13, s.right - 4, s.bottom - 1),
          theme::kTextDim, 9.0, kRightText);
    if (shown.size () <= 1)
        text (ctx, "Alone: no other Widr in this group (instances in other processes cannot be seen)",
              CRect (f.left + 8, f.top + 20, f.right - 8, f.top + 34), theme::kTextDim, 9.5, kLeftText);
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

StageView::Drag StageView::hit (const CPoint& p) const
{
    if (!field ().pointInside (p))
        return Drag::None;
    for (bool right : {false, true})
    {
        const CPoint e = arcEnd (right);
        if (std::hypot (p.x - e.x, p.y - e.y) <= 10.0)
            return Drag::Width;
    }
    return Drag::Space;
}

void StageView::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    drag = hit (e.mousePosition);
    if (drag == Drag::None)
        return;
    if (e.clickCount == 2)
    {
        host->setOnce (kWidth, host->table ().defaultNormalized (kWidth));
        host->setOnce (kSpace, host->table ().defaultNormalized (kSpace));
        drag = Drag::None;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    down = e.mousePosition;
    startWidth = host->plainValue (kWidth);
    startSpaceN = host->norm (kSpace);
    const CPoint c = listener ();
    widthAtDown = widthFor (std::atan2 (std::fabs (down.x - c.x), std::max (1.0, c.y - down.y)) / kDeg);
    host->beginEdit (drag == Drag::Width ? kWidth : kSpace);
    e.consumed = true;
}

void StageView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == Drag::None)
    {
        const Drag h = hit (e.mousePosition);
        if (auto* f = getFrame ())
            f->setCursor (h == Drag::Width ? kCursorHSize : h == Drag::Space ? kCursorVSize : kCursorDefault);
        return;
    }
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    if (drag == Drag::Width)
    {
        const CPoint c = listener (), p = e.mousePosition;
        const double w = widthFor (std::atan2 (std::fabs (p.x - c.x), std::max (1.0, c.y - p.y)) / kDeg);
        const double v = std::clamp (startWidth + (w - widthAtDown) * fine, 0.0, 2.0);
        host->setNorm (kWidth, host->table ().toNormalized (kWidth, v));
    }
    else
    {
        const double dy = (e.mousePosition.y - down.y) * fine;
        host->setNorm (kSpace, std::clamp (startSpaceN - dy / 180.0, 0.0, 1.0));
    }
    invalid ();
    e.consumed = true;
}

void StageView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag == Drag::None)
        return;
    host->endEdit (drag == Drag::Width ? kWidth : kSpace);
    drag = Drag::None;
    e.consumed = true;
}

void StageView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

void StageView::idle ()
{
    int selfSlot = -1;
    if (auto* s = controller->getShared ())
    {
        selfSlot = s->meters.slot.load (std::memory_order_relaxed);
        for (int k = 0; k < kBands; ++k)
        {
            gains[(size_t)k] = s->meters.bandGain[(size_t)k].load (std::memory_order_relaxed);
            yields[(size_t)k] = s->meters.bandYield[(size_t)k].load (std::memory_order_relaxed);
        }
    }
    // the group, from the registry: a slot whose heartbeat stood still for ~1.5 s of idle calls is gone
    const Registry& reg = Registry::global ();
    const int group = (int)std::lround (host->plainValue (kGroup));
    shown.clear ();
    for (int i = 0; i < Registry::kSlots; ++i)
    {
        const size_t k = (size_t)i;
        const uint64_t id = reg.inUse (i) ? reg.slot (i).id.load (std::memory_order_acquire) : 0;
        const uint32_t hb = reg.slot (i).heartbeat.load (std::memory_order_acquire);
        if (id != ids[k] || hb != beats[k])
        {
            ids[k] = id;
            beats[k] = hb;
            still[k] = 0;
        }
        else if (still[k] < 1000)
            ++still[k];
        if (id == 0 || (i != selfSlot && still[k] > 45))
            continue;
        const auto& d = reg.slot (i).data;
        if (i != selfSlot && (int)d.group.load (std::memory_order_relaxed) != group)
            continue;
        Member m;
        m.id = id;
        m.self = i == selfSlot;
        m.role = (int)d.role.load (std::memory_order_relaxed);
        m.width = d.width.load (std::memory_order_relaxed);
        m.space = d.space.load (std::memory_order_relaxed);
        shown.push_back (m);
    }
    std::sort (shown.begin (), shown.end (), [] (const Member& a, const Member& b) { return a.id < b.id; });
    for (size_t i = 0; i < shown.size (); ++i)
        shown[i].number = (int)i + 1;
    invalid ();
}

} // namespace widr

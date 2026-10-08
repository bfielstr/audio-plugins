#include "GestureView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace moistr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kTitle = 18.0, kPad = 6.0, kFoot = 13.0;

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

GestureView::GestureView (const CRect& r, pk::ParamHost* h, MeterSource m, SlotSource s, UserSource u)
    : CView (r), host (h), meters (std::move (m)), slot (std::move (s)), user (std::move (u))
{
    shown = now ();
}

GestureData GestureView::curveOf (pk::ParamHost* host, int g, const GestureData* userGesture)
{
    const int choice = std::clamp ((int)std::lround (host->plainValue (gestureId (g, kGestureChoice))), 0, kUserGesture);
    GestureData d;
    if (choice == kUserGesture)
    {
        if (userGesture && !userGesture->empty ())
            return *userGesture;
        d.name = "User (none loaded)";
        d.length = 1.0;
        d.points = {{0.0, 1.0}};
        return d;
    }
    const Gesture& ge = factoryGesture (choice);
    d.name = factoryGestureName (choice);
    d.length = ge.length;
    for (int i = 0; i < ge.count; ++i)
        d.points.emplace_back (ge.beat[i], ge.value[i]);
    return d;
}

GestureView::Now GestureView::now () const
{
    Now n;
    n.slot = std::clamp (slot ? slot () : 0, 0, kNumGestureSlots - 1);
    n.target = (int)std::lround (host->plainValue (gestureId (n.slot, kGestureTarget)));
    n.choice = (int)std::lround (host->plainValue (gestureId (n.slot, kGestureChoice)));
    const GestureData* u = user ? user (n.slot) : nullptr;
    n.userPoints = u ? u->points.size () : 0;
    const Meters* m = meters ? meters () : nullptr;
    if (n.target != kTargetOff && m && m->blocks.load (std::memory_order_acquire) > 0)
    {
        constexpr auto rx = std::memory_order_relaxed;
        n.pull = std::round (m->gesturePull[(size_t)n.slot].load (rx) * 100.0f) / 100.0;
        if (n.pull > 0.0)
        {
            n.pos = std::round (m->gesturePos[(size_t)n.slot].load (rx) * 400.0f) / 400.0;
            n.value = std::round (m->gestureValue[(size_t)n.slot].load (rx) * 200.0f) / 200.0;
        }
    }
    return n;
}

void GestureView::idle ()
{
    const Now n = now ();
    if (!(n == shown))
    {
        shown = n;
        invalid ();
    }
}

void GestureView::draw (CDrawContext* ctx)
{
    shown = now ();
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const int g = shown.slot;
    auto plain = [this] (uint32_t id) { return host->plainValue (id); };
    const GestureData d = curveOf (host, g, user ? user (g) : nullptr);
    const bool on = shown.target != kTargetOff;
    const bool flip = plain (gestureId (g, kGestureDepth)) < 0.0;
    // the title: the slot, its gesture and its target
    char title[160];
    std::snprintf (title, sizeof (title), "G%d  %s  >  %s", g + 1, d.name.c_str (), host->valueText (gestureId (g, kGestureTarget)).c_str ());
    text (ctx, title, CRect (all.left + kPad, all.top + 3.0, all.right - kPad, all.top + 17.0), on ? theme::kCopperPale : theme::kTextDim, 10.0,
          kLeftText, true);
    const CRect p (all.left + kPad, all.top + kTitle, all.right - kPad, all.bottom - kFoot - 2.0);
    auto xOf = [&] (double pos) { return p.left + p.getWidth () * std::clamp (pos, 0.0, 1.0); };
    auto yOf = [&] (double v) { return p.bottom - p.getHeight () * std::clamp (flip ? 1.0 - v : v, 0.0, 1.0); };

    // the beat grid of the length it plays at (its own, or Length), the mode and length under it
    const int lengthChoice = std::clamp ((int)std::lround (plain (gestureId (g, kGestureLength))), 0, kNumGestureLengths - 1);
    const double beats = lengthChoice == 0 ? d.length : kGestureLengthBeats[lengthChoice];
    const double step = beats <= 4.0 ? 0.25 : beats <= 16.0 ? 1.0 : 4.0;
    ctx->setLineWidth (1.0);
    for (double b = 0.0; b <= beats + 1e-9 && step > 0.0; b += step)
    {
        const bool whole = std::fabs (b - std::round (b)) < 1e-9 && (step < 1.0 || std::fmod (b, 4.0) < 1e-9);
        ctx->setFrameColor (whole ? theme::kGridMajor : theme::kGridMinor);
        const double x = std::round (xOf (b / beats)) + 0.5;
        ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
    }
    for (double v : {0.0, 0.5, 1.0})
    {
        ctx->setFrameColor (v == 0.5 ? theme::kGridMinor : theme::kGridMajor);
        const double y = std::round (p.bottom - p.getHeight () * v) + 0.5;
        ctx->drawLine (CPoint (p.left, y), CPoint (p.right, y));
    }
    const bool walk = std::lround (plain (gestureId (g, kGestureMode))) == kModeWalk;
    char foot[96];
    if (walk)
        std::snprintf (foot, sizeof (foot), "Walk %s over %g beats", host->valueText (gestureId (g, kGestureSpeed)).c_str (), beats);
    else
        std::snprintf (foot, sizeof (foot), "Loop of %g beats", beats);
    text (ctx, foot, CRect (p.left, p.bottom + 2.0, p.right, p.bottom + 2.0 + kFoot), theme::kTextDim, 9.0, kLeftText);
    if (!note.empty () && (noteFor.slot != shown.slot || noteFor.choice != shown.choice || noteFor.userPoints != shown.userPoints))
        note.clear ();
    if (!note.empty ())
        text (ctx, note, CRect (p.left, p.bottom + 2.0, p.right, p.bottom + 2.0 + kFoot), theme::kEnergyLive, 9.0, kRightText);
    else if (flip)
        text (ctx, "upside down (Depth below 0)", CRect (p.left, p.bottom + 2.0, p.right, p.bottom + 2.0 + kFoot), theme::kTextDim, 9.0, kRightText);

    // the curve: held before its first point and after its last, straight between, jumps straight up or down
    if (!d.points.empty () && d.length > 0.0)
    {
        auto path = owned (ctx->createGraphicsPath ());
        if (path)
        {
            path->beginSubpath (CPoint (xOf (0.0), yOf (d.points.front ().second)));
            for (const auto& [b, v] : d.points)
                path->addLine (CPoint (xOf (b / d.length), yOf (v)));
            path->addLine (CPoint (xOf (1.0), yOf (d.points.back ().second)));
            ctx->setLineWidth (on ? 1.5 : 1.0);
            ctx->setFrameColor (on ? theme::kCopper : theme::kLineDim);
            ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        }
    }

    // the playhead and the value it pulls towards
    if (on && shown.pos >= 0.0)
    {
        const double x = std::round (xOf (shown.pos)) + 0.5;
        ctx->setFrameColor (theme::withAlpha (theme::kPlayhead, 200));
        ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
        const double y = yOf (shown.value);
        ctx->setFillColor (theme::kEnergyLive);
        ctx->drawEllipse (CRect (x - 3.0, y - 3.0, x + 3.0, y + 3.0), kDrawFilled);
    }
}

} // namespace moistr

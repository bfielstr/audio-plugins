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
constexpr double kTitle = 16.0, kPad = 6.0, kFoot = 13.0, kLabelW = 74.0;

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

GestureView::GestureView (const CRect& r, pk::ParamHost* h, MeterSource m, UserSource u, NameSource n)
    : CView (r), host (h), meters (std::move (m)), user (std::move (u)), userName (std::move (n))
{
    shown = now ();
}

const Scene* GestureView::sceneOf (pk::ParamHost* host, const Scene* userScene)
{
    const int choice = std::clamp ((int)std::lround (host->plainValue (kScene)), 0, kSceneUser);
    if (choice == kSceneNone)
        return nullptr;
    if (choice == kSceneUser)
        return userScene;
    return &factoryScene (choice - 1);
}

bool GestureView::slotsOn (pk::ParamHost* host)
{
    for (int g = 0; g < kNumGestureSlots; ++g)
        if (std::lround (host->plainValue (gestureId (g, kGestureTarget))) != kTargetOff)
            return true;
    return false;
}

GestureView::Now GestureView::now () const
{
    Now n;
    n.choice = (int)std::lround (host->plainValue (kScene));
    n.user = user ? user () : nullptr;
    n.slots = slotsOn (host);
    const Meters* m = meters ? meters () : nullptr;
    if (n.choice != kSceneNone && m && m->blocks.load (std::memory_order_acquire) > 0)
    {
        constexpr auto rx = std::memory_order_relaxed;
        n.pull = std::round (m->scenePull.load (rx) * 100.0f) / 100.0;
        if (n.pull > 0.0)
        {
            n.pos = std::round (m->scenePos.load (rx) * 400.0f) / 400.0;
            for (int i = 0; i < kMaxSceneLanes; ++i)
                n.value[(size_t)i] = std::round (m->laneValue[(size_t)i].load (rx) * 100.0f) / 100.0f;
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
    const Scene* sc = sceneOf (host, shown.user);
    const int choice = std::clamp (shown.choice, 0, kSceneUser);
    std::string name = choice == kSceneNone ? "No gesture" : choice == kSceneUser ? (userName ? userName () : std::string ("User")) : factorySceneName (choice - 1);
    if (choice == kSceneUser && !sc)
        name = "User (none loaded: pick a file with File)";
    const bool on = sc && sc->count > 0;
    text (ctx, name, CRect (all.left + kPad, all.top + 2.0, all.right - kPad, all.top + kTitle), on ? theme::kCopperPale : theme::kTextDim, 10.0, kLeftText,
          true);
    const CRect area (all.left + kPad, all.top + kTitle, all.right - kPad, all.bottom - kFoot - 2.0);
    const CRect p (area.left + kLabelW, area.top, area.right, area.bottom);
    auto xOf = [&] (double pos) { return p.left + p.getWidth () * std::clamp (pos, 0.0, 1.0); };

    // the footer: the mode and the length it plays at, a note, and the 0.27 slots
    auto plain = [this] (uint32_t id) { return host->plainValue (id); };
    const int lengthChoice = std::clamp ((int)std::lround (plain (kSceneLength)), 0, kNumGestureLengths - 1);
    const double beats = lengthChoice == 0 ? (sc ? sc->length : 1.0) : kGestureLengthBeats[lengthChoice];
    const bool walk = std::lround (plain (kSceneMode)) == kModeWalk;
    char foot[160];
    if (!on)
        std::snprintf (foot, sizeof (foot), "%s", "");
    else if (walk)
        std::snprintf (foot, sizeof (foot), "Walk %s over %g beats", host->valueText (kSceneSpeed).c_str (), beats);
    else
        std::snprintf (foot, sizeof (foot), "Loop of %g beats", beats);
    const CRect footR (area.left, area.bottom + 2.0, area.right, area.bottom + 2.0 + kFoot);
    text (ctx, foot, footR, theme::kTextDim, 9.0, kLeftText);
    if (!note.empty () && (noteFor.choice != shown.choice || noteFor.user != shown.user))
        note.clear ();
    if (!note.empty ())
        text (ctx, note, footR, theme::kEnergyLive, 9.0, kRightText);
    else if (shown.slots)
        text (ctx, "and 0.27 gesture slots (this project's)", footR, theme::kTextDim, 9.0, kRightText);

    // the beat grid
    const double step = beats <= 4.0 ? 0.25 : beats <= 16.0 ? 1.0 : 4.0;
    ctx->setLineWidth (1.0);
    for (double b = 0.0; b <= beats + 1e-9 && step > 0.0 && beats > 0.0; b += step)
    {
        const bool whole = std::fabs (b - std::round (b)) < 1e-9 && (step < 1.0 || std::fmod (b, 4.0) < 1e-9);
        ctx->setFrameColor (whole ? theme::kGridMajor : theme::kGridMinor);
        const double x = std::round (xOf (b / beats)) + 0.5;
        ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
    }
    if (!on)
        return;

    // the lanes, stacked
    const int n = std::clamp (sc->count, 1, kMaxSceneLanes);
    const double rowH = p.getHeight () / n;
    const double fontSize = std::clamp (rowH - 3.0, 7.0, 9.5);
    for (int i = 0; i < n; ++i)
    {
        const SceneLane& lane = sc->lane[i];
        const double top = p.top + rowH * i, bottom = top + rowH;
        const double y0 = bottom - 1.5, y1 = top + 1.5;
        auto yOf = [&] (double v) { return y0 + (y1 - y0) * std::clamp (v, 0.0, 1.0); };
        if (i > 0)
        {
            ctx->setFrameColor (theme::kGridMinor);
            const double y = std::round (top) + 0.5;
            ctx->drawLine (CPoint (area.left, y), CPoint (p.right, y));
        }
        const int target = std::clamp (lane.target, 0, kNumTargets - 1);
        text (ctx, kTargetNames[target], CRect (area.left, top, p.left - 4.0, bottom), theme::kCopperPale, fontSize, kLeftText);
        const Gesture& g = lane.curve;
        if (g.count > 0 && sc->length > 0.0)
            if (auto path = owned (ctx->createGraphicsPath ()))
            {
                path->beginSubpath (CPoint (xOf (0.0), yOf (g.value[0])));
                for (int k = 0; k < g.count; ++k)
                    path->addLine (CPoint (xOf (g.beat[k] / sc->length), yOf (g.value[k])));
                path->addLine (CPoint (xOf (1.0), yOf (g.value[g.count - 1])));
                ctx->setLineWidth (1.25);
                ctx->setFrameColor (target == kTargetOff ? theme::kLineDim : theme::kCopper);
                ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
            }
        if (shown.pos >= 0.0 && target != kTargetOff)
        {
            const double x = xOf (shown.pos), y = yOf (shown.value[(size_t)i]);
            ctx->setFillColor (theme::kEnergyLive);
            ctx->drawEllipse (CRect (x - 2.5, y - 2.5, x + 2.5, y + 2.5), kDrawFilled);
        }
    }
    // the playhead through every lane (one clock)
    if (shown.pos >= 0.0)
    {
        const double x = std::round (xOf (shown.pos)) + 0.5;
        ctx->setFrameColor (theme::withAlpha (theme::kPlayhead, 200));
        ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
    }
}

} // namespace moistr

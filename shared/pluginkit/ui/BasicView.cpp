#include "pluginkit/ui/BasicView.h"
#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <cmath>

namespace pk::basic {

using namespace VSTGUI;

const char* const kAdvancedHelp =
    "Switches between the Basic page (the main controls, large) and the Advanced view (every control, in "
    "the layout of Menu > Layout). Kept with the project; a new instance opens on the Basic page unless "
    "Menu > Defaults > Advanced View by Default is on. Nothing about the sound changes.";
const char* const kExtrasHelp =
    "Opens or closes the extras under the main controls (what the line beside it lists). Kept with the "
    "project.";
const char* const kTailHelp = "Switches the saturator at the end of the plug-in on or off (its settings are in the extras).";
const char* const kMeterHelp = "The output level: a bar from -60 to +6 dB, the mark at 0 dB.";
const char* const kPresetStepHelp = "The previous or the next preset, in the order of the Presets menu.";

double controlWidth (const Control& c)
{
    if (c.width > 0)
        return c.width;
    switch (c.kind)
    {
        case Control::Kind::Knob: return kKnobW;
        case Control::Kind::Segmented: return 84.0 * (double)std::max<size_t> (1, c.segments.size ());
        case Control::Kind::Choice: return 132.0;
        case Control::Kind::Toggle: return kKnobW;
    }
    return kKnobW;
}

double controlHeight (const Control& c)
{
    switch (c.kind)
    {
        case Control::Kind::Knob: return kKnobH;
        case Control::Kind::Toggle: return kSwitchH;
        case Control::Kind::Choice:
        case Control::Kind::Segmented: return kCaption + 4 + kSwitchH;
    }
    return kKnobH;
}

std::vector<uint32_t> Spec::params () const
{
    std::vector<uint32_t> out;
    for (const auto& r : rows)
        for (const auto& c : r)
            out.push_back (c.id);
    for (const auto& c : output)
        out.push_back (c.id);
    if (tailOn >= 0)
        out.push_back ((uint32_t)tailOn);
    return out;
}

HeaderRight headerRight (double width)
{
    // the same places in every plug-in: Menu at the right edge, ? before it, Advanced before that
    HeaderRight h;
    const double top = 7, bottom = kHeader - 9;
    h.menu = CRect (width - kMargin - 56, top, width - kMargin, bottom);
    h.help = CRect (h.menu.left - 8 - 24, top, h.menu.left - 8, bottom);
    h.advanced = CRect (h.help.left - 8 - 92, top, h.help.left - 8, bottom);
    return h;
}

Geometry place (const Spec& spec, bool extrasOpen)
{
    Geometry g;
    const double w = std::max (spec.width, kWidth);
    g.width = w;

    // ---- the header: title, the plug-in's views, the presets; Advanced, ? and Menu at the right
    const double top = 7, bottom = kHeader - 9;
    double x = kMargin + 2;
    g.title = CRect (x, top - 2, x + 104, bottom + 2);
    x = g.title.right + 8;
    if (spec.header && spec.headerWidth > 0)
    {
        g.headerViews = CRect (x, top - 2, x + spec.headerWidth, bottom + 2);
        x = g.headerViews.right + 16;
    }
    g.presets = CRect (x, top, x + 176, bottom);
    g.presetPrev = CRect (g.presets.right + 4, top, g.presets.right + 28, bottom);
    g.presetNext = CRect (g.presetPrev.right + 4, top, g.presetPrev.right + 28, bottom);
    const HeaderRight hr = headerRight (w);
    g.advanced = hr.advanced;
    g.help = hr.help;
    g.menu = hr.menu;
    if (g.presetNext.right + 8 > g.advanced.left)
        g.problems.push_back ("the header's views reach the Advanced switch");

    // ---- the output column at the right, the display and the main controls left of it
    const double y0 = kHeader + 12;
    const double sideLeft = w - kMargin - kSideW;
    const double left = kMargin, right = sideLeft - kGap;
    double y = y0;
    if (spec.display && spec.displayHeight > 0)
    {
        g.display = CRect (left, y, right, y + spec.displayHeight);
        y = g.display.bottom + kGap;
    }
    // the controls' panel (untitled): the rows kGap apart from 14 px in, each control centred in an even
    // share of its row (the space around them shared out evenly)
    const double panelW = right - left;
    double ry = 14;
    for (const auto& row : spec.rows)
    {
        std::vector<CRect> rects;
        if (row.size () > (size_t)kPerRow)
            g.problems.push_back ("a row of more than " + std::to_string (kPerRow) + " controls");
        double sum = 0, rowH = 0;
        for (const auto& c : row)
        {
            sum += controlWidth (c);
            rowH = std::max (rowH, controlHeight (c));
        }
        const double room = panelW - 16 - sum; // (8 px inside the panel each side)
        if (room < kGap * (double)row.size ())
            g.problems.push_back ("a row too wide for the page");
        const double share = row.empty () ? 0 : room / (double)row.size ();
        double cx = 8 + share / 2;
        for (const auto& c : row)
        {
            const double cw = controlWidth (c), ch = controlHeight (c);
            const double cy = ry + std::round ((rowH - ch) / 2);
            rects.emplace_back (std::round (cx), cy, std::round (cx) + cw, cy + ch);
            cx += cw + share;
        }
        g.rows.push_back (std::move (rects));
        ry += rowH + kGap;
    }
    // the output column: its controls stacked under its title, centred
    double oy = 24;
    for (const auto& c : spec.output)
    {
        const double cw = std::min (controlWidth (c), kSideW - 16), ch = controlHeight (c);
        const double ox = std::round ((kSideW - cw) / 2);
        g.output.emplace_back (ox, oy, ox + cw, oy + ch);
        oy += ch + kGap;
    }
    // both as tall as the taller (the controls' panel at least as tall as the column beside the display)
    const double mainH = std::max (ry - kGap + 12, 60.0);
    g.main = CRect (left, y, right, y + mainH);
    const double sideBottom = std::max (g.main.bottom, y0 + oy - kGap + 12);
    if (sideBottom > g.main.bottom)
        g.main.bottom = sideBottom;
    g.side = CRect (sideLeft, y0, w - kMargin, g.main.bottom);
    y = g.main.bottom + kGap;

    // ---- the extras (open), then the strip
    if (extrasOpen && spec.extras && spec.extrasHeight > 0)
    {
        g.extras = CRect (left, y, w - kMargin, y + spec.extrasHeight);
        y = g.extras.bottom + kGap;
    }
    g.strip = CRect (left, y, w - kMargin, y + kStripH);
    const double sh = kStripH;
    double sx = 8;
    if (spec.extras && spec.extrasHeight > 0)
    {
        g.expand = CRect (sx, 6, sx + 84, sh - 6);
        sx = g.expand.right + 8;
    }
    if (spec.tailOn >= 0)
    {
        g.tail = CRect (sx, 6, sx + 64, sh - 6);
        sx = g.tail.right + 8;
    }
    // the meter under the output column (the strip's coordinates)
    const double meterLeft = sideLeft - left + 8;
    if (spec.level)
        g.meter = CRect (meterLeft, 12, g.strip.getWidth () - 8, sh - 12);
    g.summary = CRect (sx + 4, 6, meterLeft - kGap, sh - 6);
    g.height = g.strip.bottom + 8;
    return g;
}

// ---- the mini meter
MiniMeter::MiniMeter (const CRect& r, std::function<float ()> l) : CView (r), level (std::move (l)) { setMouseEnabled (false); }

double MiniMeter::position (double db) { return std::clamp ((db + 60.0) / 66.0, 0.0, 1.0); }

void MiniMeter::idle ()
{
    const double now = level ? 20.0 * std::log10 (std::max (1e-6, (double)level ())) : -120.0;
    // up at once, down at about 24 dB a second (the editor's idle runs about 30 times a second)
    shownDb = now >= shownDb ? now : std::max (now, shownDb - 0.8);
    const int px = (int)std::lround (position (shownDb) * (getWidth () - 2));
    if (px != shownPx)
    {
        shownPx = px;
        invalid ();
    }
}

void MiniMeter::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (r, kDrawFilled);
    const double inner = r.getWidth () - 2;
    const double px = std::round (position (shownDb) * inner), zero = std::round (position (0.0) * inner);
    if (px > 0)
    {
        ctx->setFillColor (theme::kEnergyLive);
        ctx->drawRect (CRect (r.left + 1, r.top + 2, r.left + 1 + std::min (px, zero), r.bottom - 2), kDrawFilled);
        if (px > zero)
        {
            ctx->setFillColor (theme::kEnergyPeak);
            ctx->drawRect (CRect (r.left + 1 + zero, r.top + 2, r.left + 1 + px, r.bottom - 2), kDrawFilled);
        }
    }
    // the 0 dB mark and the bed's outline
    ctx->setFillColor (theme::kCopper);
    ctx->drawRect (CRect (r.left + 1 + zero, r.top, r.left + 2 + zero, r.bottom), kDrawFilled);
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (theme::kLineDim);
    CRect o = r;
    o.inset (0.5, 0.5);
    ctx->drawRect (o, kDrawStroked);
}

} // namespace pk::basic

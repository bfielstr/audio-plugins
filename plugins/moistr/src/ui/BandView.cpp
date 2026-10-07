#include "BandView.h"

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
constexpr double kTitle = 18.0;
constexpr double kPad = 6.0;
constexpr double kFMin = 20.0, kFMax = 20000.0;
constexpr double kDbMin = kLevelOffDb, kDbMax = 12.0;
constexpr uint32_t kLevelIds[BandSnapshot::kMax] = {kLowLevel, kMidLevel, kHighLevel, kAirLevel};
constexpr uint32_t kMoveIds[BandSnapshot::kMax] = {0, kMidMove, kHighMove, kAirMove}; // (Low: locked)
const char* const kNames[BandSnapshot::kMax] = {"LOW", "MID", "HIGH", "AIR"};

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

std::string hzText (double hz)
{
    char buf[32];
    if (hz >= 1000.0)
        std::snprintf (buf, sizeof (buf), "%.1f kHz", hz / 1000.0);
    else
        std::snprintf (buf, sizeof (buf), "%.0f Hz", hz);
    return buf;
}

// a small padlock, its body's top left at (x, y): 9 x 7, the shackle 5 px above it
void lockGlyph (CDrawContext* ctx, double x, double y, const CColor& c)
{
    ctx->setFillColor (c);
    ctx->drawRect (CRect (x, y, x + 9.0, y + 7.0), kDrawFilled);
    ctx->setFrameColor (c);
    ctx->setLineWidth (1.5);
    ctx->drawLine (CPoint (x + 2.0, y), CPoint (x + 2.0, y - 3.0));
    ctx->drawLine (CPoint (x + 7.0, y), CPoint (x + 7.0, y - 3.0));
    ctx->drawLine (CPoint (x + 2.0, y - 3.0), CPoint (x + 3.5, y - 4.5));
    ctx->drawLine (CPoint (x + 7.0, y - 3.0), CPoint (x + 5.5, y - 4.5));
    ctx->drawLine (CPoint (x + 3.5, y - 4.5), CPoint (x + 5.5, y - 4.5));
    ctx->setLineWidth (1.0);
}
} // namespace

bool BandSnapshot::operator== (const BandSnapshot& o) const
{
    if (bands != o.bands || lowLocked != o.lowLocked || lowKnown != o.lowKnown || active != o.active || glueDb != o.glueDb)
        return false;
    for (int i = 0; i < 3; ++i)
        if (xover[i] != o.xover[i])
            return false;
    for (int b = 0; b < kMax; ++b)
        if (gainDb[b] != o.gainDb[b])
            return false;
    return true;
}

BandView::BandView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m))
{
    snap = snapshot (nullptr, host);
}

double BandView::estimatedLowXover (int seed)
{
    // a fixed spread over the range (log scale): only a stand-in until the engine publishes Seed's pick
    const double u = std::fmod (std::max (seed, 1) * 0.6180339887498949, 1.0);
    return kLowXoverMin * std::pow (kLowXoverMax / kLowXoverMin, u);
}

bool BandView::shows (uint32_t id)
{
    switch (id)
    {
        case kBandCount:
        case kXoverMid:
        case kXoverHigh:
        case kLowLevel:
        case kMidLevel:
        case kHighLevel:
        case kAirLevel:
        case kDepth:
        case kMovement:
        case kMidMove:
        case kHighMove:
        case kAirMove:
        case kSeed:
        case kShiftOn:
        case kShift: return true;
        default: return false;
    }
}

// ---- the adapter: everything the display reads from the engine ------------------------------------------
//
// TODO(engine wiring): the multiband engine publishes the split in its Meters. Replace the marked
// estimates below with its fields and nothing else in the display changes:
//   s.xover[0]   the Low crossover Seed picked (Hz)                 (now: estimatedLowXover (Seed))
//   s.lowKnown   true once xover[0] is the engine's                  (now: false: drawn as the 100 .. 500 Hz range)
//   s.xover[1/2] the upper crossovers now, with their slight drift  (now: Mid X / High X as set)
//   s.gainDb[b]  each band's gain now (dB, kLevelOffDb when off)    (now: the old meters' Mid / High level, else the Level)
BandSnapshot BandView::snapshot (const Meters* m, pk::ParamHost* host)
{
    BandSnapshot s;
    auto plain = [host] (uint32_t id) { return host->plainValue (id); };
    s.bands = std::lround (plain (kBandCount)) == kBands4 ? 4 : 3;
    s.lowLocked = true; // (the Low band never moves)
    s.xover[0] = estimatedLowXover ((int)std::lround (plain (kSeed))); // TODO(engine wiring): the engine's Low crossover
    s.lowKnown = false;                                                 // TODO(engine wiring): true with it
    s.xover[1] = plain (kXoverMid);                                     // TODO(engine wiring): Mid X now
    s.xover[2] = plain (kXoverHigh);                                    // TODO(engine wiring): High X now
    for (int b = 0; b < BandSnapshot::kMax; ++b)
        s.gainDb[b] = plain (kLevelIds[b]);
    if (m)
    {
        constexpr auto rx = std::memory_order_relaxed;
        s.active = m->active.load (rx);
        if (s.active)
        {
            // TODO(engine wiring): the moving bands' gains now. The meters of today carry each band's
            // level with the movement (Low, Mid, High); the Low band's is not used (it is locked).
            constexpr int metered = kBands < BandSnapshot::kMax ? kBands : BandSnapshot::kMax;
            for (int b = 1; b < metered; ++b)
            {
                const float l = m->level[0][(size_t)b].load (rx);
                s.gainDb[b] = l <= -99.0f ? kLevelOffDb : (double)l;
            }
            s.glueDb = std::round (m->glueDb.load (rx) * 10.0f) / 10.0;
        }
    }
    // (the display keeps the crossovers in order, a little apart)
    s.xover[1] = std::max (s.xover[1], s.xover[0] * 1.06);
    s.xover[2] = std::max (s.xover[2], s.xover[1] * 1.06);
    return s;
}

CRect BandView::plot () const
{
    const CRect all = getViewSize ();
    return CRect (all.left + kPad, all.top + kTitle, all.right - kPad, all.bottom - kPad - 12.0); // (the frequency labels under it)
}

double BandView::xOf (double hz) const
{
    const CRect p = plot ();
    return p.left + p.getWidth () * std::log (std::clamp (hz, kFMin, kFMax) / kFMin) / std::log (kFMax / kFMin);
}

double BandView::yOf (double db) const
{
    const CRect p = plot ();
    return p.top + p.getHeight () * (kDbMax - std::clamp (db, kDbMin, kDbMax)) / (kDbMax - kDbMin);
}

void BandView::idle ()
{
    const BandSnapshot s = snapshot (meters ? meters () : nullptr, host);
    if (s != snap)
    {
        snap = s;
        invalid ();
    }
}

void BandView::paintBase (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const CRect p = plot ();
    const int bands = snap.bands;

    // where Seed may put the Low crossover, while the engine's pick is not known
    if (!snap.lowKnown)
    {
        ctx->setFillColor (theme::withAlpha (theme::kLineDim, 90));
        ctx->drawRect (CRect (xOf (kLowXoverMin), p.top, xOf (kLowXoverMax), p.top + 6.0), kDrawFilled);
    }

    // the grid: decades and their steps, 0 / -12 / -24 / -36 dB
    ctx->setLineWidth (1.0);
    for (double dec = 10.0; dec < kFMax; dec *= 10.0)
        for (int k = 1; k <= 9; ++k)
        {
            const double f = dec * k;
            if (f < kFMin || f > kFMax)
                continue;
            ctx->setFrameColor (k == 1 ? theme::kGridMajor : theme::kGridMinor);
            const double x = std::round (xOf (f)) + 0.5;
            ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
        }
    for (double db : {0.0, -12.0, -24.0, -36.0})
    {
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : theme::kGridMinor);
        const double y = std::round (yOf (db)) + 0.5;
        ctx->drawLine (CPoint (p.left, y), CPoint (p.right, y));
    }
    for (double f : {100.0, 1000.0, 10000.0})
    {
        const char* s = f == 100.0 ? "100" : f == 1000.0 ? "1k" : "10k";
        text (ctx, s, CRect (xOf (f) - 20.0, p.bottom + 1.0, xOf (f) + 20.0, p.bottom + 12.0), theme::kTextDim, 9.0, kCenterText);
    }
    for (double db : {0.0, -12.0, -24.0, -36.0})
    {
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%+.0f", db);
        text (ctx, db == 0.0 ? "0" : buf, CRect (p.right - 40.0, yOf (db) - 11.0, p.right - 2.0, yOf (db) - 1.0), theme::kTextDim,
              8.5, kRightText);
    }

    // the Low band: locked, a solid fill up to its Level
    const double xLow = xOf (snap.xover[0]);
    const double lowDb = host->plainValue (kLowLevel);
    if (lowDb > kLevelOffDb)
    {
        ctx->setFillColor (theme::withAlpha (theme::kCopper, 110));
        ctx->drawRect (CRect (p.left, yOf (lowDb), xLow, p.bottom), kDrawFilled);
        ctx->setFrameColor (theme::kCopperPale);
        ctx->setLineWidth (1.5);
        ctx->drawLine (CPoint (p.left, yOf (lowDb)), CPoint (xLow, yOf (lowDb)));
        ctx->setLineWidth (1.0);
    }
    ctx->setFrameColor (theme::kCopperPale);
    ctx->drawLine (CPoint (std::round (xLow) + 0.5, p.top), CPoint (std::round (xLow) + 0.5, p.bottom));
    lockGlyph (ctx, p.left + 6.0, p.top + 9.0, theme::kCopperPale);
    text (ctx, snap.lowKnown ? "LOW X " + hzText (snap.xover[0]) : "LOW X 100-500 Hz",
          CRect (p.left + 20.0, p.top + 3.0, p.left + 160.0, p.top + 17.0), theme::kCopperPale, 9.5, kLeftText, true);

    // the upper crossovers as set (dashed: the live ones are drawn over them)
    ctx->setFrameColor (theme::kLineDim);
    ctx->setLineStyle (theme::dashed ());
    for (int k = 1; k < bands - 1; ++k)
    {
        const double x = std::round (xOf (host->plainValue (k == 1 ? kXoverMid : kXoverHigh))) + 0.5;
        ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
    }
    ctx->setLineStyle (kLineSolid);

    // each moving band: its Level (where it rises to) and how far it falls (Level - Depth, scaled by
    // Movement and its Move), between its set crossovers
    const double depth = host->plainValue (kDepth) * host->plainValue (kMovement);
    const bool shiftOn = host->plainValue (kShiftOn) >= 0.5;
    char shiftText[32];
    std::snprintf (shiftText, sizeof (shiftText), "%+.0f Hz", host->plainValue (kShift));
    for (int b = 1; b < bands; ++b)
    {
        const double lo = b == 1 ? snap.xover[0] : host->plainValue (b == 2 ? kXoverMid : kXoverHigh);
        const double hi = b == bands - 1 ? kFMax : host->plainValue (b == 1 ? kXoverMid : kXoverHigh);
        const double x0 = xOf (lo) + 2.0, x1 = xOf (hi) - 2.0;
        const double lvl = host->plainValue (kLevelIds[b]);
        if (lvl > kLevelOffDb && x1 > x0)
        {
            ctx->setFrameColor (theme::kCopper);
            ctx->drawLine (CPoint (x0, std::round (yOf (lvl)) + 0.5), CPoint (x1, std::round (yOf (lvl)) + 0.5));
            const double fall = depth * host->plainValue (kMoveIds[b]);
            if (fall > 0.05)
            {
                ctx->setFrameColor (theme::kLineDim);
                ctx->setLineStyle (theme::dashed ());
                const double y = std::round (yOf (lvl - fall)) + 0.5;
                ctx->drawLine (CPoint (x0, y), CPoint (x1, y));
                ctx->setLineStyle (kLineSolid);
            }
        }
        const double xc = 0.5 * (xOf (lo) + xOf (hi));
        text (ctx, kNames[b], CRect (xc - 30.0, p.bottom - 13.0, xc + 30.0, p.bottom - 1.0), theme::kCopperPale, 9.0, kCenterText, true);
        // the frequency shifter (on the bands above Low only): how far, over each upper band
        if (shiftOn && xOf (hi) - xOf (lo) > 46.0)
            text (ctx, shiftText, CRect (xc - 40.0, p.top + 3.0, xc + 40.0, p.top + 15.0), theme::kEnergyLive, 9.0, kCenterText, true);
    }
    text (ctx, kNames[0], CRect (p.left, p.bottom - 13.0, std::max (xLow, p.left + 30.0), p.bottom - 1.0), theme::kText, 9.0,
          kCenterText, true);

    text (ctx, bands == 4 ? "4 BANDS" : "3 BANDS", CRect (all.left + kPad, all.top + 3.0, all.left + 200.0, all.top + 17.0),
          theme::kCopperPale, 10.0, kLeftText, true);
}

void BandView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    pk::LayerKey key;
    for (uint32_t id : {kBandCount, kXoverMid, kXoverHigh, kLowLevel, kMidLevel, kHighLevel, kAirLevel, kDepth, kMovement, kMidMove,
                        kHighMove, kAirMove, kShiftOn, kShift})
        key.add (host->plainValue (id));
    key.add (snap.xover[0], snap.lowKnown, snap.bands);
    baseLayer.draw (ctx, all, key, [this] (CDrawContext* c) { paintBase (c); });

    ctx->setClipRect (all);
    const CRect p = plot ();
    // the moving bands, each filled up to its level now
    for (int b = 1; b < snap.bands; ++b)
    {
        if (snap.gainDb[b] <= kLevelOffDb)
            continue;
        const double x0 = xOf (snap.xover[b - 1]) + 1.0;
        const double x1 = b == snap.bands - 1 ? p.right : xOf (snap.xover[b]) - 1.0;
        if (x1 <= x0)
            continue;
        const double y = yOf (snap.gainDb[b]);
        ctx->setFillColor (theme::withAlpha (theme::kCopperPale, snap.active ? 70 : 45));
        ctx->drawRect (CRect (x0, y, x1, p.bottom), kDrawFilled);
        ctx->setFrameColor (snap.active ? theme::kEnergyLive : theme::kCopperPale);
        ctx->setLineWidth (snap.active ? 2.0 : 1.5);
        ctx->drawLine (CPoint (x0, y), CPoint (x1, y));
        ctx->setLineWidth (1.0);
    }
    // the upper crossovers now
    ctx->setFrameColor (theme::kCopper);
    for (int k = 1; k < snap.bands - 1; ++k)
    {
        const double x = std::round (xOf (snap.xover[k])) + 0.5;
        ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
    }
    if (snap.active)
    {
        char buf[48];
        std::snprintf (buf, sizeof (buf), "GLUE %.1f dB", snap.glueDb >= 0.05 ? -snap.glueDb : 0.0);
        text (ctx, buf, CRect (all.right - 160.0, all.top + 3.0, all.right - kPad, all.top + 17.0), theme::kText, 10.0, kRightText);
    }
    ctx->resetClipRect ();
    setDirty (false);
}

} // namespace moistr

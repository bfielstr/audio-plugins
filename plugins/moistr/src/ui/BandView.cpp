#include "BandView.h"

#include "Dsp.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>

namespace moistr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kTitle = 18.0;
constexpr double kPad = 6.0;
constexpr double kFMin = 20.0, kFMax = 20000.0;
constexpr double kDbMin = -36.0, kDbMax = 18.0;
constexpr int kPoints = 160;
constexpr uint32_t kFreqIds[kBands] = {kLowFreq, kMidFreq, kHighFreq};
constexpr uint32_t kResIds[kBands] = {kLowRes, kMidRes, kHighRes};
constexpr uint32_t kLevelIds[kBands] = {kLowLevel, kMidLevel, kHighLevel};
const char* const kNames[kBands] = {"LOW", "MID", "HIGH"};

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

BandView::BandView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

double BandView::responseDb (int band, double f, double fc, double res, double levelDb, bool steep)
{
    if (levelDb <= kLevelOffDb)
        return -200.0;
    const double k1 = 1.0 / dsp::qOf (res), k2 = band == 1 ? k1 : 1.41421356237309504880;
    const std::complex<double> s (0.0, f / fc);
    auto stage = [&] (double k) {
        const std::complex<double> d = s * s + k * s + 1.0;
        if (band == 0)
            return 1.0 / d;
        if (band == 1)
            return k * s / d;
        return s * s / d;
    };
    std::complex<double> h = stage (k1);
    if (steep)
        h *= stage (k2);
    return 20.0 * std::log10 (std::max (std::abs (h), 1e-12)) + levelDb;
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

void BandView::curve (CDrawContext* ctx, int band, double fc, double levelDb, bool dashed)
{
    if (levelDb <= kLevelOffDb)
        return;
    const CRect p = plot ();
    const double res = host->plainValue (kResIds[band]);
    const bool steep = std::lround (host->plainValue (kSlope)) == kSlope24;
    auto path = owned (ctx->createGraphicsPath ());
    if (!path)
        return;
    for (int i = 0; i <= kPoints; ++i)
    {
        const double f = kFMin * std::pow (kFMax / kFMin, (double)i / kPoints);
        const CPoint pt (p.left + p.getWidth () * i / kPoints, yOf (responseDb (band, f, fc, res, levelDb, steep)));
        if (i == 0)
            path->beginSubpath (pt);
        else
            path->addLine (pt);
    }
    // (round joins: the many short segments rasterize the same into the cached layer and onto the window)
    static const CLineStyle round (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound);
    if (dashed)
    {
        CLineStyle d = theme::dashed ();
        d.setLineJoin (CLineStyle::kLineJoinRound);
        ctx->setLineStyle (d);
    }
    else
        ctx->setLineStyle (round);
    ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    ctx->setLineStyle (kLineSolid);
}

void BandView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const uint32_t s = m->blocks.load (std::memory_order_acquire);
    if (s == seen)
        return;
    seen = s;
    constexpr auto rx = std::memory_order_relaxed;
    const bool a = m->active.load (rx);
    bool changed = a != active;
    active = a;
    if (!active)
    {
        if (changed)
            invalid (); // (once, to take the live curves away)
        return;
    }
    const int ps = m->passes.load (rx);
    changed = changed || ps != passes;
    passes = ps;
    for (int k = 0; k < kMaxPasses; ++k)
        for (int b = 0; b < kBands; ++b)
        {
            const float f = m->freq[(size_t)k][(size_t)b].load (rx), l = m->level[(size_t)k][(size_t)b].load (rx);
            changed = changed || f != freq[k][b] || l != level[k][b];
            freq[k][b] = f;
            level[k][b] = l;
        }
    const float g = std::round (m->glueDb.load (rx) * 10.0f) / 10.0f; // (the readout's 0.1 dB steps)
    changed = changed || g != glueDb;
    glueDb = g;
    if (changed)
        invalid ();
}

void BandView::paintBase (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const CRect p = plot ();

    // the hollow between Mid and High (their set frequencies)
    const double fMid = host->plainValue (kMidFreq) * std::exp2 (-host->plainValue (kGap));
    const double fHigh = host->plainValue (kHighFreq) * std::exp2 (host->plainValue (kGap));
    if (fHigh > fMid)
    {
        ctx->setFillColor (theme::withAlpha (theme::kLineDim, 90));
        ctx->drawRect (CRect (xOf (fMid), p.top, xOf (fHigh), p.bottom), kDrawFilled);
    }

    // the grid: decades and their steps, 0 / -12 / -24 dB (and +12)
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
    for (double db : {12.0, 0.0, -12.0, -24.0})
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
    for (double db : {12.0, 0.0, -12.0, -24.0})
    {
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%+.0f", db);
        text (ctx, db == 0.0 ? "0" : buf, CRect (p.left + 2.0, yOf (db) - 11.0, p.left + 40.0, yOf (db) - 1.0), theme::kTextDim, 8.5);
    }

    // each band at its set place
    ctx->setFrameColor (theme::kCopper);
    ctx->setLineWidth (1.0);
    for (int b = 0; b < kBands; ++b)
    {
        const double fc = b == 0 ? host->plainValue (kLowFreq) : b == 1 ? fMid : fHigh;
        curve (ctx, b, fc, host->plainValue (kLevelIds[b]), false);
        text (ctx, kNames[b], CRect (xOf (fc) - 30.0, p.bottom - 13.0, xOf (fc) + 30.0, p.bottom - 1.0), theme::kCopperPale, 9.0,
              kCenterText, true);
    }

    char buf[64];
    if (fHigh > fMid)
        std::snprintf (buf, sizeof (buf), "GAP %.1f OCT", std::log2 (fHigh / fMid));
    else
        std::snprintf (buf, sizeof (buf), "NO GAP");
    text (ctx, buf, CRect (all.left + kPad, all.top + 3.0, all.left + 200.0, all.top + 17.0), theme::kCopperPale, 10.0, kLeftText, true);
}

void BandView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    pk::LayerKey key;
    for (uint32_t id : {kLowFreq, kLowRes, kLowLevel, kMidFreq, kMidRes, kMidLevel, kHighFreq, kHighRes, kHighLevel, kGap, kSlope})
        key.add (host->plainValue (id));
    baseLayer.draw (ctx, all, key, [this] (CDrawContext* c) { paintBase (c); });

    ctx->setClipRect (all);
    if (active)
    {
        const CRect p = plot ();
        for (int k = passes - 1; k >= 0; --k)
        {
            ctx->setFrameColor (k == 0 ? theme::kCopperPale : theme::kCopper);
            ctx->setLineWidth (k == 0 ? 1.5 : 1.0);
            for (int b = 0; b < kBands; ++b)
                curve (ctx, b, freq[k][b], level[k][b] <= -99.0f ? kLevelOffDb : level[k][b], k == 1);
        }
        // the bands' frequencies now (the first pass)
        ctx->setFrameColor (theme::kEnergyLive);
        ctx->setLineWidth (2.0);
        for (int b = 0; b < kBands; ++b)
        {
            if (level[0][b] <= -99.0f)
                continue;
            const double x = xOf (freq[0][b]);
            ctx->drawLine (CPoint (x, p.top), CPoint (x, p.top + 8.0));
        }
        ctx->setLineWidth (1.0);
        char buf[48];
        std::snprintf (buf, sizeof (buf), "GLUE %.1f dB", glueDb >= 0.05f ? -(double)glueDb : 0.0);
        text (ctx, buf, CRect (all.right - 160.0, all.top + 3.0, all.right - kPad, all.top + 17.0), theme::kText, 10.0, kRightText);
    }
    ctx->resetClipRect ();
    setDirty (false);
}

} // namespace moistr

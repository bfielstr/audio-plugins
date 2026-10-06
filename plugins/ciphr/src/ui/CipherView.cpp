#include "CipherView.h"

#include "Wavetable.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ciphr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kTitle = 18.0; // the title row
constexpr double kPad = 6.0;
constexpr double kPitchW = 26.0; // a wave's pitch offset beside it

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kLeftText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

CipherView::CipherView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m)) {}

const Patch& CipherView::patch ()
{
    const int v = (int)std::lround (host->plainValue (kVariant));
    const int set = (int)std::lround (host->plainValue (kWaveSet));
    if (v != cachedVariant || set != cachedSet)
    {
        cached = makePatch (v, set);
        cachedVariant = v;
        cachedSet = set;
    }
    return cached;
}

CRect CipherView::clusterArea () const
{
    const CRect all = getViewSize ();
    const double w = std::min (560.0, std::floor (all.getWidth () * 0.56));
    return CRect (all.left + kPad, all.top + kTitle, all.left + w, all.bottom - kPad);
}

CRect CipherView::tapsArea () const
{
    const CRect all = getViewSize ();
    const CRect c = clusterArea ();
    return CRect (c.right + 14.0, all.top + kTitle, all.right - kPad, all.bottom - kPad - 26.0); // (two lines of text under it)
}

void CipherView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    const uint32_t s = m->blocks.load (std::memory_order_acquire);
    if (s == seen)
        return;
    seen = s;
    // repainted only when what it shows moved (nothing does once the notes have died away)
    bool changed = !havePos;
    const int v = m->voices.load (std::memory_order_relaxed);
    changed = changed || v != voices;
    voices = v;
    for (int k = 0; k < kOscs; ++k)
    {
        const float p = m->oscPos[(size_t)k].load (std::memory_order_relaxed);
        changed = changed || (voices > 0 && p != pos[k]);
        pos[k] = p;
    }
    havePos = true;
    if (changed)
        invalid ();
}

void CipherView::paintBase (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    const Patch& p = patch ();
    const WaveBank& bank = WaveBank::get ();

    // the cluster: a row per oscillator, a cell per entry (its wave's trace and its pitch offset)
    const CRect c = clusterArea ();
    const double rowH = c.getHeight () / kOscs, cellW = c.getWidth () / kEntries;
    ctx->setLineWidth (1.0);
    for (int k = 0; k < kOscs; ++k)
    {
        const double top = c.top + k * rowH;
        if (k > 0)
        {
            ctx->setFrameColor (theme::kGridMinor);
            ctx->drawLine (CPoint (c.left, top), CPoint (c.right, top));
        }
        for (int e = 0; e < kEntries; ++e)
        {
            const Entry& en = p.osc[k][e];
            const double left = c.left + e * cellW;
            const double traceW = std::max (8.0, cellW - kPitchW - 6.0);
            const double mid = top + rowH * 0.5, amp = rowH * 0.32;
            const float* t = bank.table (en.wave, 4); // (32 harmonics: a clean picture)
            constexpr int kPoints = 48;
            if (auto path = owned (ctx->createGraphicsPath ()))
            {
                for (int i = 0; i <= kPoints; ++i)
                {
                    const double ph = (double)i / kPoints;
                    const CPoint pt (left + 2.0 + ph * traceW, mid - amp * WaveBank::read (t, std::min (ph, 0.999999)));
                    if (i == 0)
                        path->beginSubpath (pt);
                    else
                        path->addLine (pt);
                }
                ctx->setFrameColor (theme::kCopper);
                ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
            }
            char buf[16];
            const int semis = (int)std::lround (en.semis);
            std::snprintf (buf, sizeof (buf), semis > 0 ? "+%d" : "%d", semis);
            text (ctx, buf, CRect (left + traceW + 3.0, mid - 7.0, left + cellW - 2.0, mid + 7.0), theme::kTextDim, 9.0, kLeftText);
        }
    }

    // the processor: taps against time, their levels; the haze of Space's diffusion
    const CRect ta = tapsArea ();
    if (ta.getWidth () > 20.0)
    {
        ctx->setFrameColor (theme::kGridMajor);
        ctx->drawLine (CPoint (ta.left, ta.bottom), CPoint (ta.right, ta.bottom));
        const double space = host->plainValue (kSpace);
        const double character = std::clamp (host->plainValue (kCharacter), 0.0, 1.0);
        const double count = 2.0 + 6.0 * character;
        if (space > 0.001)
        {
            CColor haze = theme::kCopper;
            haze.alpha = (uint8_t)std::lround (70.0 * space);
            ctx->setFillColor (haze);
            // the wash: from the first tap to the far end, taller with Space
            const double h = ta.getHeight () * (0.15 + 0.5 * space);
            ctx->drawRect (CRect (ta.left + ta.getWidth () * p.taps[kTaps - 1].time * 0.9, ta.bottom - h, ta.right, ta.bottom), kDrawFilled);
        }
        for (int i = 0; i < kTaps; ++i)
        {
            const double w = i < 2 ? 1.0 : std::clamp (count - i, 0.0, 1.0);
            const double x = ta.left + (ta.getWidth () - 2.0) * p.taps[i].time;
            const double h = ta.getHeight () * p.taps[i].gain * std::max (w, 0.0);
            ctx->setFrameColor (w > 0.0 ? theme::kCopperPale : theme::kLineDim);
            ctx->setLineWidth (w > 0.0 ? 1.5 : 1.0);
            ctx->drawLine (CPoint (x, ta.bottom), CPoint (x, ta.bottom - std::max (2.0, h)));
        }
        ctx->setLineWidth (1.0);
        char buf[96];
        std::snprintf (buf, sizeof (buf), "0  ..  %s", host->valueText (kLength).c_str ());
        text (ctx, buf, CRect (ta.left, ta.bottom + 1.0, ta.right, ta.bottom + 13.0), theme::kTextDim, 9.0, kLeftText);
        std::snprintf (buf, sizeof (buf), "Regen %s  Shift %s", host->valueText (kRegen).c_str (), host->valueText (kShift).c_str ());
        text (ctx, buf, CRect (ta.left, ta.bottom + 14.0, ta.right, ta.bottom + 26.0), theme::kTextDim, 9.0, kLeftText);
    }

    char title[64];
    if (cachedSet == kSetClassic)
        std::snprintf (title, sizeof (title), "CLUSTER  VARIANT %d", cachedVariant);
    else
        std::snprintf (title, sizeof (title), "CLUSTER  VARIANT %d  %s", cachedVariant, host->valueText (kWaveSet).c_str ());
    text (ctx, title, CRect (c.left, all.top + 3.0, c.right, all.top + 17.0), theme::kCopperPale, 10.0, kLeftText, true);
    if (ta.getWidth () > 20.0)
        text (ctx, "TAPS", CRect (ta.left, all.top + 3.0, ta.right, all.top + 17.0), theme::kCopperPale, 10.0, kLeftText, true);
}

void CipherView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    patch ();
    const pk::LayerKey key = pk::LayerKey ()
                                 .add (cachedVariant)
                                 .add (cachedSet)
                                 .add (host->plainValue (kCharacter))
                                 .add (host->plainValue (kSpace))
                                 .add (host->plainValue (kLength))
                                 .add (host->plainValue (kRegen))
                                 .add (host->plainValue (kShift));
    baseLayer.draw (ctx, all, key, [this] (CDrawContext* c) { paintBase (c); });

    // the marks: each oscillator's place in its list (the engine's while notes play, else Timbre's)
    ctx->setClipRect (all);
    const CRect c = clusterArea ();
    const double rowH = c.getHeight () / kOscs, cellW = c.getWidth () / kEntries;
    const double traceW = std::max (8.0, cellW - kPitchW - 6.0);
    const double timbrePos = std::clamp (host->plainValue (kTimbre), 0.0, 1.0) * (kEntries - 1);
    const bool live = havePos && voices > 0;
    ctx->setLineWidth (1.5);
    ctx->setLineStyle (kLineSolid);
    for (int k = 0; k < kOscs; ++k)
    {
        const double pk = live ? std::clamp ((double)pos[k], 0.0, (double)(kEntries - 1)) : timbrePos;
        // the mark sits under the trace's middle at whole entries and glides between them
        const double x = c.left + pk * cellW + 2.0 + traceW * 0.5;
        const double top = c.top + k * rowH;
        ctx->setFrameColor (live ? theme::kEnergyLive : theme::kEnergyIdle);
        ctx->drawLine (CPoint (x, top + 2.0), CPoint (x, top + rowH - 2.0));
    }
    ctx->setLineWidth (1.0);
    char buf[32];
    std::snprintf (buf, sizeof (buf), voices == 1 ? "%d voice" : "%d voices", voices);
    text (ctx, buf, CRect (c.left, all.top + 3.0, c.right - 4.0, all.top + 17.0), voices > 0 ? theme::kText : theme::kTextDim, 10.0,
          kRightText);
    ctx->resetClipRect ();
    setDirty (false);
}

} // namespace ciphr

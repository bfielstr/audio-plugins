#include "GentlrView.h"

#include "smacheratr/src/ui/ColorView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>

namespace gentlr {

using namespace VSTGUI;
namespace theme = pk::theme;
using smacheratr::ClarityBand;

namespace {
// the output spectrum a faint copper body with a copper trace, the input a dashed text-dim trace, the
// whole response a text-coloured line; the cuts being made now are the lit part (cinnabar)
const CColor kSpecFill = theme::withAlpha (theme::kCopper, 34), kSpecLine = theme::withAlpha (theme::kCopper, 170),
             kSpecIn = theme::withAlpha (theme::kTextDim, 130), kCurve = theme::kText;
constexpr double kHandleRadius = 5.0, kEdgeGrab = 4.0;

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kCenterText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

std::complex<double> response (const smacheratr::BiquadCoeffs& c, double hz, double sr)
{
    const std::complex<double> z1 = std::polar (1.0, -2.0 * M_PI * hz / sr), z2 = z1 * z1;
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}

// what a band does to the signal at hz while it cuts cutDb (0 or less) at its peak: x + (g - 1) * band,
// with the band's phase (so the curve is what the sound gets, a little lift beside a narrow band too)
std::complex<double> bandGain (const ClarityBand& b, double hz, double sr, double cutDb)
{
    const double g = std::pow (10.0, cutDb / 20.0);
    const std::complex<double> hp2 = b.hp2On ? response (b.hp2, hz, sr) : 1.0;
    return 1.0 - (1.0 - g) * response (b.hp, hz, sr) * hp2 * response (b.lp, hz, sr) * b.norm;
}

double toDb (std::complex<double> c) { return 20.0 * std::log10 (std::abs (c) + 1e-9); }

// in-place radix-2 FFT
void fft (std::vector<std::complex<float>>& a)
{
    const size_t n = a.size ();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const float ang = -2.0f * (float)M_PI / (float)len;
        const std::complex<float> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
} // namespace

CColor GentlrView::bandColor (int band, uint8_t alpha) { return smacheratr::ColorView::bandColor (band, alpha); }

const smacheratr::GentlrBandParams& GentlrView::bandParams ()
{
    static const smacheratr::GentlrBandParams bp {
        {freqParam (0), freqParam (1), freqParam (kSub), freqParam (kHigh)},
        {bandParam (0, kWidth), bandParam (1, kWidth), -1, -1},
        kNoOverlap,
        {kGlue12, kGlueSub1, kGlueSub2, kGlue1High, kGlue2High},
        [] (pk::ParamHost* h, int k) { return bandWorks (k, h->plainValue (onParam (k)), h->plainValue (rangeParam (k))); }};
    return bp;
}

GentlrView::GentlrView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m))
{
    window.resize (kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t)i] = 0.5f - 0.5f * std::cos (2.0f * (float)M_PI * i / kFftSize);
    bufIn.assign (kFftSize, 0.0f);
    bufOut.assign (kFftSize, 0.0f);
    specIn.assign (kFftSize / 2 + 1, (float)kSpecFloorDb);
    specOut.assign (kFftSize / 2 + 1, (float)kSpecFloorDb);
}

double GentlrView::sampleRate () const
{
    const Meters* m = meters ? meters () : nullptr;
    return m ? std::max (8000.0, (double)m->sampleRate.load ()) : 48000.0;
}

bool GentlrView::works (int band) const
{
    return bandWorks (band, host->plainValue (onParam (band)), host->plainValue (rangeParam (band)));
}

smacheratr::GentlrLayout GentlrView::layoutNow () const { return smacheratr::shownLayout (host, bandParams ()); }

int GentlrView::links (smacheratr::GlueBorder out[kAllBands], CPoint at[kAllBands]) const
{
    const int n = smacheratr::linkBorders (host, bandParams (), out);
    for (int i = 0; i < n; ++i)
        at[i] = CPoint (xOfHz (std::exp2 (out[i].at)), plotBottom () - 10.0);
    return n;
}

int GentlrView::linkAt (const CPoint& p, smacheratr::GlueBorder* b) const
{
    smacheratr::GlueBorder borders[kAllBands];
    CPoint at[kAllBands];
    const int n = links (borders, at);
    for (int i = 0; i < n; ++i)
        if (std::hypot (p.x - at[i].x, p.y - at[i].y) <= smacheratr::kLinkRadius + 1.0)
        {
            if (b)
                *b = borders[i];
            return i;
        }
    return -1;
}

ClarityBand GentlrView::bandNow (int band) const
{
    const smacheratr::GentlrLayout l = layoutNow ();
    if (band == kSub)
        return smacheratr::subBand (sampleRate (), l.freq[band]);
    if (band == kHigh)
        return smacheratr::highBand (sampleRate (), l.freq[band]);
    return smacheratr::clarityBand (sampleRate (), l.freq[band], l.width[band], smacheratr::claritySlopeOf (host->plainValue (kSlope)));
}

double GentlrView::xOfHz (double hz) const
{
    const CRect r = getViewSize ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double GentlrView::hzOfX (double x) const
{
    const CRect r = getViewSize ();
    return kMinHz * std::pow (kMaxHz / kMinHz, std::clamp ((x - r.left) / r.getWidth (), 0.0, 1.0));
}

double GentlrView::yOfDb (double db) const
{
    const double t = plotTop (), b = plotBottom ();
    return t + (kTopDb - std::clamp (db, kBottomDb, kTopDb)) / (kTopDb - kBottomDb) * (b - t);
}

CPoint GentlrView::handle (int band) const
{
    return CPoint (xOfHz (layoutNow ().freq[band]), yOfDb (-host->plainValue (rangeParam (band))));
}

double GentlrView::edgeX (int band, bool high) const
{
    const ClarityBand b = bandNow (band);
    return xOfHz (high ? b.highHz : b.lowHz);
}

CRect GentlrView::pill (int band) const
{
    // above its band, in the first row where it overlaps none of the bands before it
    const CRect all = getViewSize ();
    auto left = [&] (int k) { return std::clamp (handle (k).x - kPillW / 2, all.left + 4, all.right - kPillW - 34); };
    int row[kAllBands] = {};
    for (int k = 0; k <= band; ++k)
    {
        const double x = left (k);
        for (int r = 0; r < kAllBands; ++r)
        {
            bool free = true;
            for (int j = 0; j < k; ++j)
                free &= row[j] != r || std::fabs (x - left (j)) >= kPillW + 4;
            if (free)
            {
                row[k] = r;
                break;
            }
        }
    }
    const double x = left (band), y = all.top + kPillTop + row[band] * (kPillH + 4);
    return CRect (x, y, x + kPillW, y + kPillH);
}

double GentlrView::pillsBottom () const
{
    double b = getViewSize ().top;
    for (int k = 0; k < kAllBands; ++k)
        b = std::max (b, pill (k).bottom);
    return b + 4.0;
}

bool GentlrView::live () const { return lastBlocks != 0 && idleSinceBlock < 10; }

void GentlrView::analyse (const std::vector<float>& in, std::vector<float>& spec)
{
    std::vector<std::complex<float>> a ((size_t)kFftSize);
    float wsum = 0.0f;
    for (int i = 0; i < kFftSize; ++i)
    {
        a[(size_t)i] = in[(size_t)i] * window[(size_t)i];
        wsum += window[(size_t)i];
    }
    fft (a);
    const float scale = 2.0f / wsum; // a sine of amplitude A reads A
    for (size_t k = 1; k < spec.size (); ++k)
    {
        const float db = 20.0f * std::log10 (std::max (1e-7f, std::abs (a[k]) * scale));
        float& s = spec[k];
        s += (db - s) * (db > s ? 0.6f : 0.25f); // quick to rise, slower to fall
    }
}

double GentlrView::specAt (const std::vector<float>& spec, double f0, double f1) const
{
    const double binHz = sampleRate () / kFftSize;
    const double b0 = f0 / binHz, b1 = f1 / binHz;
    const int last = (int)spec.size () - 1;
    if (b1 - b0 < 1.0)
    {
        // between bins: interpolate
        const int i = std::clamp ((int)b0, 1, last - 1);
        const double t = std::clamp (b0 - i, 0.0, 1.0);
        return spec[(size_t)i] + (spec[(size_t)i + 1] - spec[(size_t)i]) * t;
    }
    float mx = (float)kSpecFloorDb;
    for (int i = std::max (1, (int)b0); i <= std::min (last, (int)b1); ++i)
        mx = std::max (mx, spec[(size_t)i]);
    return mx;
}

void GentlrView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    bool changed = false;
    // the cuts, eased
    for (int k = 0; k < kAllBands; ++k)
    {
        const float cut = !m ? 0.0f : smacheratr::clarityCutMeter (m->bands, k).load (std::memory_order_relaxed);
        const float target = works (k) ? cut : 0.0f;
        const float before = shownCut[k];
        shownCut[k] += (target - shownCut[k]) * 0.35f;
        if (std::fabs (shownCut[k]) < 0.01f)
            shownCut[k] = 0.0f;
        changed |= std::fabs (shownCut[k] - before) > 0.005f;
    }
    if (!m)
    {
        if (changed)
            invalid ();
        return;
    }
    const uint32_t blocks = m->blocks.load (std::memory_order_relaxed);
    if (blocks != lastBlocks)
    {
        lastBlocks = blocks;
        idleSinceBlock = 0;
    }
    else if (idleSinceBlock < (1 << 20))
        ++idleSinceBlock;
    const uint32_t w = m->scope.written ();
    if (w != lastWritten)
    {
        lastWritten = w;
        if (m->scope.read (bufIn.data (), bufOut.data (), kFftSize) >= kFftSize / 4)
        {
            analyse (bufIn, specIn);
            analyse (bufOut, specOut);
            haveSpectrum = true;
            changed = true;
        }
    }
    else if (haveSpectrum && !live ())
    {
        // the audio stopped: the spectra sink away
        bool any = false;
        for (auto* spec : {&specIn, &specOut})
            for (auto& s : *spec)
            {
                s = std::max ((float)kSpecFloorDb, s - 3.0f);
                any |= s > (float)kSpecFloorDb;
            }
        haveSpectrum = any;
        changed = true;
    }
    if (changed)
        invalid ();
}

void GentlrView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const double top = plotTop (), bot = plotBottom ();
    const double sr = sampleRate ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);

    bool on[kAllBands];
    ClarityBand bands[kAllBands];
    for (int k = 0; k < kAllBands; ++k)
    {
        on[k] = works (k);
        bands[k] = bandNow (k);
    }

    // the bands' regions, behind everything
    for (int k = 0; k < kAllBands; ++k)
    {
        if (!on[k])
            continue;
        const bool hot = hoverBand == k || (drag != Drag::None && dragBand == k);
        ctx->setFillColor (bandColor (k, hot ? 26 : 16));
        ctx->drawRect (CRect (xOfHz (bands[k].lowHz), all.top, xOfHz (bands[k].highHz), bot), kDrawFilled);
    }

    // grid
    ctx->setLineWidth (1.0);
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
        ctx->setFrameColor (major ? theme::kGridMajor : theme::kGridMinor);
        ctx->drawLine (CPoint (xOfHz (f), top), CPoint (xOfHz (f), bot));
        char b[16];
        std::snprintf (b, sizeof (b), f >= 1000 ? "%.0fk" : "%.0f", f >= 1000 ? f / 1000 : f);
        text (ctx, b, CRect (xOfHz (f) - 20, all.bottom - 15, xOfHz (f) + 20, all.bottom - 2), theme::kTextDim, 9.5);
    }
    for (double db : {0.0, -6.0, -12.0, -18.0, -24.0})
    {
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : theme::kGridMinor);
        ctx->drawLine (CPoint (all.left, yOfDb (db)), CPoint (all.right, yOfDb (db)));
        char b[8];
        std::snprintf (b, sizeof (b), "%.0f", db);
        text (ctx, b, CRect (all.right - 30, yOfDb (db) - 12, all.right - 3, yOfDb (db)), theme::kTextDim, 9.0, kRightText);
    }

    // the spectra on their own scale (0 dBFS at the top), tilted so a mix reads level: the output
    // filled, the input as a line (where it stands above the output, Gentlr is cutting)
    if (haveSpectrum)
    {
        auto ySpec = [&] (double db) { return top + std::clamp (db, kSpecFloorDb, 0.0) / kSpecFloorDb * (bot - top); };
        auto specPath = [&] (const std::vector<float>& spec, bool closed) {
            auto path = owned (ctx->createGraphicsPath ());
            if (!path)
                return path;
            constexpr int kPoints = 220;
            if (closed)
                path->beginSubpath (CPoint (all.left, bot));
            for (int i = 0; i <= kPoints; ++i)
            {
                const double f0 = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / kPoints);
                const double f1 = kMinHz * std::pow (kMaxHz / kMinHz, (double)(i + 1) / kPoints);
                if (f0 >= sr * 0.5)
                    break;
                const CPoint pt (xOfHz (f0), ySpec (specAt (spec, f0, f1) + kSpecTilt * std::log2 (f0 / 1000.0)));
                if (i == 0 && !closed)
                    path->beginSubpath (pt);
                else
                    path->addLine (pt);
            }
            if (closed)
            {
                path->addLine (CPoint (all.right, bot));
                path->closeSubpath ();
            }
            return path;
        };
        if (auto out = specPath (specOut, true))
        {
            ctx->setFillColor (kSpecFill);
            ctx->drawGraphicsPath (out, CDrawContext::kPathFilled);
            ctx->setLineWidth (1.0);
            ctx->setFrameColor (kSpecLine);
            ctx->drawGraphicsPath (out, CDrawContext::kPathStroked);
        }
        if (auto in = specPath (specIn, false))
        {
            ctx->setLineWidth (1.0);
            ctx->setFrameColor (kSpecIn);
            ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0.0, {2.0, 2.0}));
            ctx->drawGraphicsPath (in, CDrawContext::kPathStroked);
            ctx->setLineStyle (kLineSolid);
        }
    }

    // each band at work: the most it can cut (dashed), the cut now (filled from 0 dB), its edges
    const int steps = std::max (64, (int)(all.getWidth () / 3.0));
    auto gainPath = [&] (const std::function<double (double)>& dbAt, bool closed) {
        auto gp = owned (ctx->createGraphicsPath ());
        if (!gp)
            return gp;
        if (closed)
            gp->beginSubpath (CPoint (all.left, yOfDb (0.0)));
        bool started = closed;
        for (int i = 0; i <= steps; ++i)
        {
            const double hz = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / steps);
            if (hz >= 0.4999 * sr)
                break;
            const CPoint pt (xOfHz (hz), yOfDb (dbAt (hz)));
            if (!started)
                gp->beginSubpath (pt);
            else
                gp->addLine (pt);
            started = true;
        }
        if (closed)
        {
            gp->addLine (CPoint (all.right, yOfDb (0.0)));
            gp->closeSubpath ();
        }
        return gp;
    };
    const double edgeTop = pillsBottom ();
    for (int k = 0; k < kAllBands; ++k)
    {
        if (!on[k])
            continue;
        const ClarityBand& b = bands[k];
        const double range = host->plainValue (rangeParam (k));
        if (auto rp = gainPath ([&] (double hz) { return toDb (bandGain (b, hz, sr, -range)); }, false))
        {
            ctx->setLineWidth (1.0);
            ctx->setFrameColor (bandColor (k, 120));
            ctx->setLineStyle (theme::dashed ());
            ctx->drawGraphicsPath (rp, CDrawContext::kPathStroked);
            ctx->setLineStyle (kLineSolid);
        }
        if (shownCut[k] < -0.05f)
            if (auto lp = gainPath ([&] (double hz) { return toDb (bandGain (b, hz, sr, shownCut[k])); }, true))
            {
                // the cut it makes now: the lit part of the display
                ctx->setFillColor (theme::withAlpha (theme::kEnergyLive, 70));
                ctx->drawGraphicsPath (lp, CDrawContext::kPathFilled);
                ctx->setLineWidth (1.0);
                ctx->setFrameColor (theme::kEnergyLive);
                ctx->drawGraphicsPath (lp, CDrawContext::kPathStroked);
            }
        // (Sub, High: no edges to drag; their regions run to the ends of the display)
        if (!hasWidth (k))
            continue;
        const bool edgeHot = dragBand == k && (drag == Drag::Low || drag == Drag::High || drag == Drag::Width);
        ctx->setLineWidth (edgeHot ? 1.5 : 1.0);
        ctx->setFrameColor (edgeHot ? theme::kEnergyLive : bandColor (k, 100));
        for (double x : {xOfHz (b.lowHz), xOfHz (b.highHz)})
            ctx->drawLine (CPoint (x, edgeTop), CPoint (x, bot));
    }

    // the whole response now: every band at its cut, one after the other as the engine applies them
    if (auto path = gainPath (
            [&] (double hz) {
                std::complex<double> h (1.0, 0.0);
                for (int k = 0; k < kAllBands; ++k)
                    if (on[k] && shownCut[k] < 0.0f)
                        h *= bandGain (bands[k], hz, sr, shownCut[k]);
                return toDb (h);
            },
            false))
    {
        ctx->setLineWidth (1.0);
        ctx->setFrameColor (kCurve);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }

    // the handles (a band that does not work: dim, still there to pull down or move)
    for (int k = 0; k < kAllBands; ++k)
    {
        const CPoint h = handle (k);
        const bool hot = hoverBand == k || (drag != Drag::None && dragBand == k);
        const double rad = kHandleRadius + (on[k] ? 1.0 : 0.0);
        pk::draw::handle (ctx, h, rad, hot, on[k]);
    }

    // the glue links on the borders where two bands touch (lit while glued), and where a dragged edge
    // sits snapped (it glues there when the drag ends)
    {
        smacheratr::GlueBorder borders[kAllBands];
        CPoint at[kAllBands];
        const int n = links (borders, at);
        for (int i = 0; i < n; ++i)
            smacheratr::drawLink (ctx, at[i], borders[i].glued, hoverLink == i);
        if (push.active () && push.snapped () >= 0)
            smacheratr::drawLink (ctx, CPoint (xOfHz (std::exp2 (push.snappedAt ())), plotBottom () - 10.0), true, true);
    }

    // the readouts: the band's name, its frequency and its cut now (a click switches the band)
    for (int k = 0; k < kAllBands; ++k)
    {
        const CRect p = pill (k);
        const bool enabled = !hasOn (k) || host->plainValue (onParam (k)) >= 0.5; // (Sub, High: no switch, only a Range)
        char name[8];
        std::snprintf (name, sizeof (name), k == kSub ? "Sub" : k == kHigh ? "High" : "%d", k + 1);
        char s[64];
        if (on[k])
            std::snprintf (s, sizeof (s), "%s   %s   %.1f dB", name, host->valueText (freqParam (k)).c_str (), (double)shownCut[k]);
        else
            std::snprintf (s, sizeof (s), "%s   %s   %s", name, host->valueText (freqParam (k)).c_str (), enabled ? "no range" : "off");
        // the band's name tells it apart (one theme colour for all bands): a well behind the text, a
        // copper outline (dim line while the band does not work)
        ctx->setFillColor (theme::withAlpha (theme::kWell, 225));
        ctx->drawRect (p, kDrawFilled);
        pk::draw::outline (ctx, p, on[k] ? theme::kCopper : theme::kLineDim, 0);
        text (ctx, s, p, on[k] ? theme::kCopperPale : theme::kTextDim, 9.5, kCenterText, true);
    }
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

int GentlrView::bandUnder (const CPoint& p) const
{
    // the second band is drawn on top, so it is found first (Sub and High have no width: not here)
    for (int k = kBands - 1; k >= 0; --k)
        if (works (k) && p.x >= edgeX (k, false) - kEdgeGrab && p.x <= edgeX (k, true) + kEdgeGrab)
            return k;
    return -1;
}

GentlrView::Drag GentlrView::hit (const CPoint& p, int* band) const
{
    auto near = [&] (const CPoint& h) { return std::hypot (p.x - h.x, p.y - h.y) <= kHandleRadius + 4.0; };
    for (int k = kAllBands - 1; k >= 0; --k)
        if (near (handle (k)))
        {
            *band = k;
            return Drag::Handle;
        }
    if (p.y < pillsBottom ())
        return Drag::None; // (the edges start under the readouts)
    for (int k = kBands - 1; k >= 0; --k)
    {
        if (!works (k))
            continue;
        if (std::fabs (p.x - edgeX (k, false)) <= kEdgeGrab)
        {
            *band = k;
            return Drag::Low;
        }
        if (std::fabs (p.x - edgeX (k, true)) <= kEdgeGrab)
        {
            *band = k;
            return Drag::High;
        }
    }
    return Drag::None;
}

void GentlrView::setHover (int band)
{
    if (band != hoverBand)
    {
        hoverBand = band;
        invalid ();
    }
}

void GentlrView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if (!e.buttonState.isLeft () && !right)
        return;
    if (drag != Drag::None) // a drag the host never ended
    {
        MouseUpEvent up;
        onMouseUpEvent (up);
    }
    auto done = [&] {
        drag = Drag::None;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
    };
    // a link icon: glues or detaches the two bands at its border
    smacheratr::GlueBorder link {};
    if (!right && linkAt (e.mousePosition, &link) >= 0)
    {
        smacheratr::toggleGlue (host, bandParams (), link);
        done ();
        return;
    }
    // a band's readout: switches it on or off (the Sub and High bands have no switch: their Range is it)
    for (int k = kAllBands - 1; k >= 0; --k)
        if (hasOn (k) && pill (k).pointInside (e.mousePosition))
        {
            const uint32_t id = onParam (k);
            host->setOnce (id, host->norm (id) >= 0.5 ? 0.0 : 1.0);
            done ();
            return;
        }
    int k = 0;
    drag = hit (e.mousePosition, &k);
    // Alt (Option) held on a band, its handle or anywhere in its region: a sideways drag sets its width
    if (!right && e.clickCount < 2 && e.modifiers.has (ModifierKey::Alt))
    {
        const int under = drag != Drag::None ? k : bandUnder (e.mousePosition);
        if (under >= 0 && hasWidth (under)) // (Sub and High have no width)
        {
            drag = Drag::Width;
            k = under;
        }
    }
    if (drag == Drag::None)
        return;
    dragBand = k;
    const bool sub = !hasWidth (k); // (Sub or High: no width)
    const uint32_t freq = freqParam (k), width = sub ? freq : bandParam (k, kWidth), range = rangeParam (k);
    if (e.clickCount == 2 || right)
    {
        const smacheratr::GentlrLayout before = smacheratr::readLayout (host, bandParams ());
        host->setOnce (freq, host->table ().defaultNormalized (freq));
        if (!sub)
            host->setOnce (width, host->table ().defaultNormalized (width));
        if (drag == Drag::Handle)
            host->setOnce (range, host->table ().defaultNormalized (range));
        smacheratr::pushOnce (host, bandParams (), k, before); // (No Overlap: back where it was, it pushes too)
        done ();
        return;
    }
    down = e.mousePosition;
    // (No Overlap: first splits what overlaps; an edge within 6 px of a neighbour's snaps onto it)
    const double snap = 6.0 * std::log2 (kMaxHz / kMinHz) / std::max (1.0, getViewSize ().getWidth ());
    if (drag == Drag::Handle)
    {
        host->beginEdit (freq);
        host->beginEdit (range);
        push.begin (host, bandParams (), k, {freq, range}, smacheratr::BandPush::Grab::Body, snap);
    }
    else
    {
        host->beginEdit (width);
        push.begin (host, bandParams (), k, {width},
                    drag == Drag::Low    ? smacheratr::BandPush::Grab::LowEdge
                    : drag == Drag::High ? smacheratr::BandPush::Grab::HighEdge
                                         : smacheratr::BandPush::Grab::Width,
                    snap);
    }
    startFreq = host->plainValue (freq);
    startRange = host->plainValue (range);
    startWidth = sub ? 0.0 : host->plainValue (width);
    invalid ();
    e.consumed = true;
}

void GentlrView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == Drag::None)
    {
        const int link = linkAt (e.mousePosition);
        if (link != hoverLink)
        {
            hoverLink = link;
            invalid ();
        }
        if (link >= 0)
        {
            if (auto* f = getFrame ())
                f->setCursor (kCursorHand);
            setHover (-1);
            return;
        }
        int k = -1;
        Drag h = hit (e.mousePosition, &k);
        bool onPill = false;
        for (int b = 0; b < kAllBands; ++b)
            onPill |= hasOn (b) && pill (b).pointInside (e.mousePosition); // (the ones a click switches)
        if (onPill)
            h = Drag::None;
        else if (e.modifiers.has (ModifierKey::Alt) && (h != Drag::None ? hasWidth (k) : bandUnder (e.mousePosition) >= 0))
            h = Drag::Width; // Alt: the band's width, sideways
        if (auto* f = getFrame ())
            f->setCursor (onPill ? kCursorHand : h == Drag::Handle ? kCursorSizeAll : h != Drag::None ? kCursorHSize : kCursorDefault);
        setHover (h != Drag::None && k >= 0 ? k : (onPill ? -1 : bandUnder (e.mousePosition)));
        return;
    }
    const CRect r = getViewSize ();
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
    const double dx = (e.mousePosition.x - down.x) * fine, dy = (e.mousePosition.y - down.y) * fine;
    auto setPlain = [this] (uint32_t id, double v) { host->setNorm (id, host->table ().toNormalized (id, v)); };
    const uint32_t freq = freqParam (dragBand), range = rangeParam (dragBand);
    const uint32_t width = !hasWidth (dragBand) ? freq : bandParam (dragBand, kWidth); // (only a Handle drag for Sub and High)
    if (drag == Drag::Handle)
    {
        // sideways the frequency, down the Range (the handle follows the depth of the cut)
        const double hz = startFreq * std::pow (kMaxHz / kMinHz, dx / r.getWidth ());
        setPlain (freq, dragBand == kSub    ? std::clamp (hz, smacheratr::kSubMinHz, smacheratr::kSubMaxHz)
                        : dragBand == kHigh ? std::clamp (hz, smacheratr::kHighMinHz, smacheratr::kHighMaxHz)
                                            : hz);
        const double dbPerPixel = (kTopDb - kBottomDb) / (plotBottom () - plotTop ());
        setPlain (range, std::clamp (startRange + dy * dbPerPixel, 0.0, 24.0));
    }
    else if (drag == Drag::Width)
    {
        // an octave of width for an octave of mouse travel, the band staying centred (Shift: fine)
        const double octavesPerPixel = std::log2 (kMaxHz / kMinHz) / r.getWidth ();
        setPlain (width, startWidth + dx * octavesPerPixel);
    }
    else
    {
        // the edge follows the mouse; the band stays centred, so the width is twice the distance
        setPlain (width, 2.0 * std::fabs (std::log2 (hzOfX (e.mousePosition.x) / host->plainValue (freq))));
    }
    push.update (host, bandParams ());
    invalid ();
    e.consumed = true;
}

void GentlrView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag == Drag::None)
        return;
    push.end (host, bandParams ()); // (an edge left snapped on a neighbour's: glued)
    if (drag == Drag::Handle)
    {
        host->endEdit (freqParam (dragBand));
        host->endEdit (rangeParam (dragBand));
    }
    else
        host->endEdit (bandParam (dragBand, kWidth));
    drag = Drag::None;
    invalid ();
    e.consumed = true;
}

void GentlrView::onMouseCancelEvent (MouseCancelEvent& e)
{
    MouseUpEvent up;
    onMouseUpEvent (up); // closes the edit of the drag
    e.consumed = true;
}

void GentlrView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    if (drag == Drag::None)
        setHover (-1);
    hoverLink = -1;
    e.consumed = true;
}

void GentlrView::onMouseWheelEvent (MouseWheelEvent& e)
{
    // the suite's wheel on a filter handle (held, or with Shift over it): here the band's width, wheel up wider
    int k = dragBand;
    const bool active = drag != Drag::None || (e.modifiers.has (ModifierKey::Shift) && hit (e.mousePosition, &k) != Drag::None);
    if (!active || !hasWidth (k)) // (Sub and High have no width)
        return;
    const uint32_t id = bandParam (k, kWidth);
    const double dn = pk::wheelStep (e, host->table (), id);
    if (dn == 0.0)
        return;
    const smacheratr::GentlrLayout before = smacheratr::readLayout (host, bandParams ());
    host->setOnce (id, std::clamp (host->norm (id) + dn, 0.0, 1.0));
    if (push.active ())
        push.update (host, bandParams ());
    else
        smacheratr::pushOnce (host, bandParams (), k, before);
    invalid ();
    e.consumed = true;
}

} // namespace gentlr

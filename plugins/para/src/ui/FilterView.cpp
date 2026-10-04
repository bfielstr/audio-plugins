#include "FilterView.h"

#include "../core/Slopes.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

namespace para {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
// The two filters are copper traces told apart by line style (high-pass solid, low-pass dashed) and by
// the HP / LP labels at their handles; the sum is the text-coloured line. The input spectrum a dim
// body, the output a faint copper body with a copper trace; the envelope lights the handles' halos.
const CColor kHpColor = theme::kCopper, kLpColor = theme::kCopper;
const CColor kSpecIn = theme::withAlpha (theme::kLineDim, 150), kSpecOutFill = theme::withAlpha (theme::kCopper, 34),
             kSpecOutLine = theme::withAlpha (theme::kCopper, 170);
constexpr double kHandleRadius = 6.0;
constexpr int kPoints = 200; // along the frequency axis

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
           CHoriTxtAlign a = kCenterText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

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

FilterView::FilterView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m))
{
    window.resize (kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t)i] = 0.5f - 0.5f * std::cos (2.0f * (float)M_PI * i / kFftSize);
    bufIn.resize (kFftSize);
    bufOut.resize (kFftSize);
    specIn.assign (kFftSize / 2 + 1, (float)kSpecFloorDb);
    specOut.assign (kFftSize / 2 + 1, (float)kSpecFloorDb);
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

// the editor idles about 30 times a second: audio counts as running while blocks arrived in the
// last ~10 idles
bool FilterView::live () const { return lastBlocks != 0 && idleSinceBlock < 10; }

void FilterView::effective (double& hp, double& lp, float& hpMul, float& lpMul) const
{
    const double hpBase = host->plainValue (kHpFreq), lpBase = host->plainValue (kLpFreq);
    if (live ())
    {
        // the current settings moved as far as the engine moves them (tracking, envelope, glide),
        // so edits show at once and so does everything the notes do
        hp = hpBase * std::pow (2.0, shownHpShift / 12.0);
        lp = lpBase * std::pow (2.0, shownLpShift / 12.0);
    }
    else
    {
        // no audio: the settings at the last tracked note
        const double split = host->plainValue (kSplit);
        hp = hpCutoff (hpBase, shownOffset, split);
        lp = lpCutoff (lpBase, shownOffset, split);
    }
    // the floor, and Vocal's push and fade, as the engine does them
    lp = std::max (lp, host->plainValue (kLpFloor));
    hpMul = lpMul = 1.0f;
    if (std::lround (host->plainValue (kMovement)) == kVocal)
        vocalPush (hp, lp, leaderLp, host->plainValue (kFade), host->plainValue (kDipStart), hpMul, lpMul);
}

void FilterView::cutoffs (double& hp, double& lp) const
{
    float a, b;
    effective (hp, lp, a, b);
}

double FilterView::handleDb (bool hpSide) const
{
    // the filter's gain plus its resonant peak at the cutoff (the slope's response there: at resonance 0
    // below 0 dB, so the handle sits at the gain), so pulling a handle up shows the resonance
    const double gainDb = gainDbOf (hpSide);
    if (gainDb <= kGainMinDb + 0.01)
        return kMinDb + 1.0;
    const int slope = slopeOf (hpSide);
    const double peak = std::abs (filterResponse (slope, hpSide, 1000.0, 1000.0, host->plainValue (resId (hpSide))));
    return std::max (kMinDb + 1.0, gainDb + 20.0 * std::log10 (std::max (1.0, peak)));
}

// a handle sinks with its Vocal fade (to the bottom at -inf)
CPoint FilterView::hpHandle () const
{
    double hp, lp;
    float hm, lm;
    effective (hp, lp, hm, lm);
    return CPoint (xOfHz (hp), yOfDb (std::max (kMinDb + 1.0, handleDb (true) + 20.0 * std::log10 (std::max (1e-4f, hm)))));
}

CPoint FilterView::lpHandle () const
{
    double hp, lp;
    float hm, lm;
    effective (hp, lp, hm, lm);
    return CPoint (xOfHz (lp), yOfDb (std::max (kMinDb + 1.0, handleDb (false) + 20.0 * std::log10 (std::max (1e-4f, lm)))));
}

void FilterView::trackLeader ()
{
    const double h = host->plainValue (kHpFreq), l = host->plainValue (kLpFreq);
    if (seenHp >= 0.0)
    {
        if (l != seenLp)
            leaderLp = true;
        else if (h != seenHp)
            leaderLp = false;
    }
    seenHp = h;
    seenLp = l;
}

double FilterView::specAt (const std::vector<float>& spec, double f0, double f1) const
{
    const double binHz = rate / kFftSize;
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

void FilterView::draw (CDrawContext* ctx)
{
    trackLeader ();
    const CRect all = getViewSize ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);
    const double plotBottom = all.bottom - 16.0;

    // grid
    ctx->setLineWidth (1.0);
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
    {
        const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
        ctx->setFrameColor (major ? theme::kGridMajor : theme::kGridMinor);
        ctx->drawLine (CPoint (xOfHz (f), all.top), CPoint (xOfHz (f), plotBottom));
        char buf[16];
        std::snprintf (buf, sizeof (buf), f >= 1000 ? "%.0fk" : "%.0f", f >= 1000 ? f / 1000 : f);
        text (ctx, buf, CRect (xOfHz (f) - 20, all.bottom - 15, xOfHz (f) + 20, all.bottom - 2), theme::kTextDim, 9.5);
    }
    for (double db : {-24.0, -12.0, 0.0, 12.0})
    {
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : theme::kGridMinor);
        ctx->drawLine (CPoint (all.left, yOfDb (db)), CPoint (all.right, yOfDb (db)));
        char buf[8];
        std::snprintf (buf, sizeof (buf), "%.0f", db);
        text (ctx, buf, CRect (all.left + 2, yOfDb (db) - 12, all.left + 30, yOfDb (db)), theme::kTextDim, 9.0, kLeftText);
    }

    // live spectra: input (a dim body) and output (copper), on their own scale (0 dBFS at the top)
    if (haveSpectrum)
    {
        auto ySpec = [&] (double db) {
            return all.top + (-std::clamp (db, kSpecFloorDb, 0.0)) / -kSpecFloorDb * (plotBottom - all.top);
        };
        auto spectrum = [&] (const std::vector<float>& spec, const CColor& fill, const CColor* line) {
            auto path = owned (ctx->createGraphicsPath ());
            if (!path)
                return;
            path->beginSubpath (CPoint (all.left, plotBottom));
            for (int i = 0; i <= kPoints; ++i)
            {
                const double f0 = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / kPoints);
                const double f1 = kMinHz * std::pow (kMaxHz / kMinHz, (double)(i + 1) / kPoints);
                if (f0 >= rate * 0.5)
                    break;
                path->addLine (CPoint (xOfHz (f0), ySpec (specAt (spec, f0, f1))));
            }
            path->addLine (CPoint (all.right, plotBottom));
            path->closeSubpath ();
            ctx->setFillColor (fill);
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
            if (line)
            {
                ctx->setLineWidth (1.0);
                ctx->setFrameColor (*line);
                ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
            }
        };
        spectrum (specIn, kSpecIn, nullptr);
        spectrum (specOut, kSpecOutFill, &kSpecOutLine);
    }

    // the filters and their sum
    double hp, lp;
    float hpMul, lpMul;
    effective (hp, lp, hpMul, lpMul);
    const int hpSlope = slopeOf (true), lpSlope = slopeOf (false);
    const double resHp = host->plainValue (resId (true)), resLp = host->plainValue (resId (false));
    // the digital filters as they are: the cutoffs clamped like the engine's, and the analog
    // responses read at the warped frequency (bilinear, prewarped at the cutoff), which bends the
    // curves near the top of the spectrum
    const double nyquist = 0.5 * rate;
    const double hc = std::clamp (hp, 5.0, 0.49 * rate), lc = std::clamp (lp, 5.0, 0.49 * rate);
    auto warped = [&] (double f, double fc) { return fc * std::tan (M_PI * f / rate) / std::tan (M_PI * fc / rate); };
    const double hpGain = filterGain (gainDbOf (true)) * hpMul, lpGain = filterGain (gainDbOf (false)) * lpMul;
    auto curve = [&] (int which, const CColor& stroke, const CColor* fill, double width, bool dashed = false) {
        auto path = owned (ctx->createGraphicsPath ());
        if (!path)
            return;
        const double base = yOfDb (kMinDb);
        double lastX = all.left;
        for (int i = 0; i <= kPoints; ++i)
        {
            const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / kPoints);
            if (f >= nyquist * 0.998)
                break;
            const double fh = warped (f, hc), fl = warped (f, lc);
            // each with the polarity the engine sums it with (Slopes.h)
            const std::complex<double> a = filterResponse (hpSlope, true, fh, hc, resHp) * hpGain,
                                       b = filterResponse (lpSlope, false, fl, lc, resLp) * lpGain;
            const std::complex<double> h = which == 0 ? a : (which == 1 ? b : a + b);
            const CPoint pt (xOfHz (f), yOfDb (20.0 * std::log10 (std::max (1e-6, std::abs (h)))));
            lastX = pt.x;
            if (i == 0)
            {
                path->beginSubpath (fill ? CPoint (pt.x, base) : pt);
                if (fill)
                    path->addLine (pt);
            }
            else
                path->addLine (pt);
        }
        if (fill)
        {
            path->addLine (CPoint (lastX, base));
            path->closeSubpath ();
            ctx->setFillColor (*fill);
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        }
        ctx->setLineWidth (width);
        ctx->setFrameColor (stroke);
        if (dashed)
            ctx->setLineStyle (theme::dashed ());
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        ctx->setLineStyle (kLineSolid);
    };
    const CColor hpFill = theme::withAlpha (theme::kCopper, 22), lpFill = theme::withAlpha (theme::kCopper, 22);
    curve (0, kHpColor, &hpFill, 1.0);
    curve (1, kLpColor, &lpFill, 1.0, true);
    curve (2, theme::kText, nullptr, 1.5);

    // handles, with a halo that grows with the envelope
    for (int k = 0; k < 2; ++k)
    {
        const float mul = k == 0 ? hpMul : lpMul;
        if (mul < 0.99f)
        {
            // pushed by Vocal: a line up to the leader and how far it has faded
            const CPoint h = k == 0 ? hpHandle () : lpHandle (), lead = k == 0 ? lpHandle () : hpHandle ();
            ctx->setLineWidth (1.0);
            ctx->setFrameColor (theme::withAlpha (theme::kCopper, 150));
            ctx->drawLine (lead, h);
            char fade[40];
            if (mul < 1e-3f)
                std::snprintf (fade, sizeof (fade), "%s pushed: -inf", k == 0 ? "HP" : "LP");
            else
                std::snprintf (fade, sizeof (fade), "%s pushed: %.0f dB", k == 0 ? "HP" : "LP", 20.0 * std::log10 (mul));
            text (ctx, fade, CRect (h.x + 10, h.y - 7, h.x + 130, h.y + 7), theme::kCopperPale, 9.5, kLeftText);
        }
    }
    for (int k = 0; k < 2; ++k)
    {
        const CPoint h = k == 0 ? hpHandle () : lpHandle ();
        const float fadeMul = k == 0 ? hpMul : lpMul;
        if (shownEnv > 0.01f)
        {
            // the envelope lights a cinnabar halo round the handle (fainter as Vocal fades the filter)
            const double rr = kHandleRadius + 10.0 * shownEnv;
            const float fade = std::sqrt (std::clamp (fadeMul, 0.0f, 1.0f));
            ctx->setFillColor (theme::withAlpha (theme::kEnergyLive, (uint8_t)((30 + 60 * shownEnv) * (0.3f + 0.7f * fade))));
            ctx->drawEllipse (CRect (h.x - rr, h.y - rr, h.x + rr, h.y + rr), kDrawFilled);
        }
        // the handle (copper once Vocal has pushed it), its filter's name beside it
        const bool held = drag == (k == 0 ? Drag::Hp : Drag::Lp);
        pk::draw::handle (ctx, h, kHandleRadius, held, fadeMul >= 0.99f);
        text (ctx, k == 0 ? "HP" : "LP", CRect (h.x - 20, h.y - kHandleRadius - 14, h.x + 20, h.y - kHandleRadius - 2), theme::kCopperPale, 9.0,
              kCenterText, true);
    }

    // labels, the tracked note and the envelope meter
    char buf[112];
    std::snprintf (buf, sizeof (buf), "HP %s   LP %s   Split %s", host->valueText (kHpFreq).c_str (),
                   host->valueText (kLpFreq).c_str (), host->valueText (kSplit).c_str ());
    text (ctx, buf, CRect (all.left + 36, all.top + 4, all.right - 90, all.top + 18), theme::kText, 10.5, kLeftText, true);
    std::snprintf (buf, sizeof (buf), "now HP %.0f Hz / LP %.0f Hz", hp, lp);
    text (ctx, buf, CRect (all.left + 36, all.top + 19, all.right - 6, all.top + 32), theme::kTextDim, 9.5, kLeftText);
    const CRect envBar (all.right - 84, all.top + 7, all.right - 8, all.top + 15);
    text (ctx, "ENV", CRect (envBar.left - 28, all.top + 4, envBar.left - 4, all.top + 18), theme::kTextDim, 9.0, kRightText);
    ctx->setFillColor (theme::kLineDim);
    ctx->drawRect (envBar, kDrawFilled);
    if (shownEnv > 0.001f)
    {
        ctx->setFillColor (theme::kEnergyLive);
        ctx->drawRect (CRect (envBar.left, envBar.top, envBar.left + envBar.getWidth () * shownEnv, envBar.bottom), kDrawFilled);
    }
    if (!haveSpectrum)
        text (ctx, "play audio through it to see the spectrum", CRect (all.left, plotBottom - 30, all.right, plotBottom - 14),
              theme::kTextDim, 9.5);
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

uint32_t FilterView::resId (bool hp) const
{
    // linked, both handles edit the high-pass resonance
    return hp || host->plainValue (kResLink) >= 0.5 ? kHpRes : kLpRes;
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
    const bool right = e.buttonState.isRight (); // a right click resets, like a double-click
    if (!e.buttonState.isLeft () && !right)
        return;
    drag = hit (e.mousePosition);
    if (drag == Drag::None)
        return;
    const uint32_t fId = drag == Drag::Hp ? kHpFreq : kLpFreq, rId = resId (drag == Drag::Hp);
    const uint32_t gId = drag == Drag::Hp ? kHpGain : kLpGain;
    if (e.clickCount == 2 || right)
    {
        host->setOnce (fId, host->table ().defaultNormalized (fId));
        host->setOnce (rId, host->table ().defaultNormalized (rId));
        host->setOnce (gId, host->table ().defaultNormalized (gId));
        drag = Drag::None;
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    down = e.mousePosition;
    startFreq = host->plainValue (fId);
    startRes = host->plainValue (rId);
    startGain = host->plainValue (gId);
    host->beginEdit (fId);
    host->beginEdit (rId);
    host->beginEdit (gId);
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
    const uint32_t fId = drag == Drag::Hp ? kHpFreq : kLpFreq, rId = resId (drag == Drag::Hp);
    host->setNorm (fId, host->table ().toNormalized (fId, startFreq * std::pow (kMaxHz / kMinHz, dx / r.getWidth ())));
    // up / down: the resonance (150 px for the whole range), and the gain with it when Drag Gain is
    // on; Alt: the gain alone
    const bool gainOnly = e.modifiers.has (ModifierKey::Alt);
    if (!gainOnly)
        host->setNorm (rId, host->table ().toNormalized (rId, std::clamp (startRes - dy / 150.0, 0.0, 1.0)));
    if (gainOnly || host->plainValue (kDragGain) >= 0.5)
    {
        // the gain follows the pointer; dragged to the bottom it is -inf
        const uint32_t gId = drag == Drag::Hp ? kHpGain : kLpGain;
        const double dbPerPx = (kMaxDb - kMinDb) / (r.getHeight () - 16.0);
        double db = std::max (startGain, kMinDb) - dy * dbPerPx;
        if (db <= kMinDb + 0.5)
            db = kGainMinDb;
        // with its Gain Lock on, no higher than 0 dB
        const double top = host->plainValue (gainLockOf (gId)) >= 0.5 ? 0.0 : 12.0;
        host->setNorm (gId, host->table ().toNormalized (gId, std::min (db, top)));
    }
    invalid ();
    e.consumed = true;
}

void FilterView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag == Drag::None)
        return;
    host->endEdit (drag == Drag::Hp ? kHpFreq : kLpFreq);
    host->endEdit (resId (drag == Drag::Hp));
    host->endEdit (drag == Drag::Hp ? kHpGain : kLpGain);
    drag = Drag::None;
    e.consumed = true;
}

void FilterView::onMouseWheelEvent (MouseWheelEvent& e)
{
    // the resonance of the handle held (or under the pointer with Shift)
    const Drag target = drag != Drag::None ? drag : (e.modifiers.has (ModifierKey::Shift) ? hit (e.mousePosition) : Drag::None);
    if (target == Drag::None)
        return;
    const uint32_t rId = resId (target == Drag::Hp);
    const double dn = pk::wheelStep (e, host->table (), rId);
    if (dn == 0.0)
        return;
    const double v = std::clamp (host->norm (rId) + dn, 0.0, 1.0);
    if (drag != Drag::None)
    {
        startRes = std::clamp (startRes + dn, 0.0, 1.0); // the drag goes on from here
        host->setNorm (rId, v);
    }
    else
        host->setOnce (rId, v);
    invalid ();
    e.consumed = true;
}

void FilterView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

void FilterView::analyse (const std::vector<float>& x, std::vector<float>& spec)
{
    std::vector<std::complex<float>> a ((size_t)kFftSize);
    float wsum = 0.0f;
    for (int i = 0; i < kFftSize; ++i)
    {
        a[(size_t)i] = x[(size_t)i] * window[(size_t)i];
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

void FilterView::idle ()
{
    Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    bool changed = false;
    // audio running or not: when it stops, the display goes back to the settings
    const bool wasLive = live ();
    const uint32_t blocks = m->blocks.load (std::memory_order_relaxed);
    if (blocks != lastBlocks)
    {
        lastBlocks = blocks;
        idleSinceBlock = 0;
    }
    else if (idleSinceBlock < (1 << 20))
        ++idleSinceBlock;
    if (live () != wasLive)
        changed = true;
    const float hpShift = m->hpShift.load (std::memory_order_relaxed), lpShift = m->lpShift.load (std::memory_order_relaxed);
    const float hpMul = m->hpMul.load (std::memory_order_relaxed), lpMul = m->lpMul.load (std::memory_order_relaxed);
    const bool leader = m->leaderLp.load (std::memory_order_relaxed);
    if (!leaderKnown)
    {
        leaderLp = leader; // the engine's view when the editor opens; then edits decide
        leaderKnown = true;
    }
    trackLeader ();
    if (std::fabs (hpShift - shownHpShift) > 0.01f || std::fabs (lpShift - shownLpShift) > 0.01f ||
        std::fabs (hpMul - shownHpMul) > 0.005f || std::fabs (lpMul - shownLpMul) > 0.005f || leader != shownLeaderLp)
    {
        shownHpShift = hpShift;
        shownLpShift = lpShift;
        shownHpMul = hpMul;
        shownLpMul = lpMul;
        shownLeaderLp = leader;
        changed = true;
    }
    const float offset = m->offset.load (std::memory_order_relaxed);
    const float env = live () ? m->env.load (std::memory_order_relaxed) : 0.0f; // stale without audio
    const int note = m->note.load (std::memory_order_relaxed);
    if (std::fabs (offset - shownOffset) > 0.01f || std::fabs (env - shownEnv) > 0.005f || note != shownNote)
    {
        shownOffset = offset;
        shownEnv = env;
        shownNote = note;
        changed = true;
    }
    rate = std::max (1000.0f, m->sampleRate.load (std::memory_order_relaxed));
    const uint32_t w = m->scope.written ();
    if (w != lastWritten && m->scope.read (bufIn.data (), bufOut.data (), kFftSize) >= kFftSize / 4)
    {
        lastWritten = w;
        analyse (bufIn, specIn);
        analyse (bufOut, specOut);
        haveSpectrum = true;
        changed = true;
    }
    if (changed)
        invalid ();
}

} // namespace para

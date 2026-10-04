#include "LevelView.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <string>

namespace levlr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
// the output spectrum a faint copper body with a copper trace; the whole response a text-coloured line
const CColor kSpecFill = theme::withAlpha (theme::kCopper, 34), kSpecLine = theme::withAlpha (theme::kCopper, 170), kCurve = theme::kText;
constexpr double kEdgeGrab = 5.0; // pixels either side of a crossover's line

void text (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, CHoriTxtAlign a = kCenterText,
           bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

std::string hzText (double hz)
{
    char buf[24];
    if (hz < 999.5)
        std::snprintf (buf, sizeof (buf), "%.0f Hz", hz);
    else
        std::snprintf (buf, sizeof (buf), hz < 10000.0 ? "%.2f kHz" : "%.1f kHz", hz / 1000.0);
    return buf;
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

// One colour for every band (docs/THEME.md: one metal, one energy colour): pale copper. The bands are
// told apart by their place on the frequency axis and their numbers at the top of each column.
CColor LevelView::bandColor (int band, uint8_t alpha)
{
    (void)band;
    return theme::withAlpha (theme::kCopperPale, alpha);
}

LevelView::LevelView (const CRect& r, pk::ParamHost* h, MeterSource m) : CView (r), host (h), meters (std::move (m))
{
    window.resize (kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t)i] = 0.5f - 0.5f * std::cos (2.0f * (float)M_PI * i / kFftSize);
    buf.assign (kFftSize, 0.0f);
    spec.assign (kFftSize / 2 + 1, (float)kSpecFloorDb);
}

double LevelView::xOfHz (double hz) const
{
    const CRect r = getViewSize ();
    return r.left + std::log (std::clamp (hz, kMinHz, kMaxHz) / kMinHz) / std::log (kMaxHz / kMinHz) * r.getWidth ();
}

double LevelView::hzOfX (double x) const
{
    const CRect r = getViewSize ();
    return kMinHz * std::pow (kMaxHz / kMinHz, std::clamp ((x - r.left) / r.getWidth (), 0.0, 1.0));
}

double LevelView::yOfDb (double db) const
{
    const double t = plotTop (), b = plotBottom ();
    return t + (kRangeDb - std::clamp (db, -kRangeDb, kRangeDb)) / (2.0 * kRangeDb) * (b - t);
}

double LevelView::sampleRate () const
{
    const Meters* m = meters ? meters () : nullptr;
    return m ? std::max (8000.0, (double)m->sampleRate.load ()) : 48000.0;
}

void LevelView::crossovers (double out[kCrossovers]) const
{
    double set[kCrossovers];
    for (int k = 0; k < kCrossovers; ++k)
        set[k] = host->plainValue (xoverParam (k));
    effectiveCrossovers (set, sampleRate (), out);
}

double LevelView::edgeX (int k) const
{
    double xo[kCrossovers];
    crossovers (xo);
    return xOfHz (xo[std::clamp (k, 0, kCrossovers - 1)]);
}

int LevelView::count () const { return bandsOf (host->plainValue (kBandCount)); }
double LevelView::bandLeft (int b) const { return b <= 0 ? getViewSize ().left : edgeX (b - 1); }
double LevelView::bandRight (int b) const { return b >= count () - 1 ? getViewSize ().right : edgeX (b); }

CRect LevelView::chip (int b, bool solo) const
{
    const double l = bandLeft (b), r = bandRight (b);
    if (r - l < 2.0 * kChipW + 10.0)
        return CRect ();
    const double cx = 0.5 * (l + r), top = getViewSize ().top + kChipTop;
    return solo ? CRect (cx + 1.0, top, cx + 1.0 + kChipW, top + kChipH) : CRect (cx - 1.0 - kChipW, top, cx - 1.0, top + kChipH);
}

bool LevelView::live () const { return lastBlocks != 0 && idleSinceBlock < 10; }

void LevelView::analyse ()
{
    std::vector<std::complex<float>> a ((size_t)kFftSize);
    float wsum = 0.0f;
    for (int i = 0; i < kFftSize; ++i)
    {
        a[(size_t)i] = buf[(size_t)i] * window[(size_t)i];
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

double LevelView::specAt (double f0, double f1) const
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

void LevelView::idle ()
{
    const Meters* m = meters ? meters () : nullptr;
    if (!m)
        return;
    bool changed = false;
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
        if (m->scope.read (nullptr, buf.data (), kFftSize) >= kFftSize / 4)
        {
            analyse ();
            haveSpectrum = true;
            changed = true;
        }
    }
    else if (haveSpectrum && !live ())
    {
        // the audio stopped: the spectrum sinks away
        bool any = false;
        for (auto& s : spec)
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

void LevelView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const double top = plotTop (), bot = plotBottom ();
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (all, kDrawFilled);
    ctx->setClipRect (all);

    double xo[kCrossovers], gains[kBands];
    crossovers (xo);
    auto plain = [this] (uint32_t id) { return host->plainValue (id); };
    const int bands = count ();
    bandGains (plain, gains, bands);
    const int slope = std::clamp ((int)std::lround (plain (kSlope)), 0, kNumSlopes - 1);
    const double sr = sampleRate ();

    // the bands' columns (the bands in use)
    for (int b = 0; b < bands; ++b)
    {
        const bool hot = hoverBand == b || (drag == Drag::Band && dragIndex == b);
        ctx->setFillColor (bandColor (b, hot ? 22 : (b % 2 ? 12 : 6))); // every other column a shade lighter
        ctx->drawRect (CRect (bandLeft (b), all.top, bandRight (b), bot), kDrawFilled);
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
    for (double db : {-24.0, -12.0, 0.0, 12.0, 24.0})
    {
        ctx->setFrameColor (db == 0.0 ? theme::kGridZero : theme::kGridMinor);
        ctx->drawLine (CPoint (all.left, yOfDb (db)), CPoint (all.right, yOfDb (db)));
        char b[8];
        std::snprintf (b, sizeof (b), db > 0 ? "+%.0f" : "%.0f", db);
        text (ctx, b, CRect (all.right - 30, yOfDb (db) - 12, all.right - 3, yOfDb (db)), theme::kTextDim, 9.0, kRightText);
    }

    // the output's spectrum, on its own scale (0 dBFS at the top), tilted so a mix reads level
    if (haveSpectrum)
    {
        auto ySpec = [&] (double db) { return top + std::clamp (db, kSpecFloorDb, 0.0) / kSpecFloorDb * (bot - top); };
        if (auto path = owned (ctx->createGraphicsPath ()))
        {
            constexpr int kPoints = 220;
            path->beginSubpath (CPoint (all.left, bot));
            for (int i = 0; i <= kPoints; ++i)
            {
                const double f0 = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / kPoints);
                const double f1 = kMinHz * std::pow (kMaxHz / kMinHz, (double)(i + 1) / kPoints);
                if (f0 >= sr * 0.5)
                    break;
                path->addLine (CPoint (xOfHz (f0), ySpec (specAt (f0, f1) + kSpecTilt * std::log2 (f0 / 1000.0))));
            }
            path->addLine (CPoint (all.right, bot));
            path->closeSubpath ();
            ctx->setFillColor (kSpecFill);
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
            ctx->setLineWidth (1.0);
            ctx->setFrameColor (kSpecLine);
            ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
        }
    }

    // each band at its level: filled from 0 dB, a line at the level, the level in dB; its drive at the foot
    for (int b = 0; b < bands; ++b)
    {
        const bool heard = gains[b] > 0.0;
        const double db = plain (bandParam (b, kGain));
        const double l = bandLeft (b) + 1.0, r = bandRight (b) - 1.0, y = yOfDb (db), y0 = yOfDb (0.0);
        const bool hot = hoverBand == b || (drag == Drag::Band && dragIndex == b);
        if (heard)
        {
            // the level: a faint pale-copper body from 0 dB and a pale copper line (cinnabar while held
            // or hovered); a band not heard: a dashed copper line
            ctx->setFillColor (bandColor (b, hot ? 56 : 36));
            ctx->drawRect (CRect (l, std::min (y, y0), r, std::max (y, y0) + (std::fabs (y - y0) < 1.0 ? 1.0 : 0.0)), kDrawFilled);
            ctx->setLineWidth (1.5);
            ctx->setFrameColor (hot ? theme::kEnergyLive : bandColor (b));
        }
        else
        {
            ctx->setLineStyle (theme::dashed ());
            ctx->setLineWidth (1.0);
            ctx->setFrameColor (theme::kCopper);
        }
        ctx->drawLine (CPoint (l, y), CPoint (r, y));
        ctx->setLineStyle (kLineSolid);
        if (r - l > 46.0)
        {
            char s[24];
            if (!heard)
                std::snprintf (s, sizeof (s), "%s", plain (bandParam (b, kMute)) >= 0.5 ? "muted" : "off (solo)");
            else
                std::snprintf (s, sizeof (s), "%+.1f dB", db);
            const double ty = db >= 0.0 ? y - 16.0 : y + 3.0;
            text (ctx, s, CRect (l, ty, r, ty + 13.0), heard ? bandColor (b) : theme::kTextDim, 10.0, kCenterText, hot);
        }
        const double drive = plain (driveParam (b, kDriveDb));
        if (drive > 0.0 && r - l > 30.0)
        {
            // the drive: its type and amount, a tag above the crossovers' labels
            static const char* kShort[kNumDriveTypes] = {"Analog", "Tape", "Tube", "Hard", "Fold"};
            const int type = std::clamp ((int)std::lround (plain (driveParam (b, kDriveType))), 0, kNumDriveTypes - 1);
            char s[32];
            if (r - l > 84.0)
                std::snprintf (s, sizeof (s), "%s %.1f dB", kShort[type], drive);
            else
                std::snprintf (s, sizeof (s), "%s", kShort[type]);
            const double w = std::min (r - l - 8.0, 8.0 + 6.2 * (double)std::strlen (s)), cx = 0.5 * (l + r);
            const CRect tag (cx - 0.5 * w, bot - 32.0, cx + 0.5 * w, bot - 18.0);
            ctx->setFillColor (theme::withAlpha (theme::kWell, 200));
            ctx->drawRect (tag, kDrawFilled);
            pk::draw::outline (ctx, tag, heard ? theme::kCopper : theme::kLineDim);
            text (ctx, s, tag, heard ? bandColor (b) : theme::kTextDim, 9.5, kCenterText, true);
        }
    }

    // the whole response: the bands' filters added up at their levels, as the engine adds them
    if (auto path = owned (ctx->createGraphicsPath ()))
    {
        const int steps = std::max (64, (int)(all.getWidth () / 2.0));
        bool started = false;
        for (int i = 0; i <= steps; ++i)
        {
            const double f = kMinHz * std::pow (kMaxHz / kMinHz, (double)i / steps);
            if (f >= 0.4999 * sr)
                break;
            const double db = 20.0 * std::log10 (std::abs (totalResponse (xo, slope, gains, f, sr, bands)) + 1e-9);
            const CPoint pt (xOfHz (f), yOfDb (std::clamp (db, -kRangeDb - 3.0, kRangeDb + 3.0)));
            if (!started)
                path->beginSubpath (pt);
            else
                path->addLine (pt);
            started = true;
        }
        ctx->setLineWidth (1.0);
        ctx->setFrameColor (kCurve);
        ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }

    // the crossovers in use: a line with a grip, and the frequency at the foot
    for (int k = 0; k < bands - 1; ++k)
    {
        const double x = xOfHz (xo[k]);
        const bool hot = hoverEdge == k || (drag == Drag::Edge && dragIndex == k);
        // a copper line with an outlined grip (a well inside), both cinnabar while held or hovered
        const CColor lc = hot ? theme::kEnergyLive : theme::kCopper;
        ctx->setLineWidth (1.0);
        ctx->setFrameColor (lc);
        ctx->drawLine (CPoint (x, all.top + kChipTop + kChipH + 4.0), CPoint (x, bot));
        const double mid = 0.5 * (top + bot);
        const CRect grip (std::round (x) - 4.0, mid - 14.0, std::round (x) + 5.0, mid + 14.0);
        ctx->setFillColor (theme::kWell);
        ctx->drawRect (grip, kDrawFilled);
        pk::draw::outline (ctx, grip, lc);
        const CRect label (x - 30.0, bot - 15.0, x + 30.0, bot - 2.0);
        ctx->setFillColor (theme::withAlpha (theme::kWell, 210));
        ctx->drawRect (label, kDrawFilled);
        text (ctx, hzText (xo[k]), label, theme::kText, 9.5, kCenterText, hot);
    }

    // the band numbers, and M / S
    for (int b = 0; b < bands; ++b)
    {
        const double l = bandLeft (b), r = bandRight (b);
        if (r - l > 22.0)
            text (ctx, std::to_string (b + 1), CRect (l + 5.0, all.top + 3.0, l + 20.0, all.top + 18.0), bandColor (b), 10.5, kLeftText, true);
        for (bool solo : {false, true})
        {
            const CRect c = chip (b, solo);
            if (c.isEmpty ())
                continue;
            const bool lit = plain (bandParam (b, solo ? kSolo : kMute)) >= 0.5;
            // outlined chips (too small for a lamp): on, the outline and the letter light cinnabar; mute
            // and solo are told apart by their letters
            pk::draw::outline (ctx, c, lit ? theme::kEnergyLive : theme::kCopper);
            text (ctx, solo ? "S" : "M", c, lit ? theme::kEnergyLive : theme::kCopperPale, 9.5, kCenterText, true);
        }
    }
    ctx->setLineWidth (1.0);
    ctx->resetClipRect ();
}

int LevelView::hitEdge (const CPoint& p) const
{
    const CRect all = getViewSize ();
    if (!all.pointInside (p) || p.y < all.top + kChipTop + kChipH + 2.0)
        return -1;
    int best = -1;
    double bestD = kEdgeGrab;
    for (int k = 0; k < count () - 1; ++k)
    {
        const double d = std::fabs (p.x - edgeX (k));
        if (d <= bestD)
        {
            bestD = d;
            best = k;
        }
    }
    return best;
}

int LevelView::hitBand (const CPoint& p) const
{
    if (!getViewSize ().pointInside (p))
        return -1;
    const int bands = count ();
    for (int b = 0; b < bands; ++b)
        if (p.x >= bandLeft (b) && p.x < bandRight (b))
            return b;
    return bands - 1;
}

bool LevelView::hitChip (const CPoint& p, int& band, bool& solo) const
{
    for (int b = 0; b < count (); ++b)
        for (bool s : {false, true})
        {
            const CRect c = chip (b, s);
            if (!c.isEmpty () && c.pointInside (p))
            {
                band = b;
                solo = s;
                return true;
            }
        }
    return false;
}

void LevelView::setHover (int edge, int band)
{
    if (edge != hoverEdge || band != hoverBand)
    {
        hoverEdge = edge;
        hoverBand = band;
        invalid ();
    }
}

void LevelView::onMouseDownEvent (MouseDownEvent& e)
{
    const bool right = e.buttonState.isRight ();
    if (!e.buttonState.isLeft () && !right)
        return;
    if (drag != Drag::None) // a drag the host never ended
    {
        MouseUpEvent up;
        onMouseUpEvent (up);
    }
    auto done = [&] {
        invalid ();
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
    };
    int b = -1;
    bool solo = false;
    if (hitChip (e.mousePosition, b, solo))
    {
        // M / S: a click switches it; a right click turns it off
        const uint32_t id = bandParam (b, solo ? kSolo : kMute);
        host->setOnce (id, right ? 0.0 : (host->norm (id) >= 0.5 ? 0.0 : 1.0));
        done ();
        return;
    }
    double xo[kCrossovers];
    crossovers (xo);
    const double gap = std::pow (2.0, kMinGapOct);
    if (const int k = hitEdge (e.mousePosition); k >= 0)
    {
        const uint32_t id = xoverParam (k);
        const double lo = k > 0 ? xo[k - 1] * gap : kMinXoverHz;
        const double hi = k < kCrossovers - 1 ? xo[k + 1] / gap : std::min (kMaxXoverHz, 0.45 * sampleRate ());
        if (e.clickCount == 2 || right)
        {
            // back to its default, kept between its neighbours
            const double def = host->table ().info (id).def;
            host->setOnce (id, host->table ().toNormalized (id, std::clamp (def, std::min (lo, hi), hi)));
            done ();
            return;
        }
        drag = Drag::Edge;
        dragIndex = k;
        startValue = xo[k];
        limitLo = lo;
        limitHi = std::max (lo, hi);
        down = e.mousePosition;
        host->beginEdit (id);
        e.consumed = true;
        return;
    }
    if (const int band = hitBand (e.mousePosition); band >= 0)
    {
        const uint32_t id = bandParam (band, kGain);
        if (e.clickCount == 2 || right)
        {
            host->setOnce (id, host->table ().defaultNormalized (id));
            done ();
            return;
        }
        drag = Drag::Band;
        dragIndex = band;
        startValue = host->plainValue (id);
        down = e.mousePosition;
        host->beginEdit (id);
        e.consumed = true;
    }
}

void LevelView::onMouseMoveEvent (MouseMoveEvent& e)
{
    if (drag == Drag::None)
    {
        int b = -1;
        bool solo = false;
        const bool onChip = hitChip (e.mousePosition, b, solo);
        const int edge = onChip ? -1 : hitEdge (e.mousePosition);
        const int band = onChip || edge >= 0 ? -1 : hitBand (e.mousePosition);
        if (auto* fr = getFrame ())
            fr->setCursor (onChip ? kCursorHand : (edge >= 0 ? kCursorHSize : (band >= 0 ? kCursorVSize : kCursorDefault)));
        setHover (edge, band);
        return;
    }
    const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.15 : 1.0;
    const CRect r = getViewSize ();
    if (drag == Drag::Edge)
    {
        const uint32_t id = xoverParam (dragIndex);
        const double dx = (e.mousePosition.x - down.x) * fine;
        const double hz = std::clamp (startValue * std::pow (kMaxHz / kMinHz, dx / r.getWidth ()), limitLo, limitHi);
        host->setNorm (id, host->table ().toNormalized (id, hz));
    }
    else
    {
        const uint32_t id = bandParam (dragIndex, kGain);
        const double dy = (e.mousePosition.y - down.y) * fine;
        const double db = std::clamp (startValue - dy * 2.0 * kRangeDb / (plotBottom () - plotTop ()), -24.0, 24.0);
        host->setNorm (id, host->table ().toNormalized (id, db));
    }
    invalid ();
    e.consumed = true;
}

void LevelView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag == Drag::None)
        return;
    host->endEdit (drag == Drag::Edge ? xoverParam (dragIndex) : bandParam (dragIndex, kGain));
    drag = Drag::None;
    dragIndex = -1;
    invalid ();
    e.consumed = true;
}

void LevelView::onMouseCancelEvent (MouseCancelEvent& e)
{
    MouseUpEvent up;
    onMouseUpEvent (up); // closes the edit of the drag
    e.consumed = true;
}

void LevelView::onMouseExitEvent (MouseExitEvent& e)
{
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    if (drag == Drag::None)
        setHover (-1, -1);
    e.consumed = true;
}

void LevelView::onMouseWheelEvent (MouseWheelEvent& e)
{
    // on a crossover: the slope (the suite's wheel on a filter handle); on a band: its level
    const int edge = drag == Drag::Edge ? dragIndex : hitEdge (e.mousePosition);
    const int band = edge >= 0 ? -1 : (drag == Drag::Band ? dragIndex : hitBand (e.mousePosition));
    const uint32_t id = edge >= 0 ? (uint32_t)kSlope : (band >= 0 ? bandParam (band, kGain) : (uint32_t)kNumParams);
    if (id >= kNumParams)
        return;
    const double dn = pk::wheelStep (e, host->table (), id);
    if (dn == 0.0)
        return;
    host->setOnce (id, std::clamp (host->norm (id) + dn, 0.0, 1.0));
    invalid ();
    e.consumed = true;
}

} // namespace levlr

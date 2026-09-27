#include "WaveformView.h"

#include "Engine.h"
#include "Params.h"
#include "UiKit.h"
#include "plugin/Controller.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/idatapackage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace simplr {

using namespace VSTGUI;

namespace {
constexpr double kRuler = 16.0;
constexpr double kOverview = 7.0;
constexpr double kGrab = 5.0;

class FileDropTarget : public DropTargetAdapter, public NonAtomicReferenceCounted
{
public:
    explicit FileDropTarget (WaveformView* v) : view (v) {}
    DragOperation onDragEnter (DragEventData d) override { return acceptable (d) ? DragOperation::Copy : DragOperation::None; }
    DragOperation onDragMove (DragEventData d) override { return acceptable (d) ? DragOperation::Copy : DragOperation::None; }
    bool onDrop (DragEventData d) override
    {
        std::string path;
        if (!firstPath (d, path) || !view->onFileDropped)
            return false;
        view->onFileDropped (path);
        return true;
    }

private:
    static bool firstPath (DragEventData d, std::string& out)
    {
        if (!d.drag || d.drag->getCount () == 0)
            return false;
        const void* buffer = nullptr;
        IDataPackage::Type type;
        const uint32_t size = d.drag->getData (0, buffer, type);
        if (type != IDataPackage::kFilePath || !buffer || size == 0)
            return false;
        out.assign (static_cast<const char*> (buffer), strnlen (static_cast<const char*> (buffer), size));
        return true;
    }
    static bool acceptable (DragEventData d)
    {
        std::string p;
        return firstPath (d, p) && isSupportedAudioFile (p);
    }
    WaveformView* view;
};

ParamArray paramsFrom (ParamHost* h)
{
    ParamArray p {};
    for (uint32_t i = 0; i < kNumParams; ++i)
        p[i] = h->plainValue (i);
    return p;
}

void drawText (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
               CHoriTxtAlign a = kLeftText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}
} // namespace

WaveformView::WaveformView (const CRect& r, Controller* c, ParamHost* h) : CView (r), controller (c), host (h)
{
    dropTarget = makeOwned<FileDropTarget> (this);
}

WaveformView::~WaveformView () { stopPreview (); }

SharedPointer<IDropTarget> WaveformView::getDropTarget () { return dropTarget; }

SamplePtr WaveformView::sample () const
{
    auto* b = controller->getBridge ();
    return b ? b->sample () : nullptr;
}

CRect WaveformView::rulerArea () const
{
    CRect r = getViewSize ();
    r.bottom = r.top + kRuler;
    return r;
}

CRect WaveformView::loopBar (const SampleData& s) const
{
    double fs, fe, rs, re, ls;
    markerPositions (s, fs, fe, rs, re, ls);
    const CRect w = waveArea ();
    return CRect (posToX (ls / s.length), w.bottom - 14, posToX (re / s.length), w.bottom);
}

CRect WaveformView::waveArea () const
{
    CRect r = getViewSize ();
    r.top += kRuler;
    r.bottom -= kOverview + 2;
    return r;
}

double WaveformView::xToPos (double x) const
{
    const CRect w = waveArea ();
    return viewStart + (x - w.left) / w.getWidth () * viewLen;
}

double WaveformView::posToX (double pos) const
{
    const CRect w = waveArea ();
    return w.left + (pos - viewStart) / viewLen * w.getWidth ();
}

void WaveformView::markerPositions (const SampleData& s, double& fs, double& fe, double& rs, double& re,
                                    double& ls) const
{
    const ParamArray p = paramsFrom (host);
    flagRegion (s, p, fs, fe);
    PlayRegion r;
    classicRegion (s, p, r);
    rs = r.start;
    re = r.end;
    ls = r.loop ? r.loopStart : re - std::max (16.0, p[kLoopLen] * (re - rs));
}

void WaveformView::computeSlicesForDisplay (SliceList& out, const SampleData& s) const
{
    auto* b = controller->getBridge ();
    SliceEditsPtr e = b ? b->editsNow () : nullptr;
    computeSlices (s, e.get (), sliceSettingsFor (s, paramsFrom (host)), out);
}

void WaveformView::draw (CDrawContext* ctx)
{
    const CRect all = getViewSize ();
    const CRect w = waveArea ();
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (all, kDrawFilled);
    ctx->setFillColor (theme::kHeader);
    ctx->drawRect (rulerArea (), kDrawFilled);

    auto s = sample ();
    auto* bridge = controller->getBridge ();
    if (!s || s->length <= 0)
    {
        std::string msg = "Drop an audio file here, or click Load";
        if (bridge && bridge->sampleMissing ())
            msg = "Sample not found: " + bridge->samplePath ();
        else if (!bridge)
            msg = "Waiting for the audio engine...";
        drawText (ctx, msg, w, theme::kTextDim, 13.0, kCenterText);
        return;
    }

    ctx->setClipRect (all);
    const double len = s->length;
    const int mode = (int)std::lround (host->plainValue (kMode));
    double fs, fe, rs, re, ls;
    markerPositions (*s, fs, fe, rs, re, ls);
    const double nfs = fs / len, nfe = fe / len;

    // --- ruler: seconds, or bars when warping ------------------------------------
    const bool warp = host->plainValue (kWarp) >= 0.5;
    {
        const CRect rr = rulerArea ();
        ctx->setLineWidth (1.0);
        if (warp)
        {
            const double beats = std::max (1.0, host->plainValue (kWarpBeats));
            const double beatLen = (fe - fs) / beats / len; // normalized
            const double pxPerBeat = beatLen / viewLen * w.getWidth ();
            int every = 1;
            while (pxPerBeat * every < 40 && every < 1024)
                every *= 2;
            for (int b = 0; b <= (int)beats; b += every)
            {
                const double x = posToX (nfs + b * beatLen);
                if (x < w.left - 1 || x > w.right + 1)
                    continue;
                ctx->setFrameColor (theme::kTextDim);
                ctx->drawLine (CPoint (x, rr.bottom - 5), CPoint (x, rr.bottom));
                char buf[32];
                if (b % 4 == 0)
                    std::snprintf (buf, sizeof (buf), "%d", b / 4 + 1);
                else
                    std::snprintf (buf, sizeof (buf), "%d.%d", b / 4 + 1, b % 4 + 1);
                drawText (ctx, buf, CRect (x + 3, rr.top, x + 60, rr.bottom - 1), theme::kTextDim, 9.5);
            }
        }
        else
        {
            const double secsVisible = viewLen * s->seconds ();
            const double target = secsVisible / (w.getWidth () / 80.0);
            const double steps[] = {0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 30, 60, 120, 300};
            double step = steps[0];
            for (double st : steps)
                if (st >= target)
                {
                    step = st;
                    break;
                }
            const double t0 = std::floor (viewStart * s->seconds () / step) * step;
            for (double t = t0; t <= (viewStart + viewLen) * s->seconds () + step; t += step)
            {
                const double x = posToX (t / s->seconds ());
                if (x < w.left - 1 || x > w.right + 1)
                    continue;
                ctx->setFrameColor (theme::kTextDim);
                ctx->drawLine (CPoint (x, rr.bottom - 5), CPoint (x, rr.bottom));
                char buf[32];
                if (step >= 1.0)
                    std::snprintf (buf, sizeof (buf), "%d:%02d", (int)(t / 60), (int)std::fmod (t, 60.0));
                else
                    std::snprintf (buf, sizeof (buf), step >= 0.1 ? "%.1fs" : (step >= 0.01 ? "%.2fs" : "%.3fs"), t);
                drawText (ctx, buf, CRect (x + 3, rr.top, x + 60, rr.bottom - 1), theme::kTextDim, 9.5);
            }
        }
    }

    // --- beat grid ---------------------------------------------------------------
    if (warp)
    {
        const double beats = std::max (1.0, host->plainValue (kWarpBeats));
        const double beatLen = (fe - fs) / beats / len;
        if (beatLen / viewLen * w.getWidth () > 6)
            for (int b = 0; b <= (int)beats; ++b)
            {
                const double x = posToX (nfs + b * beatLen);
                if (x < w.left || x > w.right)
                    continue;
                ctx->setFrameColor (b % 4 == 0 ? CColor (70, 70, 76) : theme::kGrid);
                ctx->drawLine (CPoint (x, w.top), CPoint (x, w.bottom));
            }
    }

    // --- waveform ----------------------------------------------------------------
    const int lanes = s->numChannels;
    const double laneH = w.getHeight () / lanes;
    const double framesPerPx = viewLen * len / w.getWidth ();
    for (int c = 0; c < lanes; ++c)
    {
        const double mid = w.top + laneH * (c + 0.5);
        const double scale = laneH * 0.46 / std::max (0.05f, std::min (1.0f, s->peakAbs));
        ctx->setFrameColor (CColor (45, 45, 48));
        ctx->drawLine (CPoint (w.left, mid), CPoint (w.right, mid));
        const float* d = s->data (c);
        ctx->setLineWidth (1.0);
        if (framesPerPx < 1.5)
        {
            // draw the actual samples as a polyline
            const int a = std::max (0, (int)std::floor (viewStart * len) - 1);
            const int b = std::min (s->length - 1, (int)std::ceil ((viewStart + viewLen) * len) + 1);
            CPoint prev;
            for (int i = a; i <= b; ++i)
            {
                const CPoint pt (posToX (i / len), mid - d[i] * scale);
                const bool inside = i >= fs && i <= fe;
                ctx->setFrameColor (inside ? theme::kWave : theme::kWaveOutside);
                if (i > a)
                    ctx->drawLine (prev, pt);
                prev = pt;
            }
            continue;
        }
        for (double x = w.left; x < w.right; x += 1.0)
        {
            const double p0 = xToPos (x), p1 = xToPos (x + 1.0);
            int a = (int)std::floor (p0 * len), b = (int)std::ceil (p1 * len);
            a = std::max (a, 0);
            b = std::min (b, s->length);
            if (b <= a)
                continue;
            float lo = 0.0f, hi = 0.0f;
            const int bs = s->peaks.blockSize;
            if (b - a >= bs * 2 && !s->peaks.mn[c].empty ())
            {
                const int ba = a / bs, bb = std::min ((int)s->peaks.mn[c].size (), (b + bs - 1) / bs);
                lo = s->peaks.mn[c][(size_t)ba];
                hi = s->peaks.mx[c][(size_t)ba];
                for (int k = ba + 1; k < bb; ++k)
                {
                    lo = std::min (lo, s->peaks.mn[c][(size_t)k]);
                    hi = std::max (hi, s->peaks.mx[c][(size_t)k]);
                }
            }
            else
            {
                lo = hi = d[a];
                for (int i = a + 1; i < b; ++i)
                {
                    lo = std::min (lo, d[i]);
                    hi = std::max (hi, d[i]);
                }
            }
            const double midPos = (a + b) * 0.5;
            ctx->setFrameColor (midPos >= fs && midPos <= fe ? theme::kWave : theme::kWaveOutside);
            ctx->drawLine (CPoint (x + 0.5, mid - hi * scale - 0.5), CPoint (x + 0.5, mid - lo * scale + 0.5));
        }
    }

    // --- dim outside the flags ---------------------------------------------------
    ctx->setFillColor (CColor (0, 0, 0, 90));
    const double xfs = posToX (nfs), xfe = posToX (nfe);
    if (xfs > w.left)
        ctx->drawRect (CRect (w.left, w.top, std::min (xfs, w.right), w.bottom), kDrawFilled);
    if (xfe < w.right)
        ctx->drawRect (CRect (std::max (xfe, w.left), w.top, w.right, w.bottom), kDrawFilled);

    // --- mode markers ------------------------------------------------------------
    auto vline = [&] (double x, const CColor& c, double width = 1.0) {
        if (x < w.left - 2 || x > w.right + 2)
            return;
        ctx->setLineWidth (width);
        ctx->setFrameColor (c);
        ctx->drawLine (CPoint (x, w.top), CPoint (x, w.bottom));
        ctx->setLineWidth (1.0);
    };
    if (mode == kModeClassic)
    {
        const bool loopOn = host->plainValue (kLoopOn) >= 0.5;
        const double xs = posToX (rs / len), xe = posToX (re / len), xl = posToX (ls / len);
        const CRect region (std::max (w.left, xl), w.top, std::min (w.right, xe), w.bottom);
        const CRect bar = loopBar (*s);
        const CRect barClip (std::max (w.left, bar.left), bar.top, std::min (w.right, bar.right), bar.bottom);
        if (loopOn)
        {
            // shaded loop region with a solid brace along the bottom
            ctx->setFillColor (CColor (120, 200, 120, drag == Handle::LoopBody ? 75 : 55));
            ctx->drawRect (region, kDrawFilled);
            ctx->setFrameColor (CColor (120, 200, 120, 150));
            ctx->setLineWidth (1.0);
            ctx->drawLine (CPoint (region.left, region.top + 0.5), CPoint (region.right, region.top + 0.5));
            ctx->setFillColor (theme::kLoop);
            ctx->drawRect (barClip, kDrawFilled);
            vline (xl, theme::kLoop, 1.5);
            vline (xe, theme::kLoop, 1.5);
            drawText (ctx, "LOOP", barClip, CColor (20, 40, 20), 9.0, kCenterText, true);
        }
        else
        {
            // the loop that would play, dimmed: click the bar to switch looping on
            ctx->setFillColor (CColor (120, 200, 120, 14));
            ctx->drawRect (region, kDrawFilled);
            ctx->setFillColor (CColor (120, 200, 120, 60));
            ctx->drawRect (barClip, kDrawFilled);
            drawText (ctx, "loop off (click)", barClip, CColor (160, 200, 160), 9.0, kCenterText);
        }
        vline (xs, CColor (255, 255, 255, 150));
        vline (xe, CColor (255, 255, 255, 150));
        ctx->setFillColor (CColor (255, 255, 255, 170));
        ctx->drawPolygon ({CPoint (xs, w.bottom - 10), CPoint (xs + 7, w.bottom - 5), CPoint (xs, w.bottom)}, kDrawFilled);
        ctx->drawPolygon ({CPoint (xe, w.bottom - 10), CPoint (xe - 7, w.bottom - 5), CPoint (xe, w.bottom)}, kDrawFilled);
    }
    else if (mode == kModeSlicing)
    {
        SliceList sl;
        computeSlicesForDisplay (sl, *s);
        for (int i = 0; i < sl.count; ++i)
        {
            double pos = sl.pos[i] / len;
            if (drag == Handle::Slice && i == dragSlice)
                pos = dragSlicePos;
            const double x = posToX (pos);
            if (i == hoverSlice)
            {
                const double xn = posToX ((i + 1 < sl.count ? sl.pos[i + 1] : fe) / len);
                ctx->setFillColor (CColor (70, 150, 255, 28));
                ctx->drawRect (CRect (std::max (x, w.left), w.top, std::min (xn, w.right), w.bottom), kDrawFilled);
            }
            vline (x, sl.manual[i] ? theme::kSliceManual : theme::kSliceAuto, i == dragSlice ? 2.0 : 1.0);
            if (x >= w.left - 2 && x <= w.right)
            {
                char buf[16];
                std::snprintf (buf, sizeof (buf), "%d", i + 1);
                drawText (ctx, buf, CRect (x + 3, w.top + 2, x + 40, w.top + 14),
                          sl.manual[i] ? theme::kSliceManual : theme::kSliceAuto, 9.5);
            }
        }
    }

    // --- flags -------------------------------------------------------------------
    for (double x : {xfs, xfe})
    {
        vline (x, theme::kAccent, 1.5);
        if (x >= w.left - 8 && x <= w.right + 8)
        {
            ctx->setFillColor (theme::kAccent);
            const bool left = x == xfs;
            ctx->drawPolygon ({CPoint (x, w.top), CPoint (x + (left ? 9 : -9), w.top), CPoint (x + (left ? 9 : -9), w.top + 6),
                               CPoint (x, w.top + 10)},
                              kDrawFilled);
        }
    }

    // --- playheads ---------------------------------------------------------------
    if (bridge)
    {
        const int n = std::min (bridge->numPlayheads.load (std::memory_order_acquire), Bridge::kMaxPlayheads);
        for (int i = 0; i < n; ++i)
            vline (posToX (bridge->playheads[(size_t)i].load (std::memory_order_relaxed)), theme::kPlayhead);
    }

    // --- overview strip ----------------------------------------------------------
    CRect ov (all.left, all.bottom - kOverview, all.right, all.bottom);
    ctx->setFillColor (CColor (38, 38, 40));
    ctx->drawRect (ov, kDrawFilled);
    ctx->setFillColor (viewLen < 0.999 ? theme::kAccentDim : CColor (60, 60, 64));
    ctx->drawRect (CRect (ov.left + viewStart * ov.getWidth (), ov.top + 1,
                          ov.left + (viewStart + viewLen) * ov.getWidth (), ov.bottom - 1),
                   kDrawFilled);

    // name + info
    char info[160];
    std::snprintf (info, sizeof (info), "%s   %.3f s   %d Hz   %s", s->name.c_str (), s->seconds (),
                   (int)s->sampleRate, s->numChannels > 1 ? "Stereo" : "Mono");
    const CRect rr = rulerArea ();
    ctx->setFillColor (theme::kHeader);
    ctx->drawRect (CRect (rr.right - 330, rr.top, rr.right, rr.bottom), kDrawFilled);
    drawText (ctx, info, CRect (rr.right - 326, rr.top, rr.right - 6, rr.bottom - 1), theme::kTextDim, 9.5,
              kRightText);
    ctx->resetClipRect ();
}

WaveformView::Handle WaveformView::hitTest (const CPoint& p, int& sliceIndex) const
{
    sliceIndex = -1;
    auto s = sample ();
    if (!s)
        return Handle::None;
    if (rulerArea ().pointInside (p))
        return Handle::Ruler;
    const double len = s->length;
    double fs, fe, rs, re, ls;
    markerPositions (*s, fs, fe, rs, re, ls);
    auto near = [&] (double pos) { return std::fabs (posToX (pos / len) - p.x) <= kGrab; };
    const int mode = (int)std::lround (host->plainValue (kMode));
    if (mode == kModeSlicing)
    {
        SliceList sl;
        computeSlicesForDisplay (sl, *s);
        for (int i = 1; i < sl.count; ++i) // the region start is the start flag
            if (near (sl.pos[i]))
            {
                sliceIndex = i;
                return Handle::Slice;
            }
    }
    if (near (fs))
        return Handle::FlagStart;
    if (near (fe))
        return Handle::FlagEnd;
    if (mode == kModeClassic)
    {
        const CRect w = waveArea ();
        const bool lower = p.y > w.getCenter ().y;
        const CRect bar = loopBar (*s);
        const bool inBarRow = p.y >= bar.top - 2 && p.y <= w.bottom;
        if (inBarRow && near (ls) && ls > rs + 1)
            return Handle::LoopStart;
        if (inBarRow && near (re))
            return Handle::LengthEnd;
        if (inBarRow && p.x > bar.left && p.x < bar.right)
            return Handle::LoopBody;
        if (host->plainValue (kLoopOn) >= 0.5 && near (ls) && ls > rs + 1)
            return Handle::LoopStart;
        if (near (rs) && (lower || !near (fs)))
            return Handle::Start;
        if (near (re) && (lower || !near (fe)))
            return Handle::LengthEnd;
    }
    return Handle::None;
}

void WaveformView::onMouseDownEvent (MouseDownEvent& e)
{
    auto s = sample ();
    const CPoint p = e.mousePosition;
    if (e.buttonState.isRight () || (e.buttonState.isLeft () && e.modifiers.has (ModifierKey::Super)))
    {
        if (onContextMenu)
        {
            CPoint fp = p;
            localToFrame (fp);
            onContextMenu (fp);
        }
        e.consumed = true;
        e.ignoreFollowUpMoveAndUpEvents (true);
        return;
    }
    if (!e.buttonState.isLeft () || !s)
        return;
    dragStartPoint = lastPoint = p;
    moved = false;
    const int mode = (int)std::lround (host->plainValue (kMode));
    int si = -1;
    const Handle h = hitTest (p, si);
    const double len = s->length;

    if (mode == kModeSlicing && h != Handle::Ruler)
    {
        const double pos = std::clamp (xToPos (p.x), 0.0, 1.0);
        if (e.clickCount == 2)
        {
            if (h == Handle::Slice)
                removeSlice (si);
            else
                addManualSlice (pos);
            e.consumed = true;
            e.ignoreFollowUpMoveAndUpEvents (true);
            invalid ();
            return;
        }
        if (h == Handle::Slice && e.modifiers.has (ModifierKey::Alt))
        {
            toggleSliceKind (si);
            e.consumed = true;
            e.ignoreFollowUpMoveAndUpEvents (true);
            invalid ();
            return;
        }
        if (h == Handle::Slice)
        {
            SliceList sl;
            computeSlicesForDisplay (sl, *s);
            drag = Handle::Slice;
            dragSlice = si;
            dragSliceManual = sl.manual[si];
            dragSliceOrigPos = dragSlicePos = sl.pos[si] / len;
            e.consumed = true;
            return;
        }
    }

    drag = h;
    switch (h)
    {
        case Handle::FlagStart: host->beginEdit (kSampleStart); break;
        case Handle::FlagEnd: host->beginEdit (kSampleEnd); break;
        case Handle::Start: host->beginEdit (kStart); break;
        case Handle::LengthEnd: host->beginEdit (kLength); break;
        case Handle::LoopStart: host->beginEdit (kLoopLen); break;
        case Handle::LoopBody:
        {
            double fs, fe, rs, re, ls;
            markerPositions (*s, fs, fe, rs, re, ls);
            loopDragDownPos = xToPos (p.x) * len;
            loopDragRe = re;
            loopDragLen = re - ls;
            host->beginEdit (kLength);
            host->beginEdit (kLoopLen);
            break;
        }
        case Handle::Ruler: break;
        default:
        {
            // audition: the slice under the mouse, or the root note
            drag = Handle::Preview;
            int note = kRootNote;
            if (mode == kModeSlicing)
            {
                SliceList sl;
                computeSlicesForDisplay (sl, *s);
                const double pos = xToPos (p.x) * len;
                note = -1;
                for (int i = sl.count - 1; i >= 0; --i)
                    if (pos >= sl.pos[i])
                    {
                        note = kSliceBaseNote + i;
                        break;
                    }
            }
            if (note >= 0 && controller->getBridge ())
            {
                controller->getBridge ()->pushPreview (note, 0.8f);
                previewNote = note;
            }
            break;
        }
    }
    e.consumed = true;
}

void WaveformView::onMouseMoveEvent (MouseMoveEvent& e)
{
    auto s = sample ();
    const CPoint p = e.mousePosition;
    if (drag == Handle::None)
    {
        // hover feedback for slices
        const int mode = (int)std::lround (host->plainValue (kMode));
        int hs = -1;
        if (s && mode == kModeSlicing && waveArea ().pointInside (p))
        {
            SliceList sl;
            computeSlicesForDisplay (sl, *s);
            const double pos = xToPos (p.x) * s->length;
            for (int i = sl.count - 1; i >= 0; --i)
                if (pos >= sl.pos[i])
                {
                    double fs, fe, a, b, c;
                    markerPositions (*s, fs, fe, a, b, c);
                    hs = pos < fe ? i : -1;
                    break;
                }
        }
        if (hs != hoverSlice)
        {
            hoverSlice = hs;
            invalid ();
        }
        return;
    }
    if (!s)
        return;
    if (std::fabs (p.x - dragStartPoint.x) > 2 || std::fabs (p.y - dragStartPoint.y) > 2)
        moved = true;
    const double len = s->length;
    const double pos = std::clamp (xToPos (p.x), 0.0, 1.0);
    double fs, fe, rs, re, ls;
    markerPositions (*s, fs, fe, rs, re, ls);
    const double minGap = 64.0 / len;
    switch (drag)
    {
        case Handle::FlagStart: host->setNorm (kSampleStart, std::min (pos, fe / len - minGap)); break;
        case Handle::FlagEnd: host->setNorm (kSampleEnd, std::max (pos, fs / len + minGap)); break;
        case Handle::Start: host->setNorm (kStart, std::clamp ((pos * len - fs) / (fe - fs), 0.0, 1.0)); break;
        case Handle::LengthEnd: host->setNorm (kLength, std::clamp ((pos * len - rs) / (fe - fs), 0.0, 1.0)); break;
        case Handle::LoopStart:
            host->setNorm (kLoopLen, std::clamp ((re - pos * len) / std::max (1.0, re - rs), 0.0, 1.0));
            break;
        case Handle::LoopBody:
        {
            // move the whole loop: its end is the playback end, its length stays the same
            const double span = std::max (1.0, fe - fs);
            const double newRe = std::clamp (loopDragRe + (pos * len - loopDragDownPos), rs + loopDragLen, fe);
            host->setNorm (kLength, std::clamp ((newRe - rs) / span, 0.0, 1.0));
            host->setNorm (kLoopLen, std::clamp (loopDragLen / std::max (1.0, newRe - rs), 0.0, 1.0));
            break;
        }
        case Handle::Slice: dragSlicePos = std::clamp (pos, fs / len, fe / len); break;
        case Handle::Ruler:
        {
            const CRect w = waveArea ();
            const double dx = p.x - lastPoint.x, dy = p.y - lastPoint.y;
            const double anchor = xToPos (dragStartPoint.x);
            viewStart -= dx / w.getWidth () * viewLen;
            const double factor = std::exp (dy * 0.012);
            const double newLen = std::clamp (viewLen * factor, std::max (1e-6, 32.0 / len), 1.0);
            const double frac = (anchor - viewStart) / viewLen;
            viewStart = anchor - frac * newLen;
            viewLen = newLen;
            viewStart = std::clamp (viewStart, 0.0, 1.0 - viewLen);
            break;
        }
        default: break;
    }
    lastPoint = p;
    invalid ();
    e.consumed = true;
}

void WaveformView::onMouseUpEvent (MouseUpEvent& e)
{
    switch (drag)
    {
        case Handle::FlagStart: host->endEdit (kSampleStart); break;
        case Handle::FlagEnd: host->endEdit (kSampleEnd); break;
        case Handle::Start: host->endEdit (kStart); break;
        case Handle::LengthEnd: host->endEdit (kLength); break;
        case Handle::LoopStart: host->endEdit (kLoopLen); break;
        case Handle::LoopBody:
            host->endEdit (kLength);
            host->endEdit (kLoopLen);
            if (!moved) // a click toggles looping
                host->setOnce (kLoopOn, host->plainValue (kLoopOn) >= 0.5 ? 0.0 : 1.0);
            break;
        case Handle::Slice:
            if (moved)
                commitSliceMove (dragSlicePos);
            break;
        case Handle::Preview: stopPreview (); break;
        default: break;
    }
    drag = Handle::None;
    dragSlice = -1;
    invalid ();
    e.consumed = true;
}

void WaveformView::onMouseCancelEvent (MouseCancelEvent& e)
{
    MouseUpEvent up;
    onMouseUpEvent (up);
    e.consumed = true;
}

void WaveformView::onMouseWheelEvent (MouseWheelEvent& e)
{
    auto s = sample ();
    if (!s)
        return;
    const double anchor = xToPos (e.mousePosition.x);
    if (e.modifiers.has (ModifierKey::Control) || e.modifiers.has (ModifierKey::Alt))
    {
        const double factor = std::exp (-e.deltaY * 0.08);
        const double newLen = std::clamp (viewLen * factor, std::max (1e-6, 32.0 / s->length), 1.0);
        const double frac = (anchor - viewStart) / viewLen;
        viewStart = anchor - frac * newLen;
        viewLen = newLen;
    }
    else
    {
        const double d = std::fabs (e.deltaX) > std::fabs (e.deltaY) ? e.deltaX : e.deltaY;
        viewStart -= d * viewLen * 0.02;
    }
    viewStart = std::clamp (viewStart, 0.0, 1.0 - viewLen);
    invalid ();
    e.consumed = true;
}

void WaveformView::stopPreview ()
{
    if (previewNote >= 0 && controller->getBridge ())
        controller->getBridge ()->pushPreview (previewNote, 0.0f);
    previewNote = -1;
}

void WaveformView::addManualSlice (double pos)
{
    auto* b = controller->getBridge ();
    if (!b)
        return;
    SliceEdits e;
    if (auto cur = b->editsNow ())
        e = *cur;
    e.manual.push_back (pos);
    b->setEdits (e);
    controller->markDirty ();
}

void WaveformView::removeSlice (int index)
{
    auto s = sample ();
    auto* b = controller->getBridge ();
    if (!s || !b)
        return;
    SliceList sl;
    computeSlicesForDisplay (sl, *s);
    if (index <= 0 || index >= sl.count)
        return;
    SliceEdits e;
    if (auto cur = b->editsNow ())
        e = *cur;
    const double tol = (double)sliceMatchTolerance (*s) / s->length;
    const double pos = sl.pos[index] / (double)s->length;
    if (sl.manual[index])
        e.manual.erase (std::remove_if (e.manual.begin (), e.manual.end (),
                                        [&] (double m) { return std::fabs (m - pos) <= tol; }),
                        e.manual.end ());
    // also suppress any automatic slice at the same spot
    e.suppressed.push_back (pos);
    b->setEdits (e);
    controller->markDirty ();
}

void WaveformView::toggleSliceKind (int index)
{
    auto s = sample ();
    auto* b = controller->getBridge ();
    if (!s || !b)
        return;
    SliceList sl;
    computeSlicesForDisplay (sl, *s);
    if (index <= 0 || index >= sl.count)
        return;
    SliceEdits e;
    if (auto cur = b->editsNow ())
        e = *cur;
    const double tol = (double)sliceMatchTolerance (*s) / s->length;
    const double pos = sl.pos[index] / (double)s->length;
    auto nearPos = [&] (double m) { return std::fabs (m - pos) <= tol; };
    if (sl.manual[index])
    {
        // manual -> automatic: drop the manual entry and un-suppress
        e.manual.erase (std::remove_if (e.manual.begin (), e.manual.end (), nearPos), e.manual.end ());
        e.suppressed.erase (std::remove_if (e.suppressed.begin (), e.suppressed.end (), nearPos), e.suppressed.end ());
    }
    else
        e.manual.push_back (pos); // automatic -> manual: kept regardless of sensitivity
    b->setEdits (e);
    controller->markDirty ();
}

void WaveformView::commitSliceMove (double newPos)
{
    auto s = sample ();
    auto* b = controller->getBridge ();
    if (!s || !b)
        return;
    SliceEdits e;
    if (auto cur = b->editsNow ())
        e = *cur;
    const double tol = (double)sliceMatchTolerance (*s) / s->length;
    auto nearOrig = [&] (double m) { return std::fabs (m - dragSliceOrigPos) <= tol; };
    e.manual.erase (std::remove_if (e.manual.begin (), e.manual.end (), nearOrig), e.manual.end ());
    if (!dragSliceManual)
        e.suppressed.push_back (dragSliceOrigPos);
    e.manual.push_back (newPos);
    b->setEdits (e);
    controller->markDirty ();
}

void WaveformView::idle ()
{
    auto* b = controller->getBridge ();
    if (!b)
        return;
    bool changed = false;
    const uint32_t cc = b->changeCounter.load ();
    if (cc != lastChange)
    {
        lastChange = cc;
        changed = true;
    }
    const int n = std::min (b->numPlayheads.load (std::memory_order_acquire), Bridge::kMaxPlayheads);
    if (n != lastHeadCount)
        changed = true;
    for (int i = 0; i < n; ++i)
    {
        const float v = b->playheads[(size_t)i].load (std::memory_order_relaxed);
        if (std::fabs (v - lastHeads[i]) > 1e-6f)
            changed = true;
        lastHeads[i] = v;
    }
    lastHeadCount = n;
    if (changed)
        invalid ();
}

} // namespace simplr

#include "ClipView.h"

#include "Session.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/idatapackage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace stretchr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
constexpr double kRuler = 18.0;
constexpr double kProgress = 4.0;
constexpr double kGrab = 6.0;
const CColor kStretchTint (70, 150, 255);
const CColor kSqueezeTint (255, 164, 40);
const CColor kPitchLine (120, 210, 140);
const CColor kRec (230, 70, 60);

class FileDropTarget : public DropTargetAdapter, public NonAtomicReferenceCounted
{
public:
    explicit FileDropTarget (ClipView* v) : view (v) {}
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
        return firstPath (d, p) && smempler::isSupportedAudioFile (p);
    }
    ClipView* view;
};

void drawText (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size,
               CHoriTxtAlign a = kLeftText, bool bold = false)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, a, true);
}

std::string formatTime (double t, double step)
{
    const bool neg = t < -1e-9;
    t = std::fabs (t);
    const int m = (int)(t / 60.0);
    const double s = t - m * 60.0;
    char buf[32];
    if (step >= 1.0)
        std::snprintf (buf, sizeof (buf), "%s%d:%02d", neg ? "-" : "", m, (int)std::lround (s) % 60);
    else if (step >= 0.1)
        std::snprintf (buf, sizeof (buf), "%s%d:%04.1f", neg ? "-" : "", m, s);
    else
        std::snprintf (buf, sizeof (buf), "%s%d:%05.2f", neg ? "-" : "", m, s);
    return buf;
}

CColor withAlpha (CColor c, uint8_t a)
{
    c.alpha = a;
    return c;
}
} // namespace

ClipView::ClipView (const CRect& r, Controller* c) : CView (r), controller (c)
{
    dropTarget = makeOwned<FileDropTarget> (this);
}

SharedPointer<IDropTarget> ClipView::getDropTarget () { return dropTarget; }

Session* ClipView::session () const { return controller->getSession (); }

void ClipView::setMode (Mode m)
{
    editMode = m;
    hoverIndex = -1;
    invalid ();
}

void ClipView::zoomToFit ()
{
    viewLen = 0.0;
    invalid ();
}

TimeMap ClipView::timeMap (const Clip& c) const
{
    const double speed = session () ? session ()->settings ().speed : 1.0;
    return TimeMap (c.markers.size () >= 2 ? c.markers : identityMarkers (std::max (c.srcLength (), 1.0)),
                    1.0 / std::max (speed, 1e-3));
}

CRect ClipView::plotArea () const
{
    CRect r = getViewSize ();
    r.top += kRuler;
    r.bottom -= kProgress;
    r.left += 1;
    r.right -= 1;
    return r;
}

void ClipView::viewRange (double outLength, double& start, double& len) const
{
    if (viewLen > 0.0)
    {
        start = viewStart;
        len = viewLen;
        return;
    }
    const double L = std::max (outLength, 0.25);
    start = -0.02 * L;
    len = 1.06 * L;
}

double ClipView::timeToX (double t) const
{
    double s, l;
    viewRange (lastOutLen, s, l);
    const CRect p = plotArea ();
    return p.left + (t - s) / l * p.getWidth ();
}

double ClipView::xToTime (double x) const
{
    double s, l;
    viewRange (lastOutLen, s, l);
    const CRect p = plotArea ();
    return s + (x - p.left) / p.getWidth () * l;
}

double ClipView::semisToY (double semis) const
{
    const CRect p = plotArea ();
    const double mid = p.getCenter ().y, half = p.getHeight () * 0.5 - 8.0;
    return mid - semis / kMaxPitchEnvelope * half;
}

double ClipView::yToSemis (double y, bool fine) const
{
    const CRect p = plotArea ();
    const double mid = p.getCenter ().y, half = p.getHeight () * 0.5 - 8.0;
    double s = std::clamp ((mid - y) / half * kMaxPitchEnvelope, -kMaxPitchEnvelope, kMaxPitchEnvelope);
    return fine ? std::round (s * 100.0) / 100.0 : std::round (s);
}

int ClipView::markerAt (const Clip& c, const TimeMap& m, double x) const
{
    int best = -1;
    double bestD = kGrab;
    for (size_t i = 1; i < c.markers.size (); ++i)
    {
        const double d = std::fabs (timeToX (m.outAt (c.markers[i].src)) - x);
        if (d <= bestD)
        {
            bestD = d;
            best = (int)i;
        }
    }
    return best;
}

int ClipView::pitchPointAt (const Clip& c, const TimeMap& m, CPoint p) const
{
    int best = -1;
    double bestD = 7.0;
    for (size_t i = 0; i < c.pitch.size (); ++i)
    {
        const double d = std::hypot (timeToX (m.outAt (c.pitch[i].src)) - p.x, semisToY (c.pitch[i].semis) - p.y);
        if (d <= bestD)
        {
            bestD = d;
            best = (int)i;
        }
    }
    return best;
}

//==============================================================================

void ClipView::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
    ctx->setFillColor (theme::kWaveBg);
    ctx->drawRect (r, kDrawFilled);
    ctx->setFillColor (theme::kHeader);
    ctx->drawRect (CRect (r.left, r.top, r.right, r.top + kRuler), kDrawFilled);
    const CRect p = plotArea ();

    Session* s = session ();
    const Clip c = s ? s->clip () : Clip {};
    const int cap = s ? s->captureState () : 0;

    if (c.empty ())
    {
        lastOutLen = 1.0;
        std::string msg = "Click Capture and start playback to record this track, or drop an audio file here";
        if (cap == Session::kArmed)
            msg = "Armed: start playback in the host to capture";
        drawText (ctx, msg, p, cap ? kRec : theme::kTextDim, 12.0, kCenterText);
        if (cap == Session::kRecording)
        {
            char buf[64];
            std::snprintf (buf, sizeof (buf), "Recording  %s", formatTime (s->capturedSeconds (), 0.1).c_str ());
            drawText (ctx, buf, p, kRec, 14.0, kCenterText, true);
        }
        ctx->setFrameColor (cap ? kRec : theme::kPanelEdge);
        ctx->setLineWidth (1.0);
        ctx->drawRect (r, kDrawStroked);
        return;
    }

    const TimeMap map = timeMap (c);
    const double outLen = map.outLength ();
    lastOutLen = outLen;
    double vs, vl;
    viewRange (outLen, vs, vl);

    // ruler
    {
        const double pxPerSec = p.getWidth () / vl;
        static const double steps[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300};
        double step = steps[0];
        for (double st : steps)
        {
            step = st;
            if (st * pxPerSec >= 70.0)
                break;
        }
        ctx->setLineWidth (1.0);
        for (double t = std::floor (vs / step) * step; t <= vs + vl; t += step)
        {
            const double x = timeToX (t);
            if (x < p.left || x > p.right)
                continue;
            ctx->setFrameColor (theme::kGrid);
            ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
            ctx->setFrameColor (theme::kTextDim);
            ctx->drawLine (CPoint (x, r.top + kRuler - 5), CPoint (x, r.top + kRuler));
            drawText (ctx, formatTime (t, step), CRect (x + 3, r.top + 1, x + 80, r.top + kRuler - 2), theme::kTextDim,
                      9.5);
        }
    }

    // segment tints: blue where the audio is slowed down, orange where it is sped up
    for (size_t i = 0; i + 1 < c.markers.size (); ++i)
    {
        const double t0 = map.outAt (c.markers[i].src), t1 = map.outAt (c.markers[i + 1].src);
        const double st = (t1 - t0) / (c.markers[i + 1].src - c.markers[i].src);
        const double x0 = std::max (p.left, timeToX (t0)), x1 = std::min (p.right, timeToX (t1));
        if (x1 <= x0 || std::fabs (std::log (st)) < 0.01)
            continue;
        const double amount = std::min (1.0, std::fabs (std::log2 (st)) / 2.0);
        ctx->setFillColor (withAlpha (st > 1.0 ? kStretchTint : kSqueezeTint, (uint8_t)(14 + 40 * amount)));
        ctx->drawRect (CRect (x0, p.top, x1, p.bottom), kDrawFilled);
    }

    // outside the clip
    {
        ctx->setFillColor (CColor (0, 0, 0, 90));
        const double xs = timeToX (0.0), xe = timeToX (outLen);
        if (xs > p.left)
            ctx->drawRect (CRect (p.left, p.top, std::min (xs, p.right), p.bottom), kDrawFilled);
        if (xe < p.right)
            ctx->drawRect (CRect (std::max (xe, p.left), p.top, p.right, p.bottom), kDrawFilled);
    }

    // waveform
    {
        const SampleData& a = *c.audio;
        const double sr = a.sampleRate;
        const double mid = p.getCenter ().y, half = p.getHeight () * 0.5 - 6.0;
        const double norm = 0.95 / std::max (0.02f, a.peakAbs);
        const int bs = a.peaks.blockSize;
        ctx->setFrameColor (editMode == kPitchMode ? withAlpha (theme::kWave, 120) : theme::kWave);
        ctx->setLineWidth (1.0);
        for (double x = std::floor (p.left); x < p.right; x += 1.0)
        {
            const double t0 = xToTime (x), t1 = xToTime (x + 1.0);
            if (t1 <= 0.0 || t0 >= outLen)
                continue;
            const double s0 = map.srcAt (std::max (0.0, t0)) * sr, s1 = map.srcAt (std::min (outLen, t1)) * sr;
            int ia = std::clamp ((int)std::floor (s0), 0, a.length - 1);
            int ib = std::clamp ((int)std::ceil (s1), ia + 1, a.length);
            float lo = 0.0f, hi = 0.0f;
            bool first = true;
            for (int ch = 0; ch < a.numChannels; ++ch)
            {
                if (bs > 0 && ib - ia >= bs * 2 && !a.peaks.mn[ch].empty ())
                {
                    const int ba = ia / bs, bb = std::min ((int)a.peaks.mn[ch].size (), (ib + bs - 1) / bs);
                    for (int k = ba; k < bb; ++k)
                    {
                        lo = first ? a.peaks.mn[ch][(size_t)k] : std::min (lo, a.peaks.mn[ch][(size_t)k]);
                        hi = first ? a.peaks.mx[ch][(size_t)k] : std::max (hi, a.peaks.mx[ch][(size_t)k]);
                        first = false;
                    }
                }
                else
                    for (int k = ia; k < ib; ++k)
                    {
                        const float v = a.ch[ch][(size_t)k];
                        lo = first ? v : std::min (lo, v);
                        hi = first ? v : std::max (hi, v);
                        first = false;
                    }
            }
            const double y0 = mid - hi * norm * half, y1 = mid - lo * norm * half;
            ctx->drawLine (CPoint (x + 0.5, y0), CPoint (x + 0.5, std::max (y1, y0 + 1.0)));
        }
    }

    // stretch markers
    const bool stretchMode = editMode == kStretchMode;
    for (size_t i = 0; i < c.markers.size (); ++i)
    {
        const double x = timeToX (map.outAt (c.markers[i].src));
        if (x < p.left - 8 || x > p.right + 8)
            continue;
        const bool hot = stretchMode && (int)i == (drag == Drag::Marker ? dragIndex : hoverIndex);
        CColor col = i == 0 ? theme::kTextDim : (hot ? theme::kAccent : theme::kTextBright);
        if (!stretchMode)
            col = withAlpha (col, 90);
        ctx->setFrameColor (col);
        ctx->setLineWidth (hot ? 2.0 : 1.0);
        ctx->drawLine (CPoint (x, p.top), CPoint (x, p.bottom));
        ctx->setFillColor (col);
        ctx->drawPolygon ({CPoint (x - 5, p.top), CPoint (x + 5, p.top), CPoint (x, p.top + 8)}, kDrawFilled);
    }
    // speed of each segment
    if (stretchMode)
        for (size_t i = 0; i + 1 < c.markers.size (); ++i)
        {
            const double t0 = map.outAt (c.markers[i].src), t1 = map.outAt (c.markers[i + 1].src);
            const double x0 = timeToX (t0), x1 = timeToX (t1);
            if (x1 - x0 < 44.0 || x1 < p.left || x0 > p.right)
                continue;
            const double speed = 100.0 * (c.markers[i + 1].src - c.markers[i].src) / (t1 - t0);
            char buf[32];
            std::snprintf (buf, sizeof (buf), speed < 10.0 ? "%.1f %%" : "%.0f %%", speed);
            const double cx = std::clamp ((x0 + x1) * 0.5, p.left + 24.0, p.right - 24.0);
            drawText (ctx, buf, CRect (cx - 40, p.top + 2, cx + 40, p.top + 16),
                      std::fabs (speed - 100.0) < 0.5 ? theme::kTextDim : theme::kTextBright, 10.0, kCenterText);
        }

    // pitch envelope
    if (editMode == kPitchMode || !c.pitch.empty ())
    {
        const CColor line = editMode == kPitchMode ? kPitchLine : withAlpha (kPitchLine, 110);
        if (editMode == kPitchMode)
        {
            for (double g : {-24.0, -12.0, 12.0, 24.0})
            {
                ctx->setFrameColor (theme::kGrid);
                ctx->setLineWidth (1.0);
                const double y = semisToY (g);
                ctx->drawLine (CPoint (p.left, y), CPoint (p.right, y));
                char buf[16];
                std::snprintf (buf, sizeof (buf), "%+.0f", g);
                drawText (ctx, buf, CRect (p.left + 3, y - 12, p.left + 40, y), theme::kTextDim, 9.0);
            }
            ctx->setFrameColor (withAlpha (kPitchLine, 60));
            ctx->drawLine (CPoint (p.left, semisToY (0.0)), CPoint (p.right, semisToY (0.0)));
        }
        const double xs = std::max (p.left, timeToX (0.0)), xe = std::min (p.right, timeToX (outLen));
        ctx->setFrameColor (line);
        ctx->setLineWidth (2.0);
        CPoint prevPt;
        bool have = false;
        for (double x = xs; x <= xe; x += 2.0)
        {
            const double t = std::clamp (xToTime (x), 0.0, outLen);
            const CPoint pt (x, semisToY (pitchAt (c.pitch, map.srcAt (t))));
            if (have)
                ctx->drawLine (prevPt, pt);
            prevPt = pt;
            have = true;
        }
        if (editMode == kPitchMode)
            for (size_t i = 0; i < c.pitch.size (); ++i)
            {
                const CPoint pt (timeToX (map.outAt (c.pitch[i].src)), semisToY (c.pitch[i].semis));
                if (pt.x < p.left - 6 || pt.x > p.right + 6)
                    continue;
                const bool hot = (int)i == (drag == Drag::Point ? dragIndex : hoverIndex);
                ctx->setFillColor (hot ? theme::kAccent : kPitchLine);
                ctx->drawEllipse (CRect (pt.x - 4.5, pt.y - 4.5, pt.x + 4.5, pt.y + 4.5), kDrawFilled);
                if (hot)
                {
                    char buf[32];
                    std::snprintf (buf, sizeof (buf), "%+.2f st", c.pitch[i].semis);
                    drawText (ctx, buf, CRect (pt.x + 8, pt.y - 20, pt.x + 90, pt.y - 6), theme::kTextBright, 10.0);
                }
            }
    }

    // playhead
    if (s && s->playing.load ())
    {
        const double off = s->playOffset.load ();
        const double t = off >= 0.0 ? off : s->transport.load () - c.start;
        const double x = timeToX (t);
        if (t >= 0.0 && t <= outLen && x >= p.left && x <= p.right)
        {
            ctx->setFrameColor (theme::kPlayhead);
            ctx->setLineWidth (1.5);
            ctx->drawLine (CPoint (x, r.top), CPoint (x, p.bottom));
        }
    }

    // render progress / capture
    if (s && s->rendering.load ())
    {
        ctx->setFillColor (theme::kAccentDim);
        ctx->drawRect (CRect (r.left, r.bottom - kProgress, r.left + r.getWidth () * s->progress.load (), r.bottom),
                       kDrawFilled);
    }
    if (cap == Session::kArmed || cap == Session::kRecording)
    {
        char buf[64];
        if (cap == Session::kArmed)
            std::snprintf (buf, sizeof (buf), "Armed: start playback to capture (replaces this clip)");
        else
            std::snprintf (buf, sizeof (buf), "Recording  %s", formatTime (s->capturedSeconds (), 0.1).c_str ());
        drawText (ctx, buf, CRect (p.left, p.bottom - 22, p.right - 8, p.bottom - 4), kRec, 11.0, kRightText, true);
    }
    ctx->setFrameColor (cap ? kRec : theme::kPanelEdge);
    ctx->setLineWidth (1.0);
    ctx->drawRect (r, kDrawStroked);
}

//==============================================================================

void ClipView::onMouseDownEvent (MouseDownEvent& e)
{
    const CPoint pos = e.mousePosition;
    if (e.buttonState.isRight ())
    {
        if (onContextMenu)
        {
            CPoint fp = pos;
            localToFrame (fp);
            onContextMenu (fp);
        }
        e.consumed = true;
        return;
    }
    if (!e.buttonState.isLeft ())
        return;
    Session* s = session ();
    if (!s || !s->hasClip ())
        return;
    const Clip c = s->clip ();
    const TimeMap map = timeMap (c);
    const double outLen = map.outLength ();
    lastOutLen = outLen;
    const CRect p = plotArea ();
    e.consumed = true;

    // freeze the visible range while dragging, so it doesn't rescale under the mouse
    fitWhileDragging = viewLen <= 0.0;
    if (fitWhileDragging)
        viewRange (outLen, viewStart, viewLen);
    lastX = pos.x;

    if (pos.y < p.top) // ruler
    {
        if (e.clickCount == 2)
        {
            zoomToFit ();
            fitWhileDragging = false;
            e.ignoreFollowUpMoveAndUpEvents (true);
            return;
        }
        drag = Drag::Pan;
        return;
    }

    const bool fine = e.modifiers.has (ModifierKey::Shift);
    if (editMode == kStretchMode)
    {
        const int idx = markerAt (c, map, pos.x);
        if (e.clickCount == 2)
        {
            if (idx > 0 && idx + 1 < (int)c.markers.size ())
                s->edit ([idx] (Clip& k) { k.markers.erase (k.markers.begin () + idx); });
            else if (idx < 0)
            {
                const double t = xToTime (pos.x);
                if (t > 0.0 && t < outLen)
                {
                    const StretchMarker m {map.srcAt (t), t / map.scale ()};
                    s->edit ([m] (Clip& k) { k.markers.push_back (m); });
                }
            }
            restoreFit ();
            e.ignoreFollowUpMoveAndUpEvents (true);
            invalid ();
            return;
        }
        if (idx > 0)
        {
            drag = Drag::Marker;
            dragIndex = idx;
            dragStartValue = map.outAt (c.markers[(size_t)idx].src);
            s->pushUndo ();
            invalid ();
            return;
        }
    }
    else
    {
        const int idx = pitchPointAt (c, map, pos);
        if (e.clickCount == 2)
        {
            if (idx >= 0)
                s->edit ([idx] (Clip& k) { k.pitch.erase (k.pitch.begin () + idx); });
            else
            {
                const double t = std::clamp (xToTime (pos.x), 0.0, outLen);
                const PitchPoint pt {map.srcAt (t), yToSemis (pos.y, fine)};
                s->edit ([pt] (Clip& k) {
                    auto it = std::upper_bound (k.pitch.begin (), k.pitch.end (), pt.src,
                                                [] (double v, const PitchPoint& q) { return v < q.src; });
                    k.pitch.insert (it, pt);
                });
            }
            restoreFit ();
            e.ignoreFollowUpMoveAndUpEvents (true);
            invalid ();
            return;
        }
        if (idx >= 0)
        {
            drag = Drag::Point;
            dragIndex = idx;
            s->pushUndo ();
            invalid ();
            return;
        }
    }
    drag = Drag::Pan;
}

void ClipView::onMouseMoveEvent (MouseMoveEvent& e)
{
    const CPoint pos = e.mousePosition;
    Session* s = session ();
    if (!s)
        return;
    if (drag == Drag::None)
    {
        if (!s->hasClip ())
            return;
        const Clip c = s->clip ();
        const TimeMap map = timeMap (c);
        const int h = editMode == kStretchMode ? markerAt (c, map, pos.x) : pitchPointAt (c, map, pos);
        if (h != hoverIndex)
        {
            hoverIndex = h;
            invalid ();
        }
        if (auto* f = getFrame ())
            f->setCursor (editMode == kStretchMode && h > 0 ? kCursorHSize : kCursorDefault);
        return;
    }
    e.consumed = true;
    const CRect p = plotArea ();
    const double secPerPx = viewLen / p.getWidth ();
    const bool fine = e.modifiers.has (ModifierKey::Shift);
    const double dx = pos.x - lastX;
    lastX = pos.x;
    if (drag == Drag::Pan)
    {
        fitWhileDragging = false; // keep the scrolled view
        viewStart -= dx * secPerPx;
        invalid ();
        return;
    }
    const Clip c = s->clip ();
    const TimeMap map = timeMap (c);
    const double k = map.scale ();
    if (drag == Drag::Marker && dragIndex > 0 && dragIndex < (int)c.markers.size ())
    {
        dragStartValue += dx * secPerPx * (fine ? 0.1 : 1.0);
        const size_t i = (size_t)dragIndex;
        const auto& m = c.markers[i];
        const auto& a = c.markers[i - 1];
        double lo = a.dst + (m.src - a.src) * kMinSegmentStretch;
        double hi = a.dst + (m.src - a.src) * kMaxSegmentStretch;
        if (i + 1 < c.markers.size ())
        {
            const auto& b = c.markers[i + 1];
            lo = std::max (lo, b.dst - (b.src - m.src) * kMaxSegmentStretch);
            hi = std::min (hi, b.dst - (b.src - m.src) * kMinSegmentStretch);
        }
        const double dst = std::clamp (dragStartValue / k, lo, std::max (lo, hi));
        s->edit ([i, dst] (Clip& cl) {
            if (i < cl.markers.size ())
                cl.markers[i].dst = dst;
        },
                 false);
    }
    else if (drag == Drag::Point && dragIndex >= 0 && dragIndex < (int)c.pitch.size ())
    {
        const size_t i = (size_t)dragIndex;
        const double outLen = map.outLength ();
        double src = map.srcAt (std::clamp (xToTime (pos.x), 0.0, outLen));
        if (i > 0)
            src = std::max (src, c.pitch[i - 1].src);
        if (i + 1 < c.pitch.size ())
            src = std::min (src, c.pitch[i + 1].src);
        const double semis = yToSemis (pos.y, fine);
        s->edit ([i, src, semis] (Clip& cl) {
            if (i < cl.pitch.size ())
                cl.pitch[i] = {src, semis};
        },
                 false);
    }
    invalid ();
}

void ClipView::restoreFit ()
{
    if (fitWhileDragging)
        viewLen = 0.0;
    fitWhileDragging = false;
}

void ClipView::onMouseUpEvent (MouseUpEvent& e)
{
    if (drag != Drag::None)
        e.consumed = true;
    drag = Drag::None;
    dragIndex = -1;
    restoreFit ();
    invalid ();
}

void ClipView::onMouseCancelEvent (MouseCancelEvent& e)
{
    drag = Drag::None;
    dragIndex = -1;
    restoreFit ();
    invalid ();
    e.consumed = true;
}

void ClipView::onMouseExitEvent (MouseExitEvent& e)
{
    if (hoverIndex != -1)
    {
        hoverIndex = -1;
        invalid ();
    }
    if (auto* f = getFrame ())
        f->setCursor (kCursorDefault);
    e.consumed = true;
}

void ClipView::onMouseWheelEvent (MouseWheelEvent& e)
{
    Session* s = session ();
    if (!s || !s->hasClip ())
        return;
    const Clip c = s->clip ();
    const double outLen = timeMap (c).outLength ();
    lastOutLen = outLen;
    if (viewLen <= 0.0)
        viewRange (outLen, viewStart, viewLen);
    const double anchor = xToTime (e.mousePosition.x);
    if (e.modifiers.has (ModifierKey::Control) || e.modifiers.has (ModifierKey::Alt))
    {
        const double factor = std::exp (-e.deltaY * 0.08);
        const double newLen = std::clamp (viewLen * factor, 0.01, std::max (outLen * 4.0, 1.0));
        const double frac = (anchor - viewStart) / viewLen;
        viewStart = anchor - frac * newLen;
        viewLen = newLen;
    }
    else
    {
        const double d = std::fabs (e.deltaX) > std::fabs (e.deltaY) ? e.deltaX : e.deltaY;
        viewStart -= d * viewLen * 0.02;
    }
    invalid ();
    e.consumed = true;
}

void ClipView::idle ()
{
    Session* s = session ();
    if (!s)
        return;
    bool dirty = false;
    const uint64_t v = s->clipVersion ();
    if (v != lastVersion)
    {
        lastVersion = v;
        dirty = true;
    }
    const bool playing = s->playing.load ();
    if (playing || playing != wasPlaying)
    {
        const double off = s->playOffset.load ();
        const double x = timeToX (off >= 0.0 ? off : s->transport.load () - s->clipStart ());
        if (std::fabs (x - lastPlayX) >= 1.0 || playing != wasPlaying)
            dirty = true;
        lastPlayX = x;
    }
    wasPlaying = playing;
    const bool rendering = s->rendering.load ();
    if (rendering || rendering != wasRendering)
        dirty = true;
    wasRendering = rendering;
    const int cap = s->captureState ();
    if (cap != lastCapture || cap == Session::kRecording)
        dirty = true;
    lastCapture = cap;
    if (dirty)
        invalid ();
}

} // namespace stretchr

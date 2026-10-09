#include "pluginkit/ui/CaptureView.h"

#include "pluginkit/WavFile.h"
#include "pluginkit/Wavetable.h"
#include "pluginkit/ui/InfoBox.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cdropsource.h"
#include "vstgui/lib/dragging.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace pk {

using namespace VSTGUI;

namespace {

const char* const kScopeHelp =
    "The output over the capture's length: 1, 2 or 4 bars while the host plays (ending on the last bar line, "
    "so it loops), else 1, 2 or 4 seconds. Drag it onto a track in your DAW to drop it there as audio (a "
    "32-bit float WAV); right click to save it, or a wavetable of it, to a file.";
const char* const kLengthHelp = "How much the scope holds: 1, 2 or 4 bars at the host's tempo while it plays, else 1, 2 or 4 seconds.";
const char* const kFreezeHelp = "Holds what the scope shows (the audio dragged or saved stays this until you switch Freeze off).";
const char* const kDragWavHelp = "Drag onto a track in your DAW: the audio the scope shows, as a stereo 32-bit float WAV at the host's rate.";
const char* const kDragTableHelp =
    "Drag into a wavetable synth (Serum, Vital, Ableton's Wavetable) or a track: the audio as a wavetable, single "
    "cycles of 2048 samples (up to 256 of them, evenly spaced) at its pitch, in a WAV with Serum's clm chunk.";

std::filesystem::path fromUtf8 (const std::string& s) { return std::filesystem::path (std::u8string (s.begin (), s.end ())); }
std::string toUtf8 (const std::filesystem::path& p)
{
    const auto u = p.u8string ();
    return std::string (u.begin (), u.end ());
}

// <temp>/bfielstr captures/<name> <what> <date time>.wav
std::string tempFile (const std::string& name, const char* what)
{
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path (ec) / "bfielstr captures";
    std::filesystem::create_directories (dir, ec);
    const auto now = std::chrono::system_clock::now ();
    const std::time_t t = std::chrono::system_clock::to_time_t (now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (now.time_since_epoch ()).count () % 1000;
    char stamp[64];
    std::strftime (stamp, sizeof (stamp), "%Y-%m-%d %H%M%S", std::localtime (&t));
    char file[160];
    std::snprintf (file, sizeof (file), "%s %s %s.%03d.wav", name.c_str (), what, stamp, (int)ms);
    return toUtf8 (dir / fromUtf8 (file));
}

// A handle dragged out of the plug-in: on the drag's start `make` writes the file it drops.
class DragHandle : public CView
{
public:
    DragHandle (const CRect& r, std::string text, std::function<std::string (std::string&)> make, std::function<void (const std::string&)> failed)
    : CView (r), text (std::move (text)), make (std::move (make)), failed (std::move (failed))
    {
    }
    void draw (CDrawContext* ctx) override
    {
        const CRect r = getViewSize ();
        draw::outline (ctx, r, pressed ? theme::kEnergyLive : theme::kCopper);
        // grip dots
        ctx->setFillColor (theme::kCopperPale);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 2; ++j)
                ctx->drawRect (CRect (r.left + 6 + j * 4, r.top + r.getHeight () / 2 - 5 + i * 4, r.left + 8 + j * 4, r.top + r.getHeight () / 2 - 3 + i * 4),
                               kDrawFilled);
        ctx->setFont (theme::font (10.5));
        ctx->setFontColor (theme::kText);
        ctx->drawString (text.c_str (), CRect (r.left + 14, r.top, r.right, r.bottom), kCenterText, true);
    }
    void onMouseDownEvent (MouseDownEvent& e) override
    {
        if (!e.buttonState.isLeft ())
            return;
        pressed = true;
        downAt = e.mousePosition;
        invalid ();
        e.consumed = true;
    }
    void onMouseMoveEvent (MouseMoveEvent& e) override
    {
        if (!pressed)
            return;
        e.consumed = true;
        if (std::hypot (e.mousePosition.x - downAt.x, e.mousePosition.y - downAt.y) < 4.0)
            return;
        pressed = false;
        invalid ();
        std::string err;
        const std::string path = make ? make (err) : std::string ();
        if (path.empty ())
        {
            if (failed)
                failed (err);
            return;
        }
        auto src = CDropSource::create (path.c_str (), (uint32_t)path.size () + 1, IDataPackage::kFilePath);
        doDrag (DragDescription (src));
    }
    void onMouseUpEvent (MouseUpEvent& e) override
    {
        pressed = false;
        invalid ();
        e.consumed = true;
    }
    const std::string& label () const { return text; }

private:
    std::string text;
    std::function<std::string (std::string&)> make;
    std::function<void (const std::string&)> failed;
    CPoint downAt;
    bool pressed = false;
};

} // namespace

// ---- the scope ---------------------------------------------------------------------------------------
class CaptureScope : public CView
{
public:
    CaptureScope (const CRect& r, CaptureBand* b) : CView (r), band (b) {}

    void draw (CDrawContext* ctx) override
    {
        const CRect r = getViewSize ();
        ctx->setFillColor (theme::kWell);
        ctx->drawRect (r, kDrawFilled);
        const CRect wave (r.left + 1, r.top + 18, r.right - 1, r.bottom - 4);
        const int cols = std::max (1, (int)wave.getWidth ());
        // what is shown: the held audio's columns (made once), or the buffer's window now
        const CaptureBuffer* buf = band->source ? band->source () : nullptr;
        bool bars = false;
        double length = CaptureBuffer::lengthOf (band->getChoice ? band->getChoice () : 1);
        mn.assign ((size_t)cols, 0.0f);
        mx.assign ((size_t)cols, 0.0f);
        bool any = false;
        if (band->hold->frozen)
        {
            const auto& h = *band->hold;
            bars = h.bars;
            length = h.length;
            const size_t n = h.l.size ();
            for (int c = 0; c < cols && n > 0; ++c)
            {
                const size_t a = n * (size_t)c / (size_t)cols, b = std::max (a + 1, n * (size_t)(c + 1) / (size_t)cols);
                float lo = h.l[a], hi = h.l[a];
                for (size_t i = a; i < b && i < n; ++i)
                {
                    lo = std::min ({lo, h.l[i], h.r[i]});
                    hi = std::max ({hi, h.l[i], h.r[i]});
                }
                mn[(size_t)c] = lo;
                mx[(size_t)c] = hi;
            }
            any = n > 0;
        }
        else if (buf)
        {
            const auto w = buf->window (band->getChoice ? band->getChoice () : 1);
            bars = w.bars;
            length = w.length;
            buf->readPeaks (w.end, w.frames, cols, mn.data (), mx.data ());
            any = buf->written () > 0;
        }
        // the grid: beats and bars (bars), or seconds
        const int steps = bars ? (int)std::lround (length * 4) : (int)std::lround (length * 4);
        for (int s = 1; s < steps; ++s)
        {
            const double x = std::round (wave.left + wave.getWidth () * s / steps) + 0.5;
            const bool major = s % 4 == 0;
            ctx->setFrameColor (major ? theme::kGridMajor : theme::kGridMinor);
            ctx->drawLine (CPoint (x, wave.top), CPoint (x, wave.bottom));
        }
        const double mid = std::round (wave.getCenter ().y) + 0.5;
        ctx->setFrameColor (theme::kGridZero);
        ctx->drawLine (CPoint (wave.left, mid), CPoint (wave.right, mid));
        // the waveform: each column from its minimum to its maximum (copper; held: cinnabar)
        const double half = wave.getHeight () / 2;
        ctx->setFillColor (band->hold->frozen ? theme::kEnergyLive : theme::kCopper);
        for (int c = 0; c < cols && any; ++c)
        {
            const double top = mid - std::clamp ((double)mx[(size_t)c], -1.0, 1.0) * half;
            const double bottom = mid - std::clamp ((double)mn[(size_t)c], -1.0, 1.0) * half;
            ctx->drawRect (CRect (wave.left + c, std::floor (std::min (top, bottom)), wave.left + c + 1, std::ceil (std::max (top, bottom)) + 1),
                           kDrawFilled);
        }
        // its title and what it holds
        char title[64];
        std::snprintf (title, sizeof (title), "OUTPUT  %g %s%s", length, bars ? (length == 1 ? "bar" : "bars") : "s",
                       band->hold->frozen ? "  HELD" : "");
        ctx->setFont (theme::font (9.5, true));
        ctx->setFontColor (theme::kCopperPale);
        ctx->drawString (title, CRect (r.left + 6, r.top + 2, r.right - 6, r.top + 16), kLeftText, true);
        ctx->setFont (theme::font (9.5));
        ctx->setFontColor (theme::kTextDim);
        ctx->drawString (any ? "drag to a track" : "no output yet", CRect (r.left + 6, r.top + 2, r.right - 6, r.top + 16), kRightText, true);
        draw::outline (ctx, r, theme::kLineDim, 0);
    }

    void idle ()
    {
        const CaptureBuffer* buf = band->source ? band->source () : nullptr;
        const uint64_t w = buf ? buf->written () : 0;
        const int choice = band->getChoice ? band->getChoice () : 1;
        const bool frozen = band->hold->frozen;
        if (frozen != shownFrozen || choice != shownChoice || (!frozen && w != shownWritten))
        {
            shownFrozen = frozen;
            shownChoice = choice;
            shownWritten = w;
            invalid ();
        }
    }

    void onMouseDownEvent (MouseDownEvent& e) override
    {
        if (e.buttonState.isRight ())
        {
            CPoint p = e.mousePosition;
            band->showMenu (p);
            e.consumed = true;
            return;
        }
        if (!e.buttonState.isLeft ())
            return;
        pressed = true;
        downAt = e.mousePosition;
        e.consumed = true;
    }
    void onMouseMoveEvent (MouseMoveEvent& e) override
    {
        if (!pressed)
            return;
        e.consumed = true;
        if (std::hypot (e.mousePosition.x - downAt.x, e.mousePosition.y - downAt.y) < 4.0)
            return;
        pressed = false;
        std::string err;
        const std::string path = band->writeAudio (err);
        if (path.empty ())
        {
            band->say (err);
            return;
        }
        auto src = CDropSource::create (path.c_str (), (uint32_t)path.size () + 1, IDataPackage::kFilePath);
        doDrag (DragDescription (src));
    }
    void onMouseUpEvent (MouseUpEvent& e) override
    {
        pressed = false;
        e.consumed = true;
    }

private:
    CaptureBand* band;
    std::vector<float> mn, mx;
    uint64_t shownWritten = ~0ull;
    int shownChoice = -1;
    bool shownFrozen = false, pressed = false;
    CPoint downAt;
};

// ---- the band ----------------------------------------------------------------------------------------
CaptureBand::CaptureBand (const CRect& r, Source src, std::shared_ptr<CaptureHold> h, std::string n, std::function<int ()> get,
                          std::function<void (int)> set)
: CViewContainer (r), source (std::move (src)), hold (std::move (h)), name (std::move (n)), getChoice (std::move (get)), setChoice (std::move (set))
{
    setTransparency (true);
    // a slim row of controls on top (the status at its left, the controls at its right), the scope as wide
    // as the band under it
    const double w = r.getWidth (), ht = r.getHeight ();
    const double cx = w - kControlsW;
    auto* sc = new CaptureScope (CRect (0, kRowH + 6, w, ht), this);
    setHelp (sc, "Capture", kScopeHelp);
    addView (sc);
    scope = sc;
    // the length (1, 2, 4) and its unit, Freeze
    auto* len = new ViewSwitch (CRect (cx, 0, cx + 96, kRowH), {"1", "2", "4"}, [this] { return getChoice ? getChoice () : 1; },
                                [this] (int c) {
                                    if (setChoice)
                                        setChoice (c);
                                    if (scope)
                                        scope->invalid ();
                                });
    setHelp (len, "Capture Length", kLengthHelp);
    addView (len);
    unit = new Label (CRect (cx + 100, 2, cx + 132, kRowH - 2), "bars", 10.5, false, 0);
    setHelp (unit, "Capture Length", kLengthHelp);
    addView (unit);
    auto* freeze = new ActionButton (CRect (cx + 140, 0, cx + 204, kRowH), "Freeze", [this] { setFrozen (!hold->frozen); },
                                     [this] { return hold->frozen; });
    setHelp (freeze, "Freeze", kFreezeHelp);
    addView (freeze);
    // the drags out
    auto failed = [this] (const std::string& e) { say (e); };
    auto* wav = new DragHandle (CRect (cx + 212, 0, cx + 308, kRowH), "Drag WAV", [this] (std::string& e) { return writeAudio (e); }, failed);
    setHelp (wav, "Drag WAV", kDragWavHelp);
    addView (wav);
    auto* table = new DragHandle (CRect (cx + 316, 0, cx + kControlsW, kRowH), "Drag Wavetable", [this] (std::string& e) { return writeWavetable (e); },
                                  failed);
    setHelp (table, "Drag as Wavetable", kDragTableHelp);
    addView (table);
    status = new Label (CRect (0, 2, cx - 12, kRowH - 2), "right click the scope to save", 10.0, false, 0);
    status->setDim (true);
    addView (status);
}

void CaptureBand::idle ()
{
    if (const CaptureBuffer* buf = source ? source () : nullptr; buf && !buf->enabled ())
        buf->enable (); // (the ring, the first time a page shows it)
    if (auto* s = dynamic_cast<CaptureScope*> (scope))
        s->idle ();
    if (unit)
    {
        const CaptureBuffer* buf = source ? source () : nullptr;
        const bool bars = hold->frozen ? hold->bars : buf && buf->window (getChoice ? getChoice () : 1).bars;
        unit->setText (bars ? "bars" : "s");
    }
}

void CaptureBand::say (const std::string& text)
{
    if (status)
        status->setText (text);
}

void CaptureBand::setFrozen (bool on)
{
    if (on == hold->frozen)
        return;
    if (on)
    {
        const CaptureBuffer* buf = source ? source () : nullptr;
        if (!buf || buf->written () == 0)
        {
            say ("nothing to hold yet");
            return;
        }
        const auto w = buf->window (getChoice ? getChoice () : 1);
        hold->l.assign ((size_t)w.frames, 0.0f);
        hold->r.assign ((size_t)w.frames, 0.0f);
        buf->read (w.end, w.frames, hold->l.data (), hold->r.data ());
        hold->sampleRate = w.sampleRate;
        hold->bars = w.bars;
        hold->length = w.length;
    }
    else
    {
        hold->l.clear ();
        hold->r.clear ();
        hold->l.shrink_to_fit ();
        hold->r.shrink_to_fit ();
    }
    hold->frozen = on;
    say (on ? "held: drag it out or save it" : "right click the scope to save");
    invalid ();
}

bool CaptureBand::audioNow (std::vector<float>& l, std::vector<float>& r, double& sampleRate) const
{
    if (hold->frozen)
    {
        l = hold->l;
        r = hold->r;
        sampleRate = hold->sampleRate;
        return !l.empty ();
    }
    const CaptureBuffer* buf = source ? source () : nullptr;
    if (!buf || buf->written () == 0)
        return false;
    const auto w = buf->window (getChoice ? getChoice () : 1);
    l.assign ((size_t)w.frames, 0.0f);
    r.assign ((size_t)w.frames, 0.0f);
    buf->read (w.end, w.frames, l.data (), r.data ());
    sampleRate = w.sampleRate;
    return true;
}

bool CaptureBand::saveAudio (const std::string& path, std::string& error) const
{
    std::vector<float> l, r;
    double sr = 48000;
    if (!audioNow (l, r, sr))
    {
        error = "no output captured yet";
        return false;
    }
    return wav::write (path, wav::encode ({l.data (), r.data ()}, l.size (), sr), error);
}

bool CaptureBand::saveWavetable (const std::string& path, std::string& error) const
{
    std::vector<float> l, r;
    double sr = 48000;
    if (!audioNow (l, r, sr))
    {
        error = "no output captured yet";
        return false;
    }
    std::vector<float> mono (l.size ());
    for (size_t i = 0; i < l.size (); ++i)
        mono[i] = 0.5f * (l[i] + r[i]);
    const CaptureBuffer* buf = source ? source () : nullptr;
    const auto t = wavetable::make (mono.data (), mono.size (), sr, buf ? buf->noteHz () : 0.0);
    if (!t.error.empty ())
    {
        error = t.error;
        return false;
    }
    return wav::write (path, wav::encode ({t.frames.data ()}, t.frames.size (), sr, wav::clmText (wavetable::kFrameSize)), error);
}

std::string CaptureBand::writeAudio (std::string& error) const
{
    const std::string path = tempFile (name, "capture");
    return saveAudio (path, error) ? path : std::string ();
}

std::string CaptureBand::writeWavetable (std::string& error) const
{
    const std::string path = tempFile (name, "wavetable");
    return saveWavetable (path, error) ? path : std::string ();
}

void CaptureBand::chooseFile (bool wavetable)
{
    auto* f = getFrame ();
    if (!f)
        return;
    auto* sel = CNewFileSelector::create (f, CNewFileSelector::kSelectSaveFile);
    if (!sel)
        return;
    sel->setTitle (wavetable ? "Save Wavetable" : "Save Audio");
    sel->setDefaultExtension (CFileExtension ("WAV", "wav"));
    sel->setDefaultSaveName ((name + (wavetable ? " wavetable.wav" : " capture.wav")).c_str ());
    sel->run ([this, wavetable] (CNewFileSelector* s) {
        if (s->getNumSelectedFiles () == 0)
            return;
        std::string path = s->getSelectedFile (0), err;
        if (fromUtf8 (path).extension ().empty ())
            path += ".wav";
        const bool ok = wavetable ? saveWavetable (path, err) : saveAudio (path, err);
        say (ok ? "saved " + toUtf8 (fromUtf8 (path).filename ()) : err);
    });
    sel->forget ();
}

void CaptureBand::showMenu (CPoint where)
{
    auto* f = getFrame ();
    if (!f)
        return;
    localToFrame (where);
    auto menu = makeOwned<COptionMenu> ();
    menu->addEntry ("Save Audio...");
    menu->addEntry ("Save Wavetable...");
    menu->addSeparator ();
    auto* fr = menu->addEntry ("Freeze");
    fr->setChecked (hold->frozen);
    menu->popup (f, where, [this] (COptionMenu* m) {
        switch (m->getLastResult ())
        {
            case 0: chooseFile (false); break;
            case 1: chooseFile (true); break;
            case 3: setFrozen (!hold->frozen); break;
            default: break;
        }
    });
}

} // namespace pk

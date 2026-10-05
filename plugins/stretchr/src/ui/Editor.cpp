#include "Editor.h"

#include "ClipView.h"
#include "Help.h"
#include "Session.h"
#include "Wav.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cdropsource.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/events.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <vector>

namespace stretchr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::Panel;
using pk::Segmented;
using pk::Toggle;

namespace {
class Background : public CViewContainer
{
public:
    using CViewContainer::CViewContainer;
    void drawBackgroundRect (CDrawContext* ctx, const CRect&) override
    {
        // the ground, the header band and the copper window frame (docs/THEME.md, "Window")
        pk::draw::window (ctx, CRect (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ()), 34);
    }
};

// A button you drag out of the plug-in: drops the rendered clip onto the host's timeline.
class DragOutButton : public CView
{
public:
    DragOutButton (const CRect& r, Editor* e) : CView (r), editor (e) {}
    void draw (CDrawContext* ctx) override
    {
        const CRect r = getViewSize ();
        ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
        // outlined like the kit's buttons: copper, cinnabar while it is being dragged out
        pk::draw::outline (ctx, r, pressed ? pk::theme::kEnergyLive : pk::theme::kCopper);
        // grip dots
        ctx->setFillColor (pk::theme::kCopperPale);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 2; ++j)
                ctx->drawEllipse (CRect (r.left + 7 + j * 4, r.top + 7 + i * 4, r.left + 9.5 + j * 4, r.top + 9.5 + i * 4),
                                  kDrawFilled);
        ctx->setFont (pk::theme::font (10.5));
        ctx->setFontColor (pk::theme::kText);
        ctx->drawString ("Drag to DAW", CRect (r.left + 16, r.top, r.right, r.bottom), kCenterText, true);
    }
    void onMouseDownEvent (MouseDownEvent& e) override
    {
        if (!e.buttonState.isLeft ())
            return;
        pressed = true;
        started = false;
        downAt = e.mousePosition;
        invalid ();
        e.consumed = true;
    }
    void onMouseMoveEvent (MouseMoveEvent& e) override
    {
        if (!pressed || started)
            return;
        e.consumed = true;
        if (std::hypot (e.mousePosition.x - downAt.x, e.mousePosition.y - downAt.y) < 4.0)
            return;
        started = true;
        std::string err;
        const std::string path = editor->renderToFile (err);
        pressed = false;
        invalid ();
        if (path.empty ())
            return;
        auto src = CDropSource::create (path.c_str (), (uint32_t)path.size () + 1, IDataPackage::kFilePath);
        doDrag (DragDescription (src));
    }
    void onMouseUpEvent (MouseUpEvent& e) override
    {
        pressed = false;
        invalid ();
        e.consumed = true;
    }

private:
    Editor* editor;
    CPoint downAt;
    bool pressed = false, started = false;
};

std::filesystem::path rendersFolder ()
{
#if defined(_WIN32)
    const char* home = std::getenv ("USERPROFILE");
#else
    const char* home = std::getenv ("HOME");
#endif
    std::filesystem::path base = home && *home ? smemplr::pathFromUtf8 (home) : std::filesystem::temp_directory_path ();
    return base / "Music" / "Stretchr Renders";
}

std::string safeName (std::string s)
{
    for (auto& ch : s)
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' ||
            ch == '|')
            ch = '_';
    const auto dot = s.find_last_of ('.');
    if (dot != std::string::npos && dot > 0 && s.size () - dot <= 5)
        s = s.substr (0, dot);
    return s.empty () ? std::string ("Clip") : s;
}

std::string clock (double t)
{
    char buf[32];
    const bool neg = t < 0.0;
    t = std::fabs (t);
    std::snprintf (buf, sizeof (buf), "%s%d:%06.3f", neg ? "-" : "", (int)(t / 60.0), std::fmod (t, 60.0));
    return buf;
}
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    tail.reset ();
    clipView = nullptr;
    status = summary[0] = summary[1] = nullptr;
    modeButtons[0] = modeButtons[1] = nullptr;
    captureBtn = nullptr;
    windowGroup = transientsGroup = smearGroup = noneGroup = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 140, 28), "stretchr", 14.0, true));
    status = new Label (CRect (140, 6, 620, 28), "", 10.5);
    root->addView (new pk::PresetBar (CRect (630, 6, 840, 28), ctl));
    status->setDim (true);
    root->addView (status);
    auto* helpBtn = new ActionButton (CRect (848, 6, 870, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (876, 6, 972, 28), "Menu", [this] { showMenu (layoutPoint (CPoint (876, 28))); }));

    // toolbar
    Session* s = ctl->getSession ();
    captureBtn = new ActionButton (
        CRect (8, 40, 104, 64), "Capture",
        [this] {
            if (auto* ss = ctl->getSession ())
                ss->setArmed (!ss->capturing ());
        },
        [this] { return ctl->getSession () && ctl->getSession ()->capturing (); });
    captureBtn->setTooltipText (help::kCapture);
    root->addView (captureBtn);
    auto* load = new ActionButton (CRect (110, 40, 176, 64), "Load...", [this] { browseForFile (); });
    load->setTooltipText (help::kLoad);
    root->addView (load);
    auto* toHead = new ActionButton (CRect (182, 40, 272, 64), "To Playhead", [this] {
        if (auto* ss = ctl->getSession (); ss && ss->hasClip ())
            ss->setClipStart (ss->transport.load ());
    });
    toHead->setTooltipText (help::kToPlayhead);
    root->addView (toHead);
    auto* clear = new ActionButton (CRect (278, 40, 332, 64), "Clear", [this] {
        if (auto* ss = ctl->getSession (); ss && ss->hasClip ())
            ss->setClip ({}, true);
    });
    clear->setTooltipText (help::kClear);
    root->addView (clear);

    root->addView (new Label (CRect (360, 40, 392, 64), "Edit", 10.5, false, 2));
    auto* stretchBtn = new ActionButton (
        CRect (398, 40, 464, 64), "Stretch", [this] { setEditMode (ClipView::kStretchMode); },
        [this] { return clipView && clipView->mode () == ClipView::kStretchMode; });
    stretchBtn->setTooltipText (help::kStretchMode);
    modeButtons[0] = stretchBtn;
    root->addView (stretchBtn);
    auto* pitchBtn = new ActionButton (
        CRect (468, 40, 534, 64), "Pitch", [this] { setEditMode (ClipView::kPitchMode); },
        [this] { return clipView && clipView->mode () == ClipView::kPitchMode; });
    pitchBtn->setTooltipText (help::kPitchMode);
    modeButtons[1] = pitchBtn;
    root->addView (pitchBtn);
    auto* undoBtn = new ActionButton (CRect (552, 40, 604, 64), "Undo", [this] {
        if (auto* ss = ctl->getSession ())
            ss->undo ();
    });
    undoBtn->setTooltipText (help::kUndo);
    root->addView (undoBtn);
    auto* redoBtn = new ActionButton (CRect (608, 40, 660, 64), "Redo", [this] {
        if (auto* ss = ctl->getSession ())
            ss->redo ();
    });
    redoBtn->setTooltipText (help::kRedo);
    root->addView (redoBtn);
    auto* exportBtn = new ActionButton (CRect (768, 40, 866, 64), "Export WAV...", [this] { exportWav (); });
    exportBtn->setTooltipText (help::kExport);
    root->addView (exportBtn);
    auto* dragOut = new DragOutButton (CRect (872, 40, 972, 64), this);
    pk::setHelp (dragOut, "Drag Out", help::kDragOut);
    root->addView (dragOut);

    clipView = new ClipView (CRect (8, 70, 972, 380), ctl);
    pk::setHelp (clipView, "Clip", help::kClipView);
    clipView->onFileDropped = [this] (const std::string& p) { loadFile (p); };
    clipView->onContextMenu = [this] (CPoint p) { showClipMenu (p); };
    root->addView (clipView);

    // ALGORITHM
    auto* algo = new Panel (CRect (8, 388, 316, 592), "ALGORITHM");
    root->addView (algo);
    bind (algo, new Choice (CRect (12, 24, 296, 48), this, kAlgorithm));
    for (int i = 0; i < 2; ++i)
    {
        summary[i] = new Label (CRect (12, 54 + i * 14, 296, 68 + i * 14), "", 10.0);
        summary[i]->setDim (true);
        algo->addView (summary[i]);
    }
    windowGroup = new pk::Group (CRect (0, 86, 308, 204));
    algo->addView (windowGroup);
    bind (windowGroup, new Knob (CRect (114, 8, 194, 108), this, kWindow));
    transientsGroup = new pk::Group (CRect (0, 86, 308, 204));
    algo->addView (transientsGroup);
    transientsGroup->addView (new Label (CRect (12, 30, 296, 44), "Transients", 10.5, false, 1));
    bind (transientsGroup, new Segmented (CRect (40, 48, 268, 72), this, kTransients, {"Crisp", "Mixed", "Smooth"}));
    smearGroup = new pk::Group (CRect (0, 86, 308, 204));
    algo->addView (smearGroup);
    bind (smearGroup, new Knob (CRect (40, 8, 120, 108), this, kSmear));
    alienGrain = bind (smearGroup, new Knob (CRect (128, 8, 208, 108), this, kWindow));
    smearGroup->addView (new Label (CRect (214, 30, 300, 44), "Stereo", 10.5, false, 1));
    bind (smearGroup, new Segmented (CRect (214, 48, 300, 70), this, kStereo, {"Wide", "Same"}));
    noneGroup = new pk::Group (CRect (0, 86, 308, 204));
    algo->addView (noneGroup);
    auto* none = new Label (CRect (12, 44, 296, 60), "No extra settings for this algorithm", 10.5, false, 1);
    none->setDim (true);
    noneGroup->addView (none);

    // PITCH
    auto* pitch = new Panel (CRect (324, 388, 636, 592), "PITCH");
    root->addView (pitch);
    bind (pitch, new Knob (CRect (14, 28, 94, 128), this, kPitch, nullptr, true));
    bind (pitch, new Knob (CRect (104, 40, 160, 104), this, kFine, nullptr, true));
    bind (pitch, new Knob (CRect (170, 28, 250, 128), this, kFormant, nullptr, true));
    bind (pitch, new Toggle (CRect (150, 150, 296, 172), this, kPreserveFormants, "Preserve Formants"));
    auto* envHint = new Label (CRect (14, 150, 144, 172), "+ pitch envelope", 10.0);
    envHint->setDim (true);
    pitch->addView (envHint);

    // TIME
    auto* time = new Panel (CRect (644, 388, 836, 592), "TIME");
    root->addView (time);
    bind (time, new Knob (CRect (14, 28, 94, 128), this, kSpeed));
    bind (time, new Knob (CRect (112, 40, 176, 104), this, kSourceBpm));
    bind (time, new Toggle (CRect (14, 150, 176, 172), this, kFollowTempo, "Follow Tempo"));

    // OUTPUT
    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    tail = std::make_unique<smacheratr::TailPanel> (this, smacheratr::TailBases {kTailBase, kTailExtBase, kTailExt2Base, kTailExt3Base, kTailExt4Base},
                                                    [c = ctl] { auto* s = c->getSession (); return s ? s->hostRate.load () : 48000.0; },
                                                    [c = ctl] () -> const smacheratr::Meters* { auto* s = c->getSession (); return s ? &s->tailMeters : nullptr; });
    tail->add (root, layoutRegion ("tail", CRect (8, 600, 972, 600 + smacheratr::TailPanel::kOpenHeight)));
    auto* out = new Panel (CRect (844, 388, 972, 592), "OUTPUT");
    root->addView (out);
    // the Gain knob, then Trigger and Outside Clip under it, each label over its selector (the knob
    // is a little smaller than the other panels' main knobs, so its value clears Trigger's label)
    bind (out, new Knob (CRect (28, 22, 100, 92), this, kGain, nullptr, true));
    out->addView (new Label (CRect (8, 98, 120, 112), "Trigger", 10.5, false, 1));
    bind (out, new Segmented (CRect (8, 112, 120, 130), this, kTrigger, {"Play", "Timeline"}));
    out->addView (new Label (CRect (8, 138, 120, 152), "Outside Clip", 10.5, false, 1));
    bind (out, new Segmented (CRect (8, 152, 120, 172), this, kOutside, {"Thru", "Mute"}));

    applyParamTooltips (&help::forParam);
    if (s)
        lastChanges = s->changeCount ();
    updateAlgorithmControls ();
    idle ();
}

void Editor::setEditMode (int mode)
{
    if (clipView)
        clipView->setMode ((ClipView::Mode)mode);
    for (auto* b : modeButtons)
        if (b)
            b->invalid ();
}

void Editor::updateAlgorithmControls ()
{
    if (!windowGroup)
        return;
    const int a = (int)std::lround (plainValue (kAlgorithm));
    windowGroup->setVisible (a == kWindowed || a == kBalanced);
    transientsGroup->setVisible (a == kPolyphonic);
    smearGroup->setVisible (a == kExtreme || a == kAlien);
    if (alienGrain)
        alienGrain->setVisible (a == kAlien); // Alien's grain size
    noneGroup->setVisible (a == kSoloist || a == kBeats || a == kTape);
    const std::string text = help::algorithmSummary (a);
    const auto nl = text.find ('\n');
    if (summary[0])
        summary[0]->setText (text.substr (0, nl));
    if (summary[1])
        summary[1]->setText (nl == std::string::npos ? std::string () : text.substr (nl + 1));
    auto look = [this] (uint32_t id, bool on) {
        for (auto* v : byParam[id])
            if (auto* pv = dynamic_cast<pk::ParamView*> (v))
                pv->setEnabledLook (on);
    };
    look (kPitch, a != kTape);
    look (kFine, a != kTape);
    look (kFormant, a == kPolyphonic || a == kSoloist);
    look (kPreserveFormants, a == kPolyphonic);
    const bool follow = plainValue (kFollowTempo) >= 0.5;
    look (kSpeed, !follow);
    look (kSourceBpm, follow);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (id == kAlgorithm || id == kFollowTempo)
        updateAlgorithmControls ();
    if (clipView && (id == kSpeed || id == kFollowTempo || id == kSourceBpm))
        clipView->invalid ();
}

void Editor::idle ()
{
    if (tail)
        tail->idle ();
    Session* s = ctl->getSession ();
    if (!s)
        return;
    if (clipView)
        clipView->idle ();
    if (captureBtn)
    {
        const int cap = s->captureState ();
        captureBtn->setText (cap == Session::kRecording ? "Stop" : (cap == Session::kArmed ? "Armed" : "Capture"));
    }
    // clip edits aren't parameters: tell the host the project changed
    const uint64_t ch = s->changeCount ();
    if (ch != lastChanges)
    {
        lastChanges = ch;
        ctl->markDirty ();
    }
    if (status)
    {
        std::string t;
        if (errorTicks > 0)
        {
            --errorTicks;
            t = lastError;
        }
        else if (s->hasClip ())
        {
            const Clip c = s->clip ();
            const double speed = s->settings ().speed;
            const double outLen = TimeMap (c.markers, 1.0 / std::max (speed, 1e-3)).outLength ();
            char buf[256];
            std::snprintf (buf, sizeof (buf), "%s   %.2f s -> %.2f s   starts at %s   %s", c.name.c_str (),
                           c.srcLength (), outLen, clock (c.start).c_str (),
                           s->rendering.load () ? "rendering..." : (s->upToDate () ? "ready" : "waiting"));
            t = buf;
        }
        else
            t = "No clip";
        status->setText (t);
    }
}

void Editor::loadFile (const std::string& path)
{
    Session* s = ctl->getSession ();
    if (!s)
        return;
    std::string err;
    auto data = SampleData::load (path, smemplr::SampleOps {}, err);
    if (!data)
    {
        lastError = "Could not load: " + err;
        errorTicks = 150;
        return;
    }
    Clip c;
    c.name = data->name;
    c.audio = std::move (data);
    c.start = s->transport.load ();
    s->setClip (std::move (c), true);
    if (clipView)
        clipView->zoomToFit ();
}

void Editor::browseForFile ()
{
    if (!frame)
        return;
    auto* sel = CNewFileSelector::create (frame, CNewFileSelector::kSelectFile);
    if (!sel)
        return;
    sel->setTitle ("Load Audio");
    sel->addFileExtension (CFileExtension ("Audio Files", "wav"));
    sel->addFileExtension (CFileExtension ("AIFF", "aif"));
    sel->addFileExtension (CFileExtension ("AIFF", "aiff"));
    sel->addFileExtension (CFileExtension ("FLAC", "flac"));
    sel->addFileExtension (CFileExtension ("MP3", "mp3"));
    sel->run ([this] (CNewFileSelector* fs) {
        if (fs->getNumSelectedFiles () > 0)
            loadFile (fs->getSelectedFile (0));
    });
    sel->forget ();
}

std::string Editor::renderToFile (std::string& error)
{
    Session* s = ctl->getSession ();
    if (!s || !s->hasClip ())
    {
        error = "Nothing to render";
        return {};
    }
    if (!s->upToDate () && !s->waitUntilRendered (20.0))
    {
        error = "Render not ready";
        return {};
    }
    RenderedPtr r = s->latest ();
    if (!r)
    {
        error = "Nothing to render";
        return {};
    }
    std::error_code ec;
    if (r->request == exportedKey && std::filesystem::exists (smemplr::pathFromUtf8 (exportedPath), ec))
        return exportedPath;
    const auto dir = rendersFolder ();
    std::filesystem::create_directories (dir, ec);
    const std::time_t now = std::time (nullptr);
    char stamp[32];
    std::strftime (stamp, sizeof (stamp), "%Y%m%d-%H%M%S", std::localtime (&now));
    const std::string name = safeName (s->clip ().name) + " stretched " + stamp + ".wav";
    const std::string path = smemplr::utf8FromPath (dir / smemplr::pathFromUtf8 (name));
    if (!writeWav (path, *r, error))
        return {};
    exportedKey = r->request;
    exportedPath = path;
    return path;
}

void Editor::exportWav ()
{
    Session* s = ctl->getSession ();
    if (!frame || !s || !s->hasClip ())
        return;
    auto* sel = CNewFileSelector::create (frame, CNewFileSelector::kSelectSaveFile);
    if (!sel)
        return;
    sel->setTitle ("Export Rendered Clip");
    sel->setDefaultExtension (CFileExtension ("WAVE", "wav"));
    sel->setDefaultSaveName ((safeName (s->clip ().name) + " stretched.wav").c_str ());
    sel->run ([this] (CNewFileSelector* fs) {
        if (fs->getNumSelectedFiles () == 0)
            return;
        std::string path = fs->getSelectedFile (0);
        if (path.size () < 4 || path.substr (path.size () - 4) != ".wav")
            path += ".wav";
        Session* ss = ctl->getSession ();
        if (!ss || (!ss->upToDate () && !ss->waitUntilRendered (20.0)))
            return;
        RenderedPtr r = ss->latest ();
        std::string err;
        if (!r || !writeWav (path, *r, err))
        {
            lastError = "Export failed: " + err;
            errorTicks = 150;
        }
    });
    sel->forget ();
}

void Editor::showClipMenu (CPoint where)
{
    Session* s = ctl->getSession ();
    if (!frame || !s)
        return;
    auto menu = makeOwned<COptionMenu> ();
    const bool has = s->hasClip ();
    auto flags = [] (bool on) { return on ? CMenuItem::kNoFlags : CMenuItem::kDisabled; };
    menu->addEntry ("Undo", -1, flags (s->canUndo ()));
    menu->addEntry ("Redo", -1, flags (s->canRedo ()));
    menu->addSeparator ();
    menu->addEntry ("Add Stretch Markers at Transients", -1, flags (has));
    menu->addEntry ("Reset Stretch Markers", -1, flags (has));
    menu->addEntry ("Clear Pitch Envelope", -1, flags (has));
    menu->addSeparator ();
    menu->addEntry ("Zoom to Fit", -1, flags (has));
    menu->addEntry ("Move Clip to Playhead", -1, flags (has));
    menu->popup (frame, where, [this, menu] (COptionMenu* m) {
        Session* ss = ctl->getSession ();
        if (!ss)
            return;
        switch (m->getLastResult ())
        {
            case 0: ss->undo (); break;
            case 1: ss->redo (); break;
            case 3:
                ss->edit ([] (Clip& c) {
                    const TimeMap map (c.markers, 1.0); // dst units: the new markers keep the timing
                    for (const auto& o : c.audio->onsets)
                        if (o.strength >= 0.2f)
                        {
                            const double src = o.pos / c.audio->sampleRate;
                            bool near = false;
                            for (const auto& mk : c.markers)
                                near = near || std::fabs (mk.src - src) < 0.01;
                            if (!near)
                                c.markers.push_back ({src, map.outAt (src)});
                        }
                });
                break;
            case 4: ss->edit ([] (Clip& c) { c.markers = identityMarkers (c.srcLength ()); }); break;
            case 5: ss->edit ([] (Clip& c) { c.pitch.clear (); }); break;
            case 7:
                if (clipView)
                    clipView->zoomToFit ();
                break;
            case 8: ss->setClipStart (ss->transport.load ()); break;
            default: break;
        }
    });
}

void Editor::showMenu (CPoint where)
{
    if (!frame)
        return;
    auto menu = makeOwned<COptionMenu> ();
    std::vector<double> sizes {0.75, 1.0, 1.25, 1.5, 2.0};
    for (double sz : sizes)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "Interface Size %d%%", (int)std::lround (sz * 100));
        menu->addEntry (buf, -1, std::fabs (currentScale () - sz) < 0.01 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    }
    const int settingsAt = pk::addSettingsMenuEntries (menu);
    addLayoutMenu (menu);
    menu->popup (frame, where, [this, sizes, menu, settingsAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (pickedInSubMenu (m)) // (Layout: its entries act by themselves)
            return;
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

pk::layout::Spec Editor::layoutSpec (bool) const
{
    pk::layout::Spec s;
    // the clip (its toolbar over it) beside the algorithm, pitch, time and output; the end saturator under them
    s.panels = {
        {"clip", "clip", {8, 40, 972, 380}, 0},
        {"algorithm", "", {8, 388, 316, 592}, 0},
        {"pitch", "", {324, 388, 636, 592}, 0},
        {"time", "", {644, 388, 836, 592}, 0},
        {"output", "", {844, 388, 972, 592}, 0},
        {"tail", "end of the chain", {8, 600, 972, 600 + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
    };
    return s;
}

} // namespace stretchr

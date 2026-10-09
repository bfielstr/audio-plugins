#include "Editor.h"

#include "Help.h"
#include "ProbeViews.h"
#include "Session.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace probr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Label;
using pk::Panel;
using pk::Segmented;

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

Label* caption (CViewContainer* parent, const CRect& r, const char* text)
{
    auto* l = new Label (r, text, 10.5);
    l->setDim (true);
    parent->addView (l);
    return l;
}

std::string bytesText (int64_t b)
{
    char buf[48];
    if (b < 0)
        return "";
    if (b >= (int64_t)1 << 30)
        std::snprintf (buf, sizeof (buf), "%.1f GB free", (double)b / (double)(1LL << 30));
    else
        std::snprintf (buf, sizeof (buf), "%.0f MB free", (double)b / (double)(1LL << 20));
    return buf;
}

// the home folder shown as ~ (macOS, Linux)
std::string shortPath (const std::string& p)
{
#if !defined(_WIN32)
    const char* home = std::getenv ("HOME");
    const size_t n = home ? std::strlen (home) : 0;
    if (n > 1 && p.compare (0, n, home) == 0 && (p.size () == n || p[n] == '/'))
        return "~" + p.substr (n);
#endif
    return p;
}

std::string timeText (double seconds)
{
    const int total = (int)std::floor (seconds * 10.0);
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%d:%02d.%d", total / 600, (total / 10) % 60, total % 10);
    return buf;
}
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    record = nullptr;
    meter = nullptr;
    message = nullptr;
    labelField = nullptr;
    folderLabel = takeLabel = freeLabel = sessionLabel = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "probr", 14.0, true));
    const double hx = kWidth - 320; // the header's controls at the right, as in the other plug-ins
    root->addView (new pk::PresetBar (CRect (hx, 6, hx + 196, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (hx + 204, 6, hx + 226, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide the floating help tooltips (the info box at the bottom shows the same help either way).");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (hx + 232, 6, hx + 312, 28), "Menu", [this, hx] { showMenu (layoutPoint (CPoint (hx + 232, 28))); }));

    // PROBE: the Label and the Folder
    auto* probe = new Panel (CRect (kProbeLeft, kTop, kProbeRight, kRowBottom), "PROBE");
    root->addView (probe);
    caption (probe, CRect (12, 22, 200, 36), "Label");
    labelField = new TextField (CRect (kFieldLeft, kFieldTop, kFieldRight, kFieldBottom), [c = ctl] (const std::string& t) { c->setLabel (t); });
    labelField->setText (ctl->label ().c_str ());
    labelShown = ctl->label ();
    pk::setHelp (labelField, "Label", help::kLabel);
    probe->addView (labelField);
    caption (probe, CRect (12, 70, 200, 84), "Folder");
    folderLabel = new Label (CRect (12, 86, kFieldRight, 102), "", 10.5);
    pk::setHelp (folderLabel, "Folder", help::kFolder);
    probe->addView (folderLabel);
    auto* choose = new ActionButton (CRect (kChooseLeft, kChooseTop, kChooseLeft + kChooseW, kChooseTop + kButtonH), "Choose...",
                                     [this] { chooseFolder (); });
    pk::setHelp (choose, "Folder", help::kFolder);
    probe->addView (choose);
    auto* def = new ActionButton (CRect (kDefaultLeft, kChooseTop, kDefaultLeft + kChooseW, kChooseTop + kButtonH), "Default",
                                  [c = ctl] { c->setFolder (""); });
    pk::setHelp (def, "Folder", help::kFolder);
    probe->addView (def);

    // RECORD: the button, Mode, the take
    auto* rec = new Panel (CRect (kRecLeft, kTop, kRecRight, kRowBottom), "RECORD");
    root->addView (rec);
    record = bind (rec, new RecordButton (CRect (kRecordLeft, kRecordTop, kRecordLeft + kRecordW, kRecordTop + kRecordH), this, kRecord,
                                          [c = ctl] () -> int {
                                              if (auto* s = c->getShared ())
                                                  return s->status.state.load (std::memory_order_relaxed);
                                              return c->getParamNormalized (kRecord) >= 0.5 ? kStateArmed : kStateOff;
                                          }));
    caption (rec, CRect (kModeLeft, kModeTop - 16, kModeLeft + kModeW, kModeTop - 2), "Mode");
    bind (rec, new Segmented (CRect (kModeLeft, kModeTop, kModeLeft + kModeW, kModeTop + kModeH), this, kMode, {"While Playing", "Always"}));
    takeLabel = new Label (CRect (12, 118, 212, 134), "", 10.5);
    pk::setHelp (takeLabel, "Take", help::kTake);
    rec->addView (takeLabel);

    // MONITOR: the level, the probe's state and problems, the free space, the session
    auto* mon = new Panel (CRect (kProbeLeft, kMonTop, kRecRight, kMonBottom), "MONITOR");
    root->addView (mon);
    meter = new LevelMeter (CRect (12, 24, 300, 46), [c = ctl] (float& l, float& r) {
        if (auto* s = c->getShared ())
        {
            l = s->status.peakL.exchange (0.0f, std::memory_order_relaxed);
            r = s->status.peakR.exchange (0.0f, std::memory_order_relaxed);
        }
    });
    pk::setHelp (meter, "Level", help::kMeter);
    mon->addView (meter);
    message = new MessageText (CRect (312, 22, 612, 38));
    pk::setHelp (message, "Status", help::kStatus);
    mon->addView (message);
    freeLabel = new Label (CRect (312, 40, 612, 54), "", 10.5);
    freeLabel->setDim (true);
    pk::setHelp (freeLabel, "Status", help::kStatus);
    mon->addView (freeLabel);
    sessionLabel = new Label (CRect (12, 56, 612, 72), "", 10.5);
    sessionLabel->setDim (true);
    pk::setHelp (sessionLabel, "Session", help::kSession);
    mon->addView (sessionLabel);

    applyParamTooltips (&help::forParam);
    updateTexts ();
}

void Editor::chooseFolder ()
{
    if (!frame)
        return;
    auto* sel = CNewFileSelector::create (frame, CNewFileSelector::kSelectDirectory);
    if (!sel)
        return;
    sel->setTitle ("Folder for the probr sessions");
    sel->setInitialDirectory (ctl->folderShown ().c_str ());
    sel->run ([c = ctl] (CNewFileSelector* fs) {
        if (fs->getNumSelectedFiles () > 0)
            c->setFolder (fs->getSelectedFile (0));
    });
    sel->forget ();
}

void Editor::updateTexts ()
{
    if (folderLabel)
        folderLabel->setText (fitText (shortPath (ctl->folderShown ()) + (ctl->folder ().empty () ? "  (default)" : ""), 58));
    if (labelField && ctl->label () != labelShown && (!frame || frame->getFocusView () != labelField))
    {
        // (a project or a preset loaded: the field follows, unless it is being typed in)
        labelShown = ctl->label ();
        labelField->setText (labelShown.c_str ());
    }
    Shared* s = ctl->getShared ();
    if (!s)
    {
        if (message)
            message->set ("Not connected to the audio part", false);
        return;
    }
    const Status& st = s->status;
    const int state = st.state.load (std::memory_order_relaxed);
    const int fault = st.fault.load (std::memory_order_relaxed);
    const int64_t free = st.freeBytes.load (std::memory_order_relaxed);
    const double sr = std::max (1.0, st.sampleRate.load (std::memory_order_relaxed));
    if (takeLabel)
    {
        const int take = st.take.load (std::memory_order_relaxed), written = st.takesWritten.load (std::memory_order_relaxed);
        char buf[96];
        if (take <= 0)
            std::snprintf (buf, sizeof (buf), "No take yet");
        else
            std::snprintf (buf, sizeof (buf), "Take %d   %s   (%d written)", take,
                           timeText ((double)st.takeFrames.load (std::memory_order_relaxed) / sr).c_str (), written);
        takeLabel->setText (buf);
    }
    if (message)
    {
        std::string text;
        bool warn = true;
        switch (fault)
        {
            case kFaultDiskFull: text = "Stopped: the disk is full"; break;
            case kFaultOverrun: text = "Stopped: the disk could not keep up"; break;
            case kFaultWrite: text = "Stopped: cannot write to the folder"; break;
            case kFaultSizeLimit: text = "Stopped: the take reached 4 GB"; break;
            default: warn = false; break;
        }
        if (warn && state != kStateOff)
            text += " (Record off and on)";
        if (!warn)
        {
            if (state != kStateOff && free >= 0 && free < kLowSpaceBytes)
            {
                text = "Low disk space: " + bytesText (free);
                warn = true;
            }
            else if (st.noTransport.load (std::memory_order_relaxed))
            {
                text = "The host gives no song position: use Always";
                warn = true;
            }
            else if (state == kStateRecording)
            {
                const int midi = st.midiEvents.load (std::memory_order_relaxed);
                text = midi > 0 ? "Recording, with " + std::to_string (midi) + " MIDI events" : "Recording";
            }
            else if (state == kStateArmed)
                text = "Armed: waiting for the song to play";
            else
                text = "Off: the sound passes untouched";
        }
        message->set (text, warn);
    }
    if (freeLabel)
        freeLabel->setText (state != kStateOff ? bytesText (free) : std::string ()); // (where the takes go: shown while armed)
    if (sessionLabel)
    {
        std::string dir, file;
        s->settings.where (dir, file);
        sessionLabel->setText (dir.empty () ? "No session yet: it is made when the first probe arms"
                                            : fitText ("Session: " + shortPath (joinPath (dir, file)), 100));
    }
}

pk::basic::Spec Editor::basicSpec ()
{
    // what a recorder needs at hand: the record button, the level and what the probe is doing, the take,
    // and when it records (Mode). The Label, the Folder and the session are in the Advanced view. No
    // capture band: probr records the track itself (and passes it untouched), so it would hold the same.
    using namespace pk::basic;
    Spec s;
    s.title = "probr";
    s.displayHeight = 96;
    s.display = [this] (const CRect& r) -> CView* {
        auto* g = new pk::Group (r);
        record = bind (g, new RecordButton (CRect (0, 8, kRecordW, 8 + kRecordH), this, kRecord, [c = ctl] () -> int {
                           if (auto* sh = c->getShared ())
                               return sh->status.state.load (std::memory_order_relaxed);
                           return c->getParamNormalized (kRecord) >= 0.5 ? kStateArmed : kStateOff;
                       }));
        takeLabel = new Label (CRect (0, 66, kRecordW + 100, 82), "", 10.5);
        pk::setHelp (takeLabel, "Take", help::kTake);
        g->addView (takeLabel);
        meter = new LevelMeter (CRect (kRecordW + 24, 8, r.getWidth (), 30), [c = ctl] (float& l, float& rr) {
            if (auto* sh = c->getShared ())
            {
                l = sh->status.peakL.exchange (0.0f, std::memory_order_relaxed);
                rr = sh->status.peakR.exchange (0.0f, std::memory_order_relaxed);
            }
        });
        pk::setHelp (meter, "Level", help::kMeter);
        g->addView (meter);
        message = new MessageText (CRect (kRecordW + 24, 38, r.getWidth (), 54));
        pk::setHelp (message, "Status", help::kStatus);
        g->addView (message);
        freeLabel = new Label (CRect (kRecordW + 24, 58, r.getWidth (), 72), "", 10.5);
        freeLabel->setDim (true);
        pk::setHelp (freeLabel, "Status", help::kStatus);
        g->addView (freeLabel);
        return g;
    };
    s.rows = {{segmented (kMode, "Mode", {"While Playing", "Always"}, 260)}};
    // the strip's line: where the takes go (chosen in the Advanced view)
    s.summary = [this] () -> std::string {
        return "folder: " + fitText (shortPath (ctl->folderShown ()) + (ctl->folder ().empty () ? "  (default)" : ""), 80);
    };
    s.summaryHelp = help::kFolder;
    s.menu = [this] (CPoint p) { showMenu (p); };
    s.help = &help::forParam;
    s.advancedSwitch = CRect (kWidth - 416, 6, kWidth - 328, 28);
    return s;
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (id == kRecord && record)
        record->invalid ();
}

void Editor::idle ()
{
    if (record)
        record->idle ();
    if (meter)
        meter->idle ();
    updateTexts ();
}

void Editor::showMenu (CPoint where)
{
    if (!frame)
        return;
    auto menu = makeOwned<COptionMenu> ();
    std::vector<double> sizes {0.75, 1.0, 1.25, 1.5, 2.0};
    for (double s : sizes)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "Interface Size %d%%", (int)std::lround (s * 100));
        menu->addEntry (buf, -1, std::fabs (currentScale () - s) < 0.01 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    }
    const int settingsAt = pk::addSettingsMenuEntries (menu);
    addLayoutMenu (menu);
    menu->popup (frame, where, [this, sizes, settingsAt] (COptionMenu* m) {
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
    // Wide: PROBE, RECORD and MONITOR side by side
    s.panels = {
        {"probe", "", {kProbeLeft, kTop, kProbeRight, kRowBottom}, 0},
        {"record", "", {kRecLeft, kTop, kRecRight, kRowBottom}, 0},
        {"monitor", "", {kProbeLeft, kMonTop, kRecRight, kMonBottom}, 0},
    };
    return s;
}

} // namespace probr

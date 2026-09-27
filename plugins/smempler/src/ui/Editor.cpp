#include "Editor.h"

#include "Displays.h"
#include "Engine.h"
#include "Help.h"
#include "Params.h"
#include "UiKit.h"
#include "WaveformView.h"
#include "plugin/Controller.h"

#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cvstguitimer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <type_traits>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace smempler {

using namespace VSTGUI;
using namespace Steinberg;

namespace {
constexpr double kKnobW = 56, kKnobH = 64;

CRect knobRect (double x, double y) { return CRect (x, y, x + kKnobW, y + kKnobH); }

class Background : public CViewContainer
{
public:
    using CViewContainer::CViewContainer;
    void drawBackgroundRect (CDrawContext* ctx, const CRect&) override
    {
        ctx->setFillColor (theme::kBackground);
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ()), kDrawFilled);
        ctx->setFillColor (theme::kHeader);
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), 34), kDrawFilled);
    }
};

std::filesystem::path u8 (const std::string& s) { return pathFromUtf8 (s); }

// Shows the file in Finder / Explorer / the desktop's file manager.
void revealInFileBrowser (const std::string& path)
{
#if defined(_WIN32)
    const std::wstring args = L"/select,\"" + u8 (path).wstring () + L"\"";
    ShellExecuteW (nullptr, L"open", L"explorer.exe", args.c_str (), nullptr, SW_SHOWNORMAL);
#else
#if defined(__APPLE__)
    const std::string target = path;
    const char* argv[] = {"/usr/bin/open", "-R", target.c_str (), nullptr};
#else
    const std::string target = utf8FromPath (u8 (path).parent_path ());
    const char* argv[] = {"xdg-open", target.c_str (), nullptr};
#endif
    pid_t pid;
    if (posix_spawnp (&pid, argv[0], nullptr, nullptr, const_cast<char**> (argv), environ) == 0)
        waitpid (pid, nullptr, WNOHANG);
#endif
}
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    waveform = nullptr;
    filterDisplay = nullptr;
    envDisplay = nullptr;
}

// --- building ---------------------------------------------------------------------
void Editor::buildUI (CFrame* f)
{
    byParam.clear ();
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);

    // ---- top bar ----------------------------------------------------------------
    auto tip = [] (CView* v, const char* t) {
        v->setTooltipText (t);
        return v;
    };
    root->addView (tip (new ActionButton (CRect (8, 6, 60, 28), "Load", [this] { browseForSample (); }), help::kLoad));
    root->addView (tip (new ActionButton (CRect (64, 6, 86, 28), "<", [this] { stepSample (-1); }), help::kPrevNext));
    root->addView (tip (new ActionButton (CRect (88, 6, 110, 28), ">", [this] { stepSample (1); }), help::kPrevNext));
    nameLabel = new Label (CRect (118, 6, 470, 28), "No sample", 12.0, true);
    root->addView (nameLabel);
    bind (root, new Segmented (CRect (480, 6, 740, 28), this, kMode, {"Classic", "One-Shot", "Slicing"}));
    bind (root, new Toggle (CRect (750, 6, 810, 28), this, kWarp, "WARP"));
    hostLabel = new Label (CRect (820, 6, 1014, 28), "", 10.5, false, 2);
    root->addView (hostLabel);
    root->addView (tip (new ActionButton (CRect (1022, 6, 1044, 28), "?", [this] { setTooltipsEnabled (!ctl->uiShowTips); },
                                          [this] { return ctl->uiShowTips; }),
                        help::kHelpButton));
    root->addView (tip (new ActionButton (CRect (1050, 6, 1102, 28), "Menu", [this] {
                            CPoint p (1050, 28);
                            showMenu (p);
                        }),
                        help::kMenu));

    // ---- waveform ---------------------------------------------------------------
    waveform = new WaveformView (CRect (8, 38, 1102, 300), ctl, this);
    waveform->onContextMenu = [this] (CPoint p) { showMenu (p); };
    waveform->onFileDropped = [this] (const std::string& p) { loadFile (p); };
    waveform->setTooltipText (help::kWaveform);
    root->addView (waveform);

    // ---- sample row -------------------------------------------------------------
    auto* sp = new Panel (CRect (8, 306, 1102, 404), "SAMPLE");
    root->addView (sp);

    classicGroup = new Group (CRect (0, 0, 700, 98));
    sp->addView (classicGroup);
    bind (classicGroup, new Knob (knobRect (10, 24), this, kGain));
    bind (classicGroup, new Knob (knobRect (70, 24), this, kStart));
    bind (classicGroup, new Knob (knobRect (130, 24), this, kLength));
    bind (classicGroup, new Knob (knobRect (190, 24), this, kLoopLen));
    bind (classicGroup, new Knob (knobRect (250, 24), this, kLoopFade));
    bind (classicGroup, new Toggle (CRect (316, 32, 370, 50), this, kLoopOn, "Loop"));
    bind (classicGroup, new Toggle (CRect (316, 58, 370, 76), this, kSnap, "Snap"));
    bind (classicGroup, new Choice (CRect (384, 24, 454, 58), this, kVoices, "Voices"));
    bind (classicGroup, new Toggle (CRect (384, 64, 454, 82), this, kRetrig, "Retrig"));

    oneShotGroup = new Group (CRect (0, 0, 700, 98));
    sp->addView (oneShotGroup);
    bind (oneShotGroup, new Knob (knobRect (10, 24), this, kGain));
    oneShotGroup->addView (new Label (CRect (76, 22, 196, 36), "Note Off", 10.5, false, 1));
    bind (oneShotGroup, new Segmented (CRect (76, 38, 196, 58), this, kTriggerGate, {"Trigger", "Gate"}));
    bind (oneShotGroup, new Knob (knobRect (206, 24), this, kFadeIn));
    bind (oneShotGroup, new Knob (knobRect (266, 24), this, kFadeOut));
    bind (oneShotGroup, new Toggle (CRect (332, 40, 386, 58), this, kSnap, "Snap"));

    sliceGroup = new Group (CRect (0, 0, 700, 98));
    sp->addView (sliceGroup);
    bind (sliceGroup, new Choice (CRect (10, 24, 100, 58), this, kSliceBy, "Slice By"));
    sensKnob = bind (sliceGroup, new Knob (knobRect (108, 24), this, kSensitivity));
    divisionChoice = bind (sliceGroup, new Choice (CRect (108, 24, 180, 58), this, kDivision, "Division"));
    regionsChoice = bind (sliceGroup, new Choice (CRect (108, 24, 180, 58), this, kRegions, "Regions"));
    auto* hint = new Label (CRect (108, 30, 200, 60), "Double-click to add", 10.0, false, 0);
    hint->setDim (true);
    manualHint = hint;
    sliceGroup->addView (hint);
    sliceGroup->addView (new Label (CRect (200, 22, 330, 36), "Playback", 10.5, false, 1));
    bind (sliceGroup, new Segmented (CRect (200, 38, 330, 58), this, kSlicePlayback, {"Mono", "Poly", "Thru"}));
    slicePolyGroup = new Group (CRect (336, 0, 410, 98));
    sliceGroup->addView (slicePolyGroup);
    bind (slicePolyGroup, new Choice (CRect (4, 24, 70, 58), this, kVoices, "Voices"));
    bind (slicePolyGroup, new Toggle (CRect (4, 64, 70, 82), this, kRetrig, "Retrig"));
    sliceGroup->addView (new Label (CRect (414, 22, 514, 36), "Note Off", 10.5, false, 1));
    bind (sliceGroup, new Segmented (CRect (414, 38, 514, 58), this, kTriggerGate, {"Trigger", "Gate"}));
    bind (sliceGroup, new Toggle (CRect (414, 64, 464, 82), this, kSnap, "Snap"));
    bind (sliceGroup, new Knob (knobRect (520, 24), this, kFadeIn));
    bind (sliceGroup, new Knob (knobRect (578, 24), this, kFadeOut));
    bind (sliceGroup, new Knob (knobRect (636, 24), this, kGain));

    // warp section
    auto* divider = new Group (CRect (700, 8, 701, 92));
    divider->setBackgroundColor (theme::kPanelEdge);
    sp->addView (divider);
    warpOffGroup = new Group (CRect (706, 0, 1094, 98));
    sp->addView (warpOffGroup);
    auto* off1 = new Label (CRect (8, 30, 380, 46), "Warp is off: the sample plays at its own speed", 11.0);
    auto* off2 = new Label (CRect (8, 48, 380, 64), "and its pitch follows the keyboard.", 11.0);
    off1->setDim (true);
    off2->setDim (true);
    warpOffGroup->addView (off1);
    warpOffGroup->addView (off2);

    warpOnGroup = new Group (CRect (706, 0, 1094, 98));
    sp->addView (warpOnGroup);
    bind (warpOnGroup, new Choice (CRect (8, 24, 110, 58), this, kWarpMode, "Warp Mode"));
    beatsGroup = new Group (CRect (116, 0, 388, 66));
    warpOnGroup->addView (beatsGroup);
    bind (beatsGroup, new Choice (CRect (4, 24, 92, 58), this, kBeatsPreserve, "Preserve"));
    bind (beatsGroup, new Choice (CRect (98, 24, 190, 58), this, kBeatsLoop, "Loop Mode"));
    bind (beatsGroup, new Knob (CRect (200, 4, 256, 66), this, kBeatsEnvelope, "Envelope"));
    tonesGroup = new Group (CRect (116, 0, 388, 66));
    warpOnGroup->addView (tonesGroup);
    bind (tonesGroup, new Knob (CRect (4, 4, 64, 66), this, kTonesGrain, "Grain Size"));
    textureGroup = new Group (CRect (116, 0, 388, 66));
    warpOnGroup->addView (textureGroup);
    bind (textureGroup, new Knob (CRect (4, 4, 64, 66), this, kTextureGrain, "Grain Size"));
    bind (textureGroup, new Knob (CRect (70, 4, 126, 66), this, kTextureFlux, "Flux"));
    cproGroup = new Group (CRect (116, 0, 388, 66));
    warpOnGroup->addView (cproGroup);
    bind (cproGroup, new Knob (CRect (4, 4, 64, 66), this, kFormants, "Formants"));
    bind (cproGroup, new Knob (CRect (70, 4, 126, 66), this, kCproEnvelope, "Envelope"));

    warpOnGroup->addView (new Label (CRect (8, 70, 56, 88), "Warp as", 10.5));
    auto stepBeats = [this] (double factor, int add) {
        const double b = plainValue (kWarpBeats);
        double nb = add != 0 ? b + add : b * factor;
        nb = std::clamp (std::round (nb), 1.0, 1024.0);
        ctl->setPlainFromUI (kWarpBeats, nb);
    };
    warpOnGroup->addView (tip (new ActionButton (CRect (58, 69, 76, 89), "-", [stepBeats] { stepBeats (1.0, -1); }), help::kWarpAs));
    warpBeatsBox = new ActionButton (CRect (78, 69, 150, 89), "4 Bars", [] {});
    warpOnGroup->addView (warpBeatsBox);
    warpOnGroup->addView (tip (new ActionButton (CRect (152, 69, 170, 89), "+", [stepBeats] { stepBeats (1.0, 1); }), help::kWarpAs));
    warpOnGroup->addView (tip (new ActionButton (CRect (176, 69, 204, 89), ":2", [stepBeats] { stepBeats (0.5, 0); }), help::kWarpAs));
    warpOnGroup->addView (tip (new ActionButton (CRect (206, 69, 234, 89), "x2", [stepBeats] { stepBeats (2.0, 0); }), help::kWarpAs));
    bpmLabel = new Label (CRect (240, 70, 386, 88), "", 10.5, false, 2);
    warpOnGroup->addView (bpmLabel);

    // ---- filter -----------------------------------------------------------------
    auto* fp = new Panel (CRect (8, 410, 380, 716), "FILTER");
    root->addView (fp);
    bind (fp, new Toggle (CRect (60, 3, 100, 17), this, kFilterOn, "On"));
    bind (fp, new Choice (CRect (8, 24, 104, 42), this, kFilterType));
    bind (fp, new Choice (CRect (110, 24, 190, 42), this, kFilterCircuit));
    bind (fp, new Segmented (CRect (196, 24, 266, 42), this, kFilterSlope, {"12", "24"}));
    filterDisplay = new FilterDisplay (CRect (8, 48, 364, 204), this);
    filterDisplay->setTooltipText (help::kFilterDisplay);
    fp->addView (filterDisplay);
    bind (fp, new Knob (knobRect (8, 214), this, kFilterFreq));
    bind (fp, new Knob (knobRect (66, 214), this, kFilterRes));
    driveKnob = bind (fp, new Knob (knobRect (124, 214), this, kFilterDrive));
    morphKnob = bind (fp, new Knob (knobRect (124, 214), this, kFilterMorph));
    bind (fp, new Knob (knobRect (182, 214), this, kFilterVel));
    bind (fp, new Knob (knobRect (240, 214), this, kFilterKey));
    bind (fp, new Knob (knobRect (298, 214), this, kFilterEnvAmt, "Env", true));

    // ---- envelopes --------------------------------------------------------------
    auto* ep = new Panel (CRect (386, 410, 704, 716), "ENVELOPE");
    root->addView (ep);
    const char* tabNames[] = {"Amp", "Filter", "Pitch"};
    tabButtons.clear ();
    for (int i = 0; i < 3; ++i)
    {
        auto* b = new ActionButton (CRect (8 + i * 64, 24, 70 + i * 64, 42), tabNames[i], [this, i] { setEnvTab (i); },
                                    [this, i] { return envTab == i; });
        ep->addView (b);
        tabButtons.push_back (b);
    }
    envDisplay = new EnvelopeDisplay (CRect (8, 48, 310, 204), this, 0);
    envDisplay->setTooltipText (help::kEnvelope);
    ep->addView (envDisplay);
    for (int t = 0; t < 3; ++t)
    {
        envTabs[t] = new Group (CRect (0, 206, 318, 306));
        ep->addView (envTabs[t]);
        const uint32_t base = t == 0 ? kAmpA : (t == 1 ? kFiltA : kPitchA);
        for (uint32_t i = 0; i < 4; ++i)
            bind (envTabs[t], new Knob (knobRect (8 + i * 58, 8), this, base + i));
        if (t == 1)
            bind (envTabs[t], new Knob (knobRect (240, 8), this, kFilterEnvAmt, "Amount", true));
        if (t == 2)
            bind (envTabs[t], new Knob (knobRect (240, 8), this, kPitchEnvAmt, "Amount", true));
    }
    bind (envTabs[0], new Choice (CRect (240, 8, 310, 42), this, kAmpLoopMode, "Loop"));
    ampLoopTime = bind (envTabs[0], new Knob (CRect (248, 44, 304, 100), this, kAmpLoopTime, "Time"));
    ampLoopRate = bind (envTabs[0], new Choice (CRect (240, 48, 310, 82), this, kAmpLoopRate, "Rate"));

    // ---- LFO --------------------------------------------------------------------
    auto* lp = new Panel (CRect (710, 410, 918, 716), "LFO");
    root->addView (lp);
    bind (lp, new Toggle (CRect (40, 3, 80, 17), this, kLfoOn, "On"));
    bind (lp, new Choice (CRect (8, 24, 110, 42), this, kLfoWave));
    bind (lp, new Segmented (CRect (116, 24, 176, 42), this, kLfoSync, {"Hz", "Sync"}));
    bind (lp, new Toggle (CRect (180, 24, 200, 42), this, kLfoRetrig, "R"));
    lfoRateHz = bind (lp, new Knob (knobRect (8, 50), this, kLfoRate));
    lfoRateSync = bind (lp, new Knob (knobRect (8, 50), this, kLfoSyncRate));
    bind (lp, new Knob (knobRect (74, 50), this, kLfoAttack));
    bind (lp, new Knob (knobRect (140, 50), this, kLfoOffset));
    bind (lp, new Knob (knobRect (8, 120), this, kLfoKey));
    lp->addView (new Label (CRect (8, 190, 200, 202), "LFO AMOUNT", 10.0, true));
    bind (lp, new HSlider (CRect (8, 206, 200, 222), this, kLfoVol, "Volume"));
    bind (lp, new HSlider (CRect (8, 228, 200, 244), this, kLfoPitch, "Pitch"));
    bind (lp, new HSlider (CRect (8, 250, 200, 266), this, kLfoPan, "Pan"));
    bind (lp, new HSlider (CRect (8, 272, 200, 288), this, kLfoFilter, "Filter"));

    // ---- global -----------------------------------------------------------------
    auto* gp = new Panel (CRect (924, 410, 1102, 716), "GLOBAL");
    root->addView (gp);
    bind (gp, new Knob (knobRect (4, 22), this, kVolume));
    bind (gp, new Knob (knobRect (61, 22), this, kVelVol));
    bind (gp, new Knob (knobRect (118, 22), this, kPan, nullptr, true));
    bind (gp, new Knob (knobRect (4, 92), this, kPanRand));
    bind (gp, new Knob (knobRect (61, 92), this, kSpread));
    bind (gp, new Knob (knobRect (118, 92), this, kTranspose, nullptr, true));
    bind (gp, new Knob (knobRect (4, 162), this, kDetune, nullptr, true));
    bind (gp, new Knob (knobRect (61, 162), this, kPbRange));
    bind (gp, new Knob (knobRect (118, 162), this, kGlideTime));
    gp->addView (new Label (CRect (4, 236, 174, 250), "Glide", 10.5, false, 1));
    bind (gp, new Segmented (CRect (4, 252, 174, 272), this, kGlideMode, {"Off", "Glide", "Porta"}));

    // hover help for every parameter control
    applyParamTooltips (&help::forParam);

    updateVisibility ();
    lastName.clear ();
    idle ();
}

// --- updates ----------------------------------------------------------------------
void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (waveform)
        waveform->invalid ();
    if (filterDisplay)
        filterDisplay->invalid ();
    if (envDisplay)
        envDisplay->invalid ();
    switch (id)
    {
        case kMode:
        case kWarp:
        case kWarpMode:
        case kSliceBy:
        case kSlicePlayback:
        case kFilterType:
        case kFilterCircuit:
        case kFilterOn:
        case kLfoSync:
        case kLfoOn:
        case kAmpLoopMode:
        case kWarpBeats: updateVisibility (); break;
        default: break;
    }
}

void Editor::bridgeChanged ()
{
    if (waveform)
        waveform->invalid ();
}

void Editor::setEnvTab (int t)
{
    envTab = std::clamp (t, 0, 2);
    if (envDisplay)
        envDisplay->setWhich (envTab);
    for (auto* b : tabButtons)
        b->invalid ();
    updateVisibility ();
}

void Editor::updateVisibility ()
{
    if (!classicGroup)
        return;
    const int mode = (int)std::lround (plainValue (kMode));
    classicGroup->setVisible (mode == kModeClassic);
    oneShotGroup->setVisible (mode == kModeOneShot);
    sliceGroup->setVisible (mode == kModeSlicing);

    const int sliceBy = (int)std::lround (plainValue (kSliceBy));
    sensKnob->setVisible (sliceBy == kSliceTransient);
    divisionChoice->setVisible (sliceBy == kSliceBeat);
    regionsChoice->setVisible (sliceBy == kSliceRegion);
    manualHint->setVisible (sliceBy == kSliceManual);
    slicePolyGroup->setVisible (std::lround (plainValue (kSlicePlayback)) == kSlicePoly);

    const bool warp = plainValue (kWarp) >= 0.5;
    warpOnGroup->setVisible (warp);
    warpOffGroup->setVisible (!warp);
    const int wm = (int)std::lround (plainValue (kWarpMode));
    beatsGroup->setVisible (wm == kWarpBeatsMode);
    tonesGroup->setVisible (wm == kWarpTones);
    textureGroup->setVisible (wm == kWarpTexture);
    cproGroup->setVisible (wm == kWarpComplexPro);
    if (warpBeatsBox)
        warpBeatsBox->setText (valueText (kWarpBeats));

    const int ft = (int)std::lround (plainValue (kFilterType));
    const int fc = (int)std::lround (plainValue (kFilterCircuit));
    morphKnob->setVisible (ft == kMorph);
    driveKnob->setVisible (ft != kMorph);
    static_cast<ParamView*> (driveKnob)
        ->setEnabledLook (fc != kClean && circuitSupported (ft, fc) && ft != kNotch);

    const bool sync = std::lround (plainValue (kLfoSync)) == 1;
    lfoRateHz->setVisible (!sync);
    lfoRateSync->setVisible (sync);

    for (int t = 0; t < 3; ++t)
        envTabs[t]->setVisible (t == envTab);
    const int lm = (int)std::lround (plainValue (kAmpLoopMode));
    ampLoopTime->setVisible (lm == kAmpLoopTrigger || lm == kAmpLoopLoop);
    ampLoopRate->setVisible (lm == kAmpLoopBeat || lm == kAmpLoopSync);

    if (auto* f = frame)
        f->invalid ();
}

void Editor::idle ()
{
    if (waveform)
        waveform->idle ();
    if (envDisplay)
        envDisplay->tick ();
    if (nameLabel)
    {
        std::string name = ctl->sampleDisplayName ();
        auto* b = ctl->getBridge ();
        if (b && b->sampleMissing ())
            name = "Missing: " + name;
        if (name.empty ())
            name = "No sample";
        if (name != lastName)
        {
            lastName = name;
            nameLabel->setText (name);
            if (waveform)
                waveform->resetZoom ();
        }
    }
    if (auto* b = ctl->getBridge ())
    {
        char buf[96];
        std::snprintf (buf, sizeof (buf), "Host %.1f BPM%s", b->hostBpm.load (), b->hostPlaying.load () ? "  \xE2\x96\xB6" : "");
        if (hostLabel)
            hostLabel->setText (buf);
        if (bpmLabel)
        {
            if (auto s = b->sample ())
            {
                ParamArray p {};
                for (uint32_t i = 0; i < kNumParams; ++i)
                    p[i] = plainValue (i);
                std::snprintf (buf, sizeof (buf), "Sample %.2f BPM", sampleBpmFor (*s, p));
                bpmLabel->setText (buf);
            }
            else
                bpmLabel->setText ("");
        }
        b->collectGarbage ();
    }
}

// --- sample actions ---------------------------------------------------------------
void Editor::loadFile (const std::string& path)
{
    if (!ctl->loadSample (path, true))
    {
        if (nameLabel)
            nameLabel->setText ("Could not load " + utf8FromPath (u8 (path).filename ()));
        lastName = "?";
    }
    if (waveform)
    {
        waveform->resetZoom ();
        waveform->invalid ();
    }
}

void Editor::browseForSample ()
{
    if (!frame)
        return;
    auto* sel = CNewFileSelector::create (frame, CNewFileSelector::kSelectFile);
    if (!sel)
        return;
    sel->setTitle ("Load Sample");
    sel->addFileExtension (CFileExtension ("Audio Files", "wav"));
    sel->addFileExtension (CFileExtension ("AIFF", "aif"));
    sel->addFileExtension (CFileExtension ("AIFF", "aiff"));
    sel->addFileExtension (CFileExtension ("FLAC", "flac"));
    sel->addFileExtension (CFileExtension ("MP3", "mp3"));
    if (auto* b = ctl->getBridge ())
    {
        const std::string cur = b->samplePath ();
        if (!cur.empty ())
            sel->setInitialDirectory (utf8FromPath (u8 (cur).parent_path ()).c_str ());
    }
    sel->run ([this] (CNewFileSelector* s) {
        if (s->getNumSelectedFiles () > 0)
            loadFile (s->getSelectedFile (0));
    });
    sel->forget ();
}

void Editor::stepSample (int dir)
{
    auto* b = ctl->getBridge ();
    if (!b)
        return;
    const std::string cur = b->samplePath ();
    if (cur.empty ())
    {
        browseForSample ();
        return;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path folder = u8 (cur).parent_path ();
    std::vector<fs::path> files;
    for (auto& entry : fs::directory_iterator (folder, ec))
        if (entry.is_regular_file (ec) && isSupportedAudioFile (utf8FromPath (entry.path ())) &&
            utf8FromPath (entry.path ().filename ())[0] != '.')
            files.push_back (entry.path ());
    if (files.empty ())
        return;
    std::sort (files.begin (), files.end (), [] (const fs::path& a, const fs::path& c) {
        std::string x = utf8FromPath (a.filename ()), y = utf8FromPath (c.filename ());
        std::transform (x.begin (), x.end (), x.begin (), ::tolower);
        std::transform (y.begin (), y.end (), y.begin (), ::tolower);
        return x < y;
    });
    int idx = 0;
    for (size_t i = 0; i < files.size (); ++i)
        if (files[i] == u8 (cur))
            idx = (int)i;
    idx = (idx + dir + (int)files.size ()) % (int)files.size ();
    loadFile (utf8FromPath (files[(size_t)idx]));
}

void Editor::showMenu (CPoint where)
{
    if (!frame)
        return;
    auto* b = ctl->getBridge ();
    const SampleOps ops = b ? b->sampleOps () : SampleOps {};
    const bool hasSample = b && b->sample ();
    const bool cropped = ops.cropStart > 0.0 || ops.cropEnd < 1.0;
    const int mode = (int)std::lround (plainValue (kMode));

    auto menu = makeOwned<COptionMenu> ();
    std::vector<std::function<void ()>> actions;
    auto add = [&] (const std::string& title, std::function<void ()> fn, bool enabled = true, bool checked = false) {
        int32_t flags = CMenuItem::kNoFlags;
        if (!enabled)
            flags |= CMenuItem::kDisabled;
        if (checked)
            flags |= CMenuItem::kChecked;
        menu->addEntry (title.c_str (), -1, flags);
        actions.push_back (enabled ? std::move (fn) : std::function<void ()> {});
    };
    auto sep = [&] {
        menu->addSeparator ();
        actions.push_back ({});
    };

    add ("Load Sample...", [this] { browseForSample (); });
#if defined(_WIN32)
    const char* revealTitle = "Show in Explorer";
#elif defined(__APPLE__)
    const char* revealTitle = "Show in Finder";
#else
    const char* revealTitle = "Show in File Manager";
#endif
    add (revealTitle, [b] { revealInFileBrowser (b->samplePath ()); }, hasSample);
    sep ();
    add ("Normalize Volume", [this, ops] {
             SampleOps o = ops;
             o.normalize = !o.normalize;
             ctl->applyOps (o, false);
         },
         hasSample, ops.normalize);
    add ("Reverse", [this, ops] {
             SampleOps o = ops;
             o.reverse = !o.reverse;
             ctl->applyOps (o, false);
         },
         hasSample, ops.reverse);
    add ("Crop to Sample Start/End", [this, ops, b] {
             auto s = b->sample ();
             if (!s)
                 return;
             ParamArray p {};
             for (uint32_t i = 0; i < kNumParams; ++i)
                 p[i] = plainValue (i);
             double fs, fe;
             flagRegion (*s, p, fs, fe);
             // positions in the current (cropped/reversed) sample -> original file range
             double a = fs / s->length, e = fe / s->length;
             if (ops.reverse)
             {
                 const double t = a;
                 a = 1.0 - e;
                 e = 1.0 - t;
             }
             SampleOps o = ops;
             const double span = ops.cropEnd - ops.cropStart;
             o.cropStart = ops.cropStart + a * span;
             o.cropEnd = ops.cropStart + e * span;
             ctl->applyOps (o, true);
         },
         hasSample);
    add ("Undo Crop", [this, ops] {
             SampleOps o = ops;
             o.cropStart = 0.0;
             o.cropEnd = 1.0;
             ctl->applyOps (o, false);
             ctl->setPlainFromUI (kSampleStart, 0.0);
             ctl->setPlainFromUI (kSampleEnd, 1.0);
         },
         hasSample && cropped);
    sep ();
    const bool cp = plainValue (kLoopFadePower) >= 0.5;
    add ("Use Constant Power Fade for Loops", [this, cp] { ctl->setPlainFromUI (kLoopFadePower, cp ? 0.0 : 1.0); },
         true, cp);
    if (mode == kModeSlicing)
        add ("Reset Slice Edits", [this, b] {
                 b->setEdits ({});
                 ctl->markDirty ();
             },
             hasSample);
    sep ();
    add ("Clear Sample", [this] { ctl->clearSample (); }, hasSample);
    sep ();
    for (double s : {0.75, 1.0, 1.25, 1.5, 2.0})
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "Interface Size %d%%", (int)std::lround (s * 100));
        add (buf, [this, s] { resizeTo (s); }, true, std::fabs (scale - s) < 0.01);
    }

    auto shared = std::make_shared<std::vector<std::function<void ()>>> (std::move (actions));
    menu->popup (frame, where, [shared, menu] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r >= 0 && r < (int32_t)shared->size () && (*shared)[(size_t)r])
            (*shared)[(size_t)r]();
    });
}

} // namespace smempler

#include "Editor.h"

#include "Displays.h"
#include "Engine.h"
#include "Help.h"
#include "Params.h"
#include "UiKit.h"
#include "MsView.h"
#include "Rack.h"
#include "WaveformView.h"
#include "plugin/Controller.h"

#include "pluginkit/SampleFiles.h"
#include "pluginkit/ui/ScopeView.h"
#include "pluginkit/vst/PresetBar.h"

#include "multidyn/src/ui/DynDisplay.h"
#include "multidyn/src/ui/Thresholds.h"
#include "para/src/ui/FilterView.h"
#include "para/src/ui/Help.h"
#include "multidyn/src/ui/Help.h"
#include "widr/src/ui/GonioView.h"
#include "widr/src/ui/Help.h"
#include "smacheratr/src/ui/Help.h"
#include "smacheratr/src/ui/ColorView.h"
#include "smacheratr/src/ui/ShaperView.h"

#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cvstguitimer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

using pk::NumberBox;

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

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c)
{
    // the saturator at the very end: Smacheratr's display on the tail's parameters
    satHost = std::make_unique<pk::MappedParamHost> (this, smacheratr::paramTable (), [] (uint32_t id) -> int64_t {
        switch (id)
        {
            case smacheratr::kDrive: return kTailBase + pk::kTailDrive;
            case smacheratr::kPreLimit: return kTailBase + pk::kTailPreLimit;
            case smacheratr::kPreLimitThreshold: return kTailBase + pk::kTailThreshold;
            case smacheratr::kPostClip: return kTailBase + pk::kTailPostClip;
            case smacheratr::kDryWet: return kTailBase + pk::kTailMix;
            default: return -1;
        }
    });
}

void Editor::onClose ()
{
    fxRow = fxCtl = fxBody = fxEndBody = nullptr;
    fxFilterView = nullptr;
    fxDynDisplay = nullptr;
    fxShaperView = endShaperView = nullptr;
    fxGonio = nullptr;
    msView = nullptr;
    mdLayoutHost = nullptr;
    scope = nullptr;
    for (int b = 0; b < 4; ++b)
    {
        mdNames[b] = nullptr;
        mdOn[b] = mdSolo[b] = mdIn[b] = mdOut[b] = nullptr;
        for (auto& v : mdBoxes[b])
            v = nullptr;
    }
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
    nameLabel = new Label (CRect (118, 6, 340, 28), "No sample", 12.0, true);
    root->addView (new pk::PresetBar (CRect (346, 6, 472, 28), ctl));
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
    bind (classicGroup, new Knob (knobRect (190, 24), this, kLoopFade));
    bind (classicGroup, new Toggle (CRect (256, 32, 310, 50), this, kLoopOn, "Loop"));
    bind (classicGroup, new Toggle (CRect (256, 58, 310, 76), this, kSnap, "Snap"));
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
    gp->addView (new Label (CRect (4, 280, 60, 296), "Root", 10.5, false, 0));
    bind (gp, new NumberBox (CRect (64, 278, 174, 298), this, kRootKey));

    // hover help for every parameter control

    // ---- after the sampler: the effects rack (the slots in chain order, "+" to add one, the saturator
    // at the very end on the right), and the output scope ----
    fxRow = new Group (CRect (8, kFxTabTop, 846, kFxTabTop + 20));
    root->addView (fxRow);
    fxCtl = new Group (CRect (8, kFxCtlTop, 846, kFxCtlTop + 20));
    root->addView (fxCtl);
    auto* fxp = new Panel (CRect (8, kFxPanelTop, 846, kFxPanelTop + 234));
    root->addView (fxp);
    fxBody = new Group (CRect (0, 0, 838, 234));
    fxp->addView (fxBody);
    {
        // the saturator at the very end (built once: its controls are bound)
        fxEndBody = new Group (CRect (0, 0, 838, 234));
        fxp->addView (fxEndBody);
        endShaperView = new smacheratr::ShaperView (CRect (8, 8, 330, 226), satHost.get (), [this] () -> const smacheratr::Meters* {
            auto* b = ctl->getBridge ();
            return b ? &b->satMeters : nullptr;
        });
        endShaperView->setTooltipText (smacheratr::help::kShaperDisplay);
        fxEndBody->addView (endShaperView);
        addTailPanel (fxEndBody, CRect (340, 8, 830, 90), kTailBase, "smacheratr  (the very end)");
        auto* n3 = new Label (CRect (340, 100, 830, 114), "after the rack, just before the output", 9.5);
        n3->setDim (true);
        fxEndBody->addView (n3);
    }
    scope = new pk::ScopeView (
        CRect (852, kFxTabTop, 1102, kFxPanelTop + 234),
        [this] (float* l, float* r, int n) {
            auto* b = ctl->getBridge ();
            return b ? b->outScope.read (l, r, n) : 0;
        },
        [this] {
            auto* b = ctl->getBridge ();
            return b ? b->sampleRate.load () : 48000.0;
        },
        Bridge::kScopeSize);
    scope->setTooltipText (help::kScope);
    root->addView (scope);

    applyParamTooltips (&help::forParam);

    updateVisibility ();
    rebuildRack ();
    lastName.clear ();
    idle ();
}

// --- updates ----------------------------------------------------------------------
void Editor::setNorm (uint32_t id, double v)
{
    pk::EditorBase::setNorm (id, v);
    if (isRackParam (id))
    {
        const int slot = (int)((id - kRackBase) / kSlotSize);
        const uint32_t field = (id - kRackBase) % kSlotSize;
        if (field >= kSlotParams && ctl->slotType (slot) == kFxMultidyn)
            if (auto* h = hostFor (slot))
                if (const int64_t mdId = fxIdAt (kFxMultidyn, field - kSlotParams); mdId >= 0)
                    multidyn::pushThresholds (*h, (uint32_t)mdId, v);
    }
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (waveform)
        waveform->invalid ();
    if (filterDisplay)
        filterDisplay->invalid ();
    if (envDisplay)
        envDisplay->invalid ();
    if (isRackParam (id))
    {
        const int slot = (int)((id - kRackBase) / kSlotSize);
        const uint32_t field = (id - kRackBase) % kSlotSize;
        if (field == kSlotType)
            rackDirty = true; // rebuilt in idle (a button may be what changed it)
        else if (slot == fxTab)
        {
            if (fxBody)
                fxBody->invalid ();
            if (fxCtl)
                fxCtl->invalid ();
            if (field == kSlotParams + multidyn::kBands && ctl->slotType (slot) == kFxMultidyn)
                updateMdLayout ();
        }
    }
    if (id >= kTailBase && id < kTailBase + pk::kTailFields && endShaperView)
        endShaperView->invalid ();
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

void Editor::updateMdLayout ()
{
    if (!fxDynDisplay)
        return;
    auto place = [] (VSTGUI::CView* v, const CRect& r) {
        if (v)
        {
            v->setViewSize (r);
            v->setMouseableArea (r);
        }
    };
    const double dispL = 112, dispR = 600, dispT = 8, dispB = 226;
    if (!mdLayoutHost)
        return;
    const int n = std::clamp ((int)std::lround (mdLayoutHost->plainValue (multidyn::kBands)) + 1, 1, multidyn::kMaxBands);
    const double top = dispT + multidyn::DynDisplay::kHeader;
    const double laneH = (dispB - dispT - multidyn::DynDisplay::kHeader - multidyn::DynDisplay::kScaleHeight) / n;
    const double belowX = dispL + 4, aboveX = dispR - multidyn::DynDisplay::kRightCol + 4, timeX = aboveX + 80;
    for (int b = 0; b < 4; ++b)
    {
        const bool used = b < n;
        for (VSTGUI::CView* v : {(VSTGUI::CView*)mdNames[b], mdOn[b], mdSolo[b], mdIn[b], mdOut[b]})
            if (v)
                v->setVisible (used);
        for (auto* v : mdBoxes[b])
            if (v)
                v->setVisible (used);
        if (!used)
            continue;
        const double laneTop = top + (n - 1 - b) * laneH, cy = laneTop + laneH / 2;
        const char* names[4] = {"Low", n == 4 ? "Mid 1" : "Mid", n == 4 ? "Mid 2" : "High", "High"};
        if (mdNames[b])
            mdNames[b]->setText (n == 1 ? "Full" : (b == n - 1 ? "High" : names[b]));
        place (mdNames[b], CRect (8, laneTop + 2, 48, laneTop + 16));
        place (mdOn[b], CRect (50, laneTop + 2, 78, laneTop + 16));
        place (mdSolo[b], CRect (82, laneTop + 2, 104, laneTop + 16));
        place (mdIn[b], CRect (8, laneTop + 18, 56, laneTop + 34));
        place (mdOut[b], CRect (58, laneTop + 18, 106, laneTop + 34));
        const double xs[6] = {belowX, belowX, aboveX, aboveX, timeX, timeX};
        for (int i = 0; i < 6; ++i)
        {
            const double y = i % 2 == 0 ? cy - 20 : cy + 2;
            place (mdBoxes[b][i], CRect (xs[i], y, xs[i] + 68, y + 18));
        }
    }
    if (frame)
        frame->invalid ();
}

void Editor::setFxTab (int t)
{
    // rebuilt on the next idle: the tab button that asked may be one of the views rebuilt
    fxTab = std::clamp (t, 0, (int)kFxEnd);
    rackDirty = true;
}

pk::MappedParamHost* Editor::hostFor (int slot)
{
    if (slot < 0 || slot >= kRackSlots)
        return nullptr;
    const int type = ctl->slotType (slot);
    auto& h = slotHosts[(size_t)slot];
    if (!h || slotHostType[(size_t)slot] != type)
    {
        h = std::make_unique<pk::MappedParamHost> (this, fxTable (type), [slot, type] (uint32_t id) -> int64_t {
            const int64_t j = fxBlockOf (type, id);
            return j < 0 ? -1 : (int64_t)slotBlockParam (slot, (uint32_t)j);
        });
        slotHostType[(size_t)slot] = type;
    }
    return h.get ();
}

void Editor::copySlot (int from, int to)
{
    for (uint32_t f = 0; f < kSlotSize; ++f)
        setOnce (slotParam (to, f), norm (slotParam (from, f)));
}

void Editor::addFx (int type)
{
    int slot = -1;
    for (int s = 0; s < kRackSlots && slot < 0; ++s)
        if (ctl->slotType (s) == kFxEmpty)
            slot = s;
    if (slot < 0 || type <= kFxEmpty || type >= kNumFxTypes)
        return;
    // the effect with its own defaults, on
    const auto& t = fxBlockTable (type);
    setOnce (slotParam (slot, kSlotType), paramTable ().toNormalized (slotParam (slot, kSlotType), (double)type));
    setOnce (slotParam (slot, kSlotOn), 1.0);
    for (uint32_t j = 0; j < kSlotBlock; ++j)
        setOnce (slotBlockParam (slot, j), j < t.size () ? t.defaultNormalized (j) : 0.0);
    fxTab = slot;
    rackDirty = true;
}

void Editor::removeFx (int slot)
{
    if (slot < 0 || slot >= kRackSlots)
        return;
    // the ones after it move up, so the chain stays in order without gaps
    for (int s = slot; s + 1 < kRackSlots; ++s)
        copySlot (s + 1, s);
    setOnce (slotParam (kRackSlots - 1, kSlotType), 0.0);
    setOnce (slotParam (kRackSlots - 1, kSlotOn), 1.0);
    for (uint32_t j = 0; j < kSlotBlock; ++j)
        setOnce (slotBlockParam (kRackSlots - 1, j), 0.0);
    fxTab = ctl->slotType (slot) != kFxEmpty ? slot : std::max (0, slot - 1);
    if (ctl->slotType (fxTab) == kFxEmpty)
        fxTab = kFxEnd;
    rackDirty = true;
}

void Editor::moveFx (int slot, int dir)
{
    const int other = slot + dir;
    if (slot < 0 || slot >= kRackSlots || other < 0 || other >= kRackSlots || ctl->slotType (other) == kFxEmpty)
        return;
    // swap the two slots, every value
    std::array<double, kSlotSize> keepA;
    for (uint32_t f = 0; f < kSlotSize; ++f)
        keepA[f] = norm (slotParam (slot, f));
    copySlot (other, slot);
    for (uint32_t f = 0; f < kSlotSize; ++f)
        setOnce (slotParam (other, f), keepA[f]);
    fxTab = other;
    rackDirty = true;
}

void Editor::showAddMenu (CPoint where)
{
    if (!frame)
        return;
    auto menu = makeOwned<COptionMenu> ();
    for (int t = kFxPara; t < kNumFxTypes; ++t)
        menu->addEntry (fxName (t));
    menu->popup (frame, where, [this, menu] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r >= 0)
            addFx (kFxPara + r);
    });
}

void Editor::rebuildRack ()
{
    rackDirty = false;
    if (!fxRow || !fxCtl)
        return;
    fxRow->removeAll ();
    fxCtl->removeAll ();
    int firstEmpty = -1;
    for (int s = 0; s < kRackSlots; ++s)
    {
        shownTypes[(size_t)s] = ctl->slotType (s);
        if (shownTypes[(size_t)s] == kFxEmpty && firstEmpty < 0)
            firstEmpty = s;
    }
    if (fxTab < kFxEnd && shownTypes[(size_t)fxTab] == kFxEmpty)
        fxTab = kFxEnd;
    // the slots, in chain order
    double x = 0.0;
    for (int s = 0; s < kRackSlots; ++s)
    {
        if (shownTypes[(size_t)s] == kFxEmpty)
            continue;
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%d  %s", s + 1, fxName (shownTypes[(size_t)s]));
        fxRow->addView (new ActionButton (CRect (x, 0, x + kFxTabWidth - 4, 20), buf, [this, s] { setFxTab (s); },
                                          [this, s] { return fxTab == s; }));
        x += kFxTabWidth;
    }
    if (firstEmpty >= 0)
    {
        auto* add = new ActionButton (CRect (x, 0, x + 26, 20), "+", [this, x] { showAddMenu (CPoint (8 + x, kFxTabTop + 20)); });
        add->setTooltipText ("Add an effect to the end of the rack (after the sampler; the rack runs left to right).");
        fxRow->addView (add);
    }
    fxRow->addView (new ActionButton (CRect (838 - 132, 0, 838, 20), "end: smacheratr", [this] { setFxTab (kFxEnd); },
                                      [this] { return fxTab == kFxEnd; }));
    // the selected slot's controls
    if (fxTab < kFxEnd)
    {
        const int s = fxTab;
        char buf[48];
        std::snprintf (buf, sizeof (buf), "slot %d: %s", s + 1, fxName (shownTypes[(size_t)s]));
        fxCtl->addView (new Label (CRect (0, 1, 150, 19), buf, 10.5, true, 0));
        auto* onT = new Toggle (CRect (156, 1, 200, 19), this, slotParam (s, kSlotOn), "On");
        onT->setTooltipText ("Switch this effect off (it keeps its place, and its latency).");
        fxCtl->addView (onT);
        auto* left = new ActionButton (CRect (208, 0, 234, 20), "<", [this, s] { moveFx (s, -1); });
        left->setTooltipText ("Move it earlier in the chain.");
        fxCtl->addView (left);
        auto* right = new ActionButton (CRect (238, 0, 264, 20), ">", [this, s] { moveFx (s, +1); });
        right->setTooltipText ("Move it later in the chain.");
        fxCtl->addView (right);
        auto* rm = new ActionButton (CRect (272, 0, 340, 20), "Remove", [this, s] { removeFx (s); });
        rm->setTooltipText ("Take this effect out of the rack.");
        fxCtl->addView (rm);
    }
    auto* note = new Label (CRect (fxTab < kFxEnd ? 350 : 0, 1, 838, 19),
                            fxTab < kFxEnd ? "the rack runs left to right after the sampler; right click resets a control"
                                           : "the saturator at the very end, after the rack",
                            9.5);
    note->setDim (true);
    fxCtl->addView (note);
    buildBody ();
    if (frame)
        frame->invalid ();
}

void Editor::buildBody ()
{
    if (!fxBody)
        return;
    fxBody->removeAll ();
    fxFilterView = nullptr;
    fxDynDisplay = nullptr;
    fxShaperView = nullptr;
    fxColorView = nullptr;
    fxGonio = nullptr;
    msView = nullptr;
    mdLayoutHost = nullptr;
    for (int b = 0; b < 4; ++b)
    {
        mdNames[b] = nullptr;
        mdOn[b] = mdSolo[b] = mdIn[b] = mdOut[b] = nullptr;
        for (auto& v : mdBoxes[b])
            v = nullptr;
    }
    const bool end = fxTab >= kFxEnd;
    fxEndBody->setVisible (end);
    fxBody->setVisible (!end);
    if (end)
        return;
    const int s = fxTab;
    const int type = ctl->slotType (s);
    pk::MappedParamHost* h = hostFor (s);
    auto* g = fxBody;
    const CRect none (0, 0, 0, 0);
    rackPageParams.clear ();
    auto add = [&] (CView* v, const char* tip) {
        if (tip)
            v->setTooltipText (tip);
        if (auto* pv = dynamic_cast<pk::ParamView*> (v))
            rackPageParams.insert (pv->paramId ());
        g->addView (v);
        return v;
    };
    switch (type)
    {
        case kFxPara:
        {
            using namespace para;
            auto tip = [] (uint32_t id) { return para::help::forParam (id); };
            fxFilterView = new FilterView (CRect (8, 8, 470, 226), h, [this, s] () -> para::Meters* {
                auto* b = ctl->getBridge ();
                return b ? &b->rack.para[(size_t)s] : nullptr;
            });
            add (fxFilterView, para::help::kDisplay);
            add (new Toggle (CRect (380, 30, 462, 48), h, kDragGain, "Drag Gain"), tip (kDragGain));
            add (new Segmented (CRect (480, 8, 570, 26), h, kSlope, {"12", "18", "24"}), tip (kSlope));
            add (new Segmented (CRect (576, 8, 676, 26), h, kMovement, {"Free", "Vocal"}), tip (kMovement));
            add (new Toggle (CRect (682, 8, 780, 26), h, kResLink, "Link Res"), tip (kResLink));
            const uint32_t ids[12] = {kHpFreq, kHpRes, kHpGain, kLpFreq, kLpRes, kLpGain, kSplit, kEnvAmount, kEnvAttack, kEnvDecay, kDryWet, kOutput};
            const char* names[12] = {"HP", "HP Res", "HP Gain", "LP", "LP Res", "LP Gain", "Split", "Env", "Attack", "Decay", "Dry/Wet", "Output"};
            for (int i = 0; i < 12; ++i)
                add (new Knob (knobRect (480 + (i % 6) * 58, 32 + (i / 6) * 66), h, ids[i], names[i], i == 6 || i == 7 || i == 11), tip (ids[i]));
            add (new Knob (knobRect (480, 164), h, kDipStart, "Dip"), tip (kDipStart));
            add (new Knob (knobRect (538, 164), h, kFade, "Fade"), tip (kFade));
            add (new Knob (knobRect (596, 164), h, kLpFloor, "Floor"), tip (kLpFloor));
            break;
        }
        case kFxMultidyn:
        {
            using namespace multidyn;
            auto tip = [] (uint32_t id) { return multidyn::help::forParam (id); };
            fxDynDisplay = new DynDisplay (CRect (112, 8, 600, 226), h, [this, s] () -> multidyn::Meters* {
                auto* b = ctl->getBridge ();
                return b ? &b->rack.multidyn[(size_t)s] : nullptr;
            });
            add (fxDynDisplay, multidyn::help::kDisplay);
            auto* io = new Label (CRect (8, 8, 108, 22), "In / Out", 9.5, true, 1);
            io->setDim (true);
            g->addView (io);
            const CColor below (255, 170, 60), above (110, 165, 255);
            const int fields[6] = {kBelowThresh, kBelowRatio, kAboveThresh, kAboveRatio, kAttack, kRelease};
            for (int b = 0; b < 4; ++b)
            {
                mdNames[b] = new Label (none, "", 10.0, true, 0);
                g->addView (mdNames[b]);
                mdOn[b] = add (new Toggle (none, h, bandParam (b, kBandActive), "On"), tip (bandParam (b, kBandActive)));
                mdSolo[b] = add (new Toggle (none, h, bandParam (b, kBandSolo), "S"), tip (bandParam (b, kBandSolo)));
                mdIn[b] = add (new NumberBox (none, h, bandParam (b, kBandInput)), tip (bandParam (b, kBandInput)));
                mdOut[b] = add (new NumberBox (none, h, bandParam (b, kBandOutput)), tip (bandParam (b, kBandOutput)));
                for (int i = 0; i < 6; ++i)
                    mdBoxes[b][i] = add (new NumberBox (none, h, bandParam (b, fields[i]), i < 2 ? below : (i < 4 ? above : pk::theme::kTextBright)),
                                         tip (bandParam (b, fields[i])));
            }
            add (new Segmented (CRect (608, 32, 716, 50), h, kBands, {"1", "2", "3", "4"}), tip (kBands));
            add (new Toggle (CRect (722, 32, 830, 50), h, kSoftKnee, "Soft Knee"), tip (kSoftKnee));
            add (new Segmented (CRect (608, 56, 700, 74), h, kDetector, {"Peak", "RMS"}), tip (kDetector));
            add (new Toggle (CRect (706, 56, 776, 74), h, kPreLimit, "Pre-Lim"), tip (kPreLimit));
            add (new NumberBox (CRect (780, 56, 830, 74), h, kPreLimitCeiling), tip (kPreLimitCeiling));
            auto* sp = new Label (CRect (608, 80, 830, 92), "Splits", 9.5, true, 1);
            sp->setDim (true);
            g->addView (sp);
            for (int x = 0; x < 3; ++x)
                add (new NumberBox (CRect (608 + x * 74, 94, 676 + x * 74, 112), h, (uint32_t)(kXover1 + x)), tip ((uint32_t)(kXover1 + x)));
            add (new Knob (knobRect (608, 122), h, kAmount), tip (kAmount));
            add (new Knob (knobRect (664, 122), h, kTime), tip (kTime));
            add (new Knob (knobRect (720, 122), h, kOutput, nullptr, true), tip (kOutput));
            add (new Knob (knobRect (776, 122), h, kSoften), tip (kSoften));
            auto* rl = new Label (CRect (608, 198, 680, 214), "RMS window", 9.5, true, 0);
            rl->setDim (true);
            g->addView (rl);
            add (new NumberBox (CRect (684, 196, 740, 214), h, kRmsWindow), tip (kRmsWindow));
            mdLayoutHost = h;
            updateMdLayout ();
            break;
        }
        case kFxMsEq:
        {
            msView = new MsView (CRect (8, 8, 470, 226), h, [this, s] (float& m, float& sd) {
                auto* b = ctl->getBridge ();
                m = b ? b->rack.msMid[(size_t)s].load () : 0.0f;
                sd = b ? b->rack.msSide[(size_t)s].load () : 0.0f;
            });
            add (msView, "Blue: the mid level. Orange: the side high-pass and level. Drag the handle sideways for the cutoff, "
                         "up/down for the side level, the mouse wheel for the slope (while holding the handle, or with Shift); "
                         "double-click or right click resets. Right: live mid and side levels.");
            g->addView (new Label (CRect (480, 8, 520, 26), "Slope", 10.5, false, 2));
            add (new Segmented (CRect (526, 8, 646, 26), h, mseq::kSlope, {"6", "12", "24"}), help::forParam (kMsSlope));
            add (new Knob (knobRect (480, 34), h, mseq::kSideHp), help::forParam (kMsSideHp));
            add (new Knob (knobRect (540, 34), h, mseq::kSideGain), help::forParam (kMsSideGain));
            add (new Knob (knobRect (600, 34), h, mseq::kMidGain), help::forParam (kMsMidGain));
            auto* n2 = new Label (CRect (480, 108, 834, 122), "below the side high-pass the low end is mono", 9.5);
            n2->setDim (true);
            g->addView (n2);
            break;
        }
        case kFxSmacheratr:
        {
            using namespace smacheratr;
            auto tip = [] (uint32_t id) { return smacheratr::help::forParam (id); };
            fxShaperView = new ShaperView (CRect (8, 8, 230, 226), h, [this, s] () -> const smacheratr::Meters* {
                auto* b = ctl->getBridge ();
                return b ? &b->rack.sat[(size_t)s] : nullptr;
            });
            add (fxShaperView, smacheratr::help::kShaperDisplay);
            // the colour curve and Clarity's band, as in Smacheratr
            fxColorView = new ColorView (
                CRect (236, 34, 526, 226), h,
                [this] () {
                    auto* b = ctl->getBridge ();
                    return b ? b->sampleRate.load (std::memory_order_relaxed) : 48000.0;
                },
                [this, s] () -> const smacheratr::Meters* {
                    auto* b = ctl->getBridge ();
                    return b ? &b->rack.sat[(size_t)s] : nullptr;
                });
            add (fxColorView, smacheratr::help::kColorDisplay);
            add (new Toggle (CRect (236, 8, 306, 26), h, kPreLimit, "Pre-Limit"), tip (kPreLimit));
            add (new NumberBox (CRect (310, 8, 366, 26), h, kPreLimitThreshold), tip (kPreLimitThreshold));
            add (new Toggle (CRect (372, 8, 432, 26), h, kClarity, "Clarity"), tip (kClarity));
            add (new Toggle (CRect (436, 8, 478, 26), h, kMidSide, "M/S"), tip (kMidSide));
            add (new Choice (CRect (482, 8, 580, 26), h, kPostClip), tip (kPostClip));
            add (new Toggle (CRect (584, 8, 634, 26), h, kHiQuality, "Hi-Q"), tip (kHiQuality));
            add (new Toggle (CRect (638, 8, 712, 26), h, kDcFilter, "DC Filter"), tip (kDcFilter));
            add (new Toggle (CRect (716, 8, 770, 26), h, kColorOn, "Color"), tip (kColorOn));
            const uint32_t ids[9] = {kDrive, kOutput, kDryWet, kColorLo, kColorHi, kColorFreq, kColorWidth, kClarityFreq, kClarityWidth};
            const char* names[9] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "Clarity Hz", "Clarity W"};
            for (int i = 0; i < 9; ++i)
                add (new Knob (knobRect (534 + (i % 5) * 58, 36 + (i / 5) * 76), h, ids[i], names[i], i == 3 || i == 4), tip (ids[i]));
            break;
        }
        case kFxWidr:
        {
            using namespace widr;
            auto tip = [] (uint32_t id) { return widr::help::forParam (id); };
            fxGonio = new GonioView (CRect (8, 8, 188, 226), [this, s] () -> widr::Meters* {
                auto* b = ctl->getBridge ();
                return b ? &b->rack.widr[(size_t)s] : nullptr;
            });
            add (fxGonio, widr::help::kGonio);
            add (new Segmented (CRect (196, 8, 436, 26), h, kCharacter, {"Tight", "Wide", "Epic", "Surround"}), tip (kCharacter));
            add (new Toggle (CRect (444, 8, 530, 26), h, kMonoCheck, "Mono Check"), tip (kMonoCheck));
            const uint32_t row1[7] = {widr::kWidth, kContrast, kAir, kBeyond, kMonoBelow, kGuard, kOutput};
            for (int i = 0; i < 7; ++i)
                add (new Knob (knobRect (196 + i * 62, 32), h, row1[i], nullptr, i == 6), tip (row1[i]));
            const uint32_t row2[5] = {kSize, kSpace, kDecay, kPreDelay, kDamping};
            for (int i = 0; i < 5; ++i)
                add (new Knob (knobRect (196 + i * 62, 100), h, row2[i]), tip (row2[i]));
            add (new pk::HSlider (CRect (196, 172, 506, 192), h, kDryLevel, "Dry"), tip (kDryLevel));
            add (new pk::HSlider (CRect (196, 198, 506, 218), h, kWetLevel, "Wet"), tip (kWetLevel));
            auto* n = new Label (CRect (520, 172, 834, 218), "here Widr works alone (the group awareness needs its own plug-in)", 9.5);
            n->setDim (true);
            g->addView (n);
            break;
        }
        default: break;
    }
    // for the host test (which loads the plug-in as a module and cannot look inside the editor): the
    // effect and the parameters with a control on its page, one line per page built
    if (const char* report = std::getenv ("SMEMPLER_RACK_PAGE_REPORT"))
        if (std::FILE* f = std::fopen (report, "a"))
        {
            std::fprintf (f, "%d", type);
            for (uint32_t id : rackPageParams)
                std::fprintf (f, " %u", id);
            std::fprintf (f, "\n");
            std::fclose (f);
        }
    fxBody->invalid ();
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
    if (scope)
        scope->idle ();
    if (rackDirty)
        rebuildRack ();
    else
        for (int s = 0; s < kRackSlots; ++s)
            if (ctl->slotType (s) != shownTypes[(size_t)s])
            {
                rebuildRack (); // a preset or the host changed the rack
                break;
            }
    ctl->checkLatency ();
    if (fxFilterView)
        fxFilterView->idle ();
    if (fxDynDisplay)
        fxDynDisplay->idle ();
    if (msView)
        msView->idle ();
    if (fxGonio)
        fxGonio->idle ();
    if (fxTab == kFxEnd && endShaperView)
        endShaperView->idle ();
    if (fxShaperView)
        fxShaperView->idle ();
    if (fxColorView)
        fxColorView->idle ();
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
void Editor::loadFile (const std::string& dropped)
{
    // a host's temporary file (a rendered or recorded clip dragged in) is copied somewhere lasting
    // first: the project refers to the sample by its path
    const std::string path = pk::keepIfTemporary (dropped);
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

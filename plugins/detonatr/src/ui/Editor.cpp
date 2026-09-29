#include "Editor.h"

#include "Help.h"
#include "Views.h"
#include "plugin/Controller.h"

#include "multidyn/src/ui/DynDisplay.h"
#include "multidyn/src/ui/Help.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace detonatr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Knob;
using pk::Label;
using pk::NumberBox;
using pk::Panel;
using pk::Segmented;
using pk::Toggle;

namespace {
constexpr double kKnobW = 56, kKnobH = 64;
CRect knobRect (double x, double y) { return CRect (x, y, x + kKnobW, y + kKnobH); }

class Background : public CViewContainer
{
public:
    using CViewContainer::CViewContainer;
    void drawBackgroundRect (CDrawContext* ctx, const CRect&) override
    {
        ctx->setFillColor (pk::theme::kBackground);
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ()), kDrawFilled);
        ctx->setFillColor (pk::theme::kHeader);
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), 34), kDrawFilled);
    }
};

Label* dimLabel (CViewContainer* p, const CRect& r, const char* s, double size = 9.5)
{
    auto* l = new Label (r, s, size);
    l->setDim (true);
    p->addView (l);
    return l;
}
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    tailDisplays.reset ();
    strip = nullptr;
    hit = nullptr;
    panels.fill (nullptr);
    slots.fill (nullptr);
    rootNote = nullptr;
    recordingsSeen = ~0u;
    dyn = nullptr;
    for (int b = 0; b < 4; ++b)
    {
        mbNames[b] = nullptr;
        mbOn[b] = mbSolo[b] = mbIn[b] = mbOut[b] = nullptr;
        for (auto& v : mbBoxes[b])
            v = nullptr;
    }
    curve = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "detonatr", 14.0, true));
    root->addView (new pk::PresetBar (CRect (580, 6, 776, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (784, 6, 806, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (812, 6, 892, 28), "Menu", [this] { showMenu (CPoint (812, 28)); }));

    // the chain and the master controls
    strip = new StageStrip (CRect (8, kStripTop, kStripRight, kStripBottom), this);
    strip->setTooltipText (help::kStageStrip);
    strip->onStagePicked = [this] (int s) { showStage (s); };
    root->addView (strip);
    bind (root, new Knob (knobRect (752, kStripTop - 4), this, kDryWet));
    bind (root, new Knob (knobRect (820, kStripTop - 4), this, kOutput));

    auto metersOf = [c = ctl] () -> const Meters* {
        auto* b = c->getBridge ();
        return b ? &b->meters : nullptr;
    };
    hit = new HitView (CRect (8, kHitTop, 892, kHitBottom), metersOf);
    hit->setTooltipText (help::kHitView);
    root->addView (hit);

    // the stages' pages, one shown at a time
    const CRect page (8, kStageTop, 892, kStageBottom);
    for (int s = 0; s < kNumStages; ++s)
    {
        if (s == kStageSaturator)
        {
            auto* tailPanel = addTailPanel (root, page, kTailBase, kTailExtBase, "saturator  (smacheratr: raises the dropped body back up)");
            tailDisplays = std::make_unique<smacheratr::TailDisplays> (
                this, kTailBase, kTailExtBase,
                [c = ctl] {
                    auto* b = c->getBridge ();
                    return b ? (double)b->meters.sampleRate.load () : 48000.0;
                },
                [c = ctl] () -> const smacheratr::Meters* {
                    auto* b = c->getBridge ();
                    return b ? &b->meters.sat : nullptr;
                });
            tailDisplays->add (tailPanel, CRect (10, 24, 874, 24 + smacheratr::TailDisplays::kHeight - 22));
            tailDisplays->onBandPicked ([this] (int k) { showTailBand (k); });
            panels[(size_t)s] = tailPanel;
            continue;
        }
        static const char* titles[kNumStages] = {"clean  (denoise and dereverb)", "tone  (resonators, recordings, disperser)",
                                                 "multiband  (multidyn)", "transient  (spike and drop)", ""};
        auto* p = new Panel (page, titles[s]);
        root->addView (p);
        panels[(size_t)s] = p;
        switch (s)
        {
            case kStageClean: buildClean (p); break;
            case kStageTone: buildTone (p); break;
            case kStageMultiband: buildMultiband (p); break;
            case kStageTransient: buildTransient (p); break;
            default: break;
        }
    }

    applyParamTooltips (&help::forParam);
    showStage (shown);
    updateRootNote ();
    idle ();
}

void Editor::buildClean (CViewContainer* p)
{
    bind (p, new Toggle (CRect (10, 26, 70, 44), this, kCleanOn, "On"));
    bind (p, new Knob (knobRect (10, 56), this, kDenoise));
    bind (p, new Knob (knobRect (76, 56), this, kDereverb));
    dimLabel (p, CRect (160, 60, 870, 76), "Put it first: it cleans the source before the other stages build it up (like RX's De-noise and De-reverb).");
    dimLabel (p, CRect (160, 80, 870, 96), "Denoise learns the noise floor from the quiet parts and turns down what does not stand out of it, so the tones stay.");
    dimLabel (p, CRect (160, 100, 870, 116), "Dereverb turns down the room: what is only the decaying tail of what came before. The hit itself stays.");
    dimLabel (p, CRect (160, 120, 870, 136), "Off, it only delays (so turning it off does not move the sound in time).");
}

void Editor::buildTone (CViewContainer* p)
{
    bind (p, new Toggle (CRect (10, 26, 70, 44), this, kToneOn, "On"));
    const uint32_t ids[7] = {kRoot, kDecay, kResonators, kCarriers, kToneDry, kDisperse, kDisperseFreq};
    for (int i = 0; i < 7; ++i)
        bind (p, new Knob (knobRect (80 + i * 60, 22), this, ids[i]));
    p->addView (new Label (CRect (510, 26, 570, 44), "Material", 10.5, false, 2));
    bind (p, new Segmented (CRect (576, 26, 876, 44), this, kMaterial, {"Glass", "Pot", "Pipe", "Wood", "Bell", "Bottle"}));
    rootNote = new Label (CRect (576, 52, 876, 68), "", 10.0);
    rootNote->setDim (true);
    p->addView (rootNote);

    dimLabel (p, CRect (10, 92, 876, 106), "RECORDINGS  (household items: the input's bands play them, a vocoder)", 9.5);
    for (int i = 0; i < kCarrierSlots; ++i)
    {
        const double x = 10 + i * 218;
        auto* s = new RecordingSlot (CRect (x, 110, x + 148, 190), i);
        s->setTooltipText (help::kRecording);
        s->onFileDropped = [this, i] (const std::string& path) { loadRecording (i, path); };
        s->onClick = [this, i] { browseRecording (i); };
        p->addView (s);
        slots[(size_t)i] = s;
        auto* load = new ActionButton (CRect (x, 196, x + 70, 214), "Load", [this, i] { browseRecording (i); });
        load->setTooltipText (help::kRecording);
        p->addView (load);
        auto* clear = new ActionButton (CRect (x + 78, 196, x + 148, 214), "Clear", [this, i] {
            ctl->clearRecording (i);
            refreshRecordings ();
        });
        clear->setTooltipText (help::kClearRecording);
        p->addView (clear);
        bind (p, new Knob (knobRect (x + 152, 118), this, kCarrierLevel1 + (uint32_t)i));
    }
}

void Editor::buildMultiband (CViewContainer* p)
{
    using namespace multidyn;
    bind (p, new Toggle (CRect (8, 24, 64, 40), this, kMultibandOn, "On"));
    mbHost = std::make_unique<pk::MappedParamHost> (this, multidyn::paramTable (), [] (uint32_t id) -> int64_t {
        const int64_t b = mbBlockOf (id);
        return b < 0 ? -1 : (int64_t)(kMbBase + b);
    });
    pk::MappedParamHost* h = mbHost.get ();
    auto add = [p] (CView* v, const char* tip) {
        if (tip)
            v->setTooltipText (tip);
        p->addView (v);
        return v;
    };
    auto tip = [] (uint32_t id) { return multidyn::help::forParam (id); };
    dyn = new DynDisplay (CRect (112, 24, 600, 242), h, [c = ctl] () -> multidyn::Meters* {
        auto* b = c->getBridge ();
        return b ? &b->meters.multiband : nullptr;
    });
    add (dyn, multidyn::help::kDisplay);
    const CColor below (255, 170, 60), above (110, 165, 255);
    const int fields[6] = {kBelowThresh, kBelowRatio, kAboveThresh, kAboveRatio, kAttack, kRelease};
    const CRect none (0, 0, 1, 1);
    for (int b = 0; b < 4; ++b)
    {
        mbNames[b] = new Label (none, "", 10.0, true, 0);
        p->addView (mbNames[b]);
        mbOn[b] = add (new Toggle (none, h, bandParam (b, kBandActive), "On"), tip (bandParam (b, kBandActive)));
        mbSolo[b] = add (new Toggle (none, h, bandParam (b, kBandSolo), "S"), tip (bandParam (b, kBandSolo)));
        mbIn[b] = add (new NumberBox (none, h, bandParam (b, kBandInput)), tip (bandParam (b, kBandInput)));
        mbOut[b] = add (new NumberBox (none, h, bandParam (b, kBandOutput)), tip (bandParam (b, kBandOutput)));
        for (int i = 0; i < 6; ++i)
            mbBoxes[b][i] = add (new NumberBox (none, h, bandParam (b, fields[i]), i < 2 ? below : (i < 4 ? above : pk::theme::kTextBright)),
                                 tip (bandParam (b, fields[i])));
    }
    add (new Segmented (CRect (608, 40, 716, 58), h, kBands, {"1", "2", "3", "4"}), tip (kBands));
    add (new Toggle (CRect (722, 40, 830, 58), h, kSoftKnee, "Soft Knee"), tip (kSoftKnee));
    add (new Segmented (CRect (608, 64, 700, 82), h, kDetector, {"Peak", "RMS"}), tip (kDetector));
    add (new Toggle (CRect (706, 64, 776, 82), h, kPreLimit, "Pre-Lim"), tip (kPreLimit));
    add (new NumberBox (CRect (780, 64, 830, 82), h, kPreLimitCeiling), tip (kPreLimitCeiling));
    dimLabel (p, CRect (608, 88, 830, 100), "Splits");
    for (int x = 0; x < 3; ++x)
        add (new NumberBox (CRect (608 + x * 74, 102, 676 + x * 74, 120), h, (uint32_t)(kXover1 + x)), tip ((uint32_t)(kXover1 + x)));
    add (new Knob (knobRect (608, 128), h, kAmount), tip (kAmount));
    add (new Knob (knobRect (664, 128), h, kTime), tip (kTime));
    add (new Knob (knobRect (720, 128), h, multidyn::kOutput, nullptr, true), tip (multidyn::kOutput));
    add (new Knob (knobRect (776, 128), h, kSoften), tip (kSoften));
    dimLabel (p, CRect (608, 204, 680, 220), "RMS window");
    add (new NumberBox (CRect (684, 202, 740, 220), h, kRmsWindow), tip (kRmsWindow));
    updateMbLayout ();
}

void Editor::updateMbLayout ()
{
    if (!dyn || !mbHost)
        return;
    auto place = [] (CView* v, const CRect& r) {
        if (v)
        {
            v->setViewSize (r);
            v->setMouseableArea (r);
        }
    };
    const double dispL = 112, dispR = 600, dispT = 24, dispB = 242;
    const int n = std::clamp ((int)std::lround (mbHost->plainValue (multidyn::kBands)) + 1, 1, multidyn::kMaxBands);
    const double top = dispT + multidyn::DynDisplay::kHeader;
    const double laneH = (dispB - dispT - multidyn::DynDisplay::kHeader - multidyn::DynDisplay::kScaleHeight) / n;
    const double belowX = dispL + 4, aboveX = dispR - multidyn::DynDisplay::kRightCol + 4, timeX = aboveX + 80;
    for (int b = 0; b < 4; ++b)
    {
        const bool used = b < n;
        for (CView* v : {(CView*)mbNames[b], mbOn[b], mbSolo[b], mbIn[b], mbOut[b]})
            if (v)
                v->setVisible (used);
        for (auto* v : mbBoxes[b])
            if (v)
                v->setVisible (used);
        if (!used)
            continue;
        const double laneTop = top + (n - 1 - b) * laneH, cy = laneTop + laneH / 2;
        const char* names[4] = {"Low", n == 4 ? "Mid 1" : "Mid", n == 4 ? "Mid 2" : "High", "High"};
        if (mbNames[b])
            mbNames[b]->setText (n == 1 ? "Full" : (b == n - 1 ? "High" : names[b]));
        place (mbNames[b], CRect (8, laneTop + 2, 48, laneTop + 16));
        place (mbOn[b], CRect (50, laneTop + 2, 78, laneTop + 16));
        place (mbSolo[b], CRect (82, laneTop + 2, 104, laneTop + 16));
        place (mbIn[b], CRect (8, laneTop + 18, 56, laneTop + 34));
        place (mbOut[b], CRect (58, laneTop + 18, 106, laneTop + 34));
        const double xs[6] = {belowX, belowX, aboveX, aboveX, timeX, timeX};
        for (int i = 0; i < 6; ++i)
        {
            const double y = i % 2 == 0 ? cy - 20 : cy + 2;
            place (mbBoxes[b][i], CRect (xs[i], y, xs[i] + 68, y + 18));
        }
    }
    if (panels[kStageMultiband])
        panels[kStageMultiband]->invalid ();
}

void Editor::buildTransient (CViewContainer* p)
{
    bind (p, new Toggle (CRect (10, 26, 70, 44), this, kTransientOn, "On"));
    const uint32_t ids[4] = {kSpike, kFall, kDrop, kSensitivity};
    for (int i = 0; i < 4; ++i)
        bind (p, new Knob (knobRect (10 + (i % 2) * 64, 54 + (i / 2) * 76), this, ids[i]));
    dimLabel (p, CRect (150, 60, 300, 76), "a very short spike");
    dimLabel (p, CRect (150, 76, 300, 92), "at each hit, the rest");
    dimLabel (p, CRect (150, 92, 300, 108), "turned down: put the");
    dimLabel (p, CRect (150, 108, 300, 124), "Saturator after it to");
    dimLabel (p, CRect (150, 124, 300, 140), "raise the body back up");
    dimLabel (p, CRect (150, 140, 300, 156), "dense and loud.");
    curve = new TransientCurve (CRect (310, 24, 876, 240), this);
    curve->setTooltipText (help::kTransientCurve);
    p->addView (curve);
}

void Editor::showStage (int stage)
{
    shown = std::clamp (stage, 0, kNumStages - 1);
    for (int s = 0; s < kNumStages; ++s)
        if (panels[(size_t)s])
            panels[(size_t)s]->setVisible (s == shown);
    if (strip)
    {
        strip->selected = shown;
        strip->invalid ();
    }
    if (frame)
        frame->invalid ();
}

void Editor::updateRootNote ()
{
    if (!rootNote)
        return;
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const double hz = plainValue (kRoot);
    const double midi = 69.0 + 12.0 * std::log2 (std::max (1.0, hz) / 440.0);
    const int note = (int)std::lround (midi);
    const int cents = (int)std::lround ((midi - note) * 100.0);
    char buf[64];
    // Live's octave numbering (C3 = MIDI 60)
    std::snprintf (buf, sizeof (buf), "root: %s%d  %+d ct", names[((note % 12) + 12) % 12], note / 12 - 2, cents);
    rootNote->setText (buf);
}

void Editor::refreshRecordings ()
{
    auto* b = ctl->getBridge ();
    if (!b)
        return;
    recordingsSeen = b->changeCounter.load ();
    for (int i = 0; i < kCarrierSlots; ++i)
        if (slots[(size_t)i])
            slots[(size_t)i]->setRecording (b->carrier (i), b->carrierName (i));
}

void Editor::loadRecording (int slot, const std::string& path)
{
    std::string err;
    if (!ctl->loadRecording (slot, path, err))
    {
        if (slots[(size_t)slot])
            slots[(size_t)slot]->setError (err.empty () ? "could not load it" : err);
        return;
    }
    refreshRecordings ();
}

void Editor::browseRecording (int slot)
{
    if (!frame)
        return;
    auto* sel = CNewFileSelector::create (frame, CNewFileSelector::kSelectFile);
    if (!sel)
        return;
    sel->setTitle ("Load a Recording");
    sel->addFileExtension (CFileExtension ("Audio Files", "wav"));
    sel->addFileExtension (CFileExtension ("AIFF", "aif"));
    sel->addFileExtension (CFileExtension ("AIFF", "aiff"));
    sel->addFileExtension (CFileExtension ("FLAC", "flac"));
    sel->addFileExtension (CFileExtension ("MP3", "mp3"));
    sel->run ([this, slot] (CNewFileSelector* s) {
        if (s->getNumSelectedFiles () > 0)
            loadRecording (slot, s->getSelectedFile (0));
    });
    sel->forget ();
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tailDisplays)
        tailDisplays->paramChanged (id);
    if (strip && ((id >= kOrderBase && id < kOrderBase + kNumStages) || id == kCleanOn || id == kToneOn || id == kMultibandOn ||
                  id == kTransientOn || id == kTailBase + pk::kTailOn))
        strip->invalid ();
    if (curve && (id == kSpike || id == kFall || id == kDrop || id == kTransientOn))
        curve->invalid ();
    if (id == kRoot)
        updateRootNote ();
    if (isMbParam (id))
    {
        if (mbIdAt (id - kMbBase) == multidyn::kBands)
            updateMbLayout ();
        if (panels[kStageMultiband])
            panels[kStageMultiband]->invalid ();
    }
}

void Editor::idle ()
{
    if (hit)
        hit->idle ();
    if (tailDisplays)
        tailDisplays->idle ();
    if (dyn)
        dyn->idle ();
    if (auto* b = ctl->getBridge ())
    {
        if (b->changeCounter.load () != recordingsSeen)
            refreshRecordings ();
        if (++idleCount % 60 == 0)
            b->collectGarbage ();
    }
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
    menu->popup (frame, where, [this, sizes, menu] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

} // namespace detonatr

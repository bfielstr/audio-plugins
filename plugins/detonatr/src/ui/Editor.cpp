#include "Editor.h"

#include "Help.h"
#include "Views.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/vst/PresetBar.h"

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
using pk::Panel;
using pk::Segmented;
using pk::Toggle;

namespace {
constexpr double kKnobW = 56, kKnobH = 64;
CRect knobRect (double x, double y) { return CRect (x, y, x + kKnobW, y + kKnobH); }
const CRect kOnRect (10, 24, 66, 40);

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
    displays.fill (nullptr);
}

void Editor::knobs (CViewContainer* p, std::initializer_list<uint32_t> ids, int perRow, double x0, double y0,
                    std::initializer_list<uint32_t> bipolar)
{
    int i = 0;
    for (uint32_t id : ids)
    {
        const bool bi = std::find (bipolar.begin (), bipolar.end (), id) != bipolar.end ();
        bind (p, new Knob (knobRect (x0 + (i % perRow) * kKnobDx, y0 + (i / perRow) * kKnobDy), this, id, nullptr, bi));
        ++i;
    }
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
    strip = new StageStrip (CRect (kStripLeft, kStripTop, kStripRight, kStripBottom), this);
    strip->setTooltipText (help::kStageStrip);
    strip->onPagePicked = [this] (int s) { showPage (s); };
    root->addView (strip);
    bind (root, new Knob (knobRect (772, kStripTop - 4), this, kDryWet));
    bind (root, new Knob (knobRect (832, kStripTop - 4), this, kOutput));

    auto metersOf = [c = ctl] () -> const Meters* {
        auto* b = c->getBridge ();
        return b ? &b->meters : nullptr;
    };
    hit = new HitView (CRect (8, kHitTop, 892, kHitBottom), metersOf);
    hit->setTooltipText (help::kHitView);
    root->addView (hit);

    // the pages, one shown at a time
    const CRect page (kPageLeft, kStageTop, 892, kStageBottom);
    static const char* titles[kNumStages] = {
        "vocoder  (the sound vocodes itself, like MVocoder with its own side-chain)",
        "spike  (transients per band, like Spiff)",
        "motion  (a Doppler swarm, like SpinTracer)",
        "transient 1  (like Oxford TransMod)",
        "limiter 1  (like Pro-L2: true peak, 4x)",
        "transient 2  (like Oxford TransMod)",
        "comp 1  (multiband up and down to a target, like Pro-C 3's TTM)",
        "comp 2  (multiband up and down to a target, like Pro-C 3's TTM)",
        "tape  (two bands of warm tape, like Saturn 2)",
        "limiter 2  (like Pro-L2: true peak, 4x)"};
    for (int s = 0; s < kNumStages; ++s)
    {
        auto* p = new Panel (page, titles[s]);
        root->addView (p);
        panels[(size_t)s] = p;
        bind (p, new Toggle (kOnRect, this, stageOnParam (s), "On"));
        switch (s)
        {
            case kStageVocoder: buildVocoder (p); break;
            case kStageSpike: buildSpike (p); break;
            case kStageMotion: buildMotion (p); break;
            case kStageTransient1: buildTransient (p, 0); break;
            case kStageLimiter1: buildLimiter (p, 0); break;
            case kStageTransient2: buildTransient (p, 1); break;
            case kStageComp1: buildComp (p, 0); break;
            case kStageComp2: buildComp (p, 1); break;
            case kStageTape: buildTape (p); break;
            case kStageLimiter2: buildLimiter (p, 1); break;
            default: break;
        }
    }
    // the Smacheratr at the end of the chain
    auto* tailPanel = addTailPanel (root, page, kTailBase, kTailExtBase, kTailExt2Base, "smacheratr  (the end of the chain, after Dry/Wet and Output)");
    tailDisplays = std::make_unique<smacheratr::TailDisplays> (
        this, kTailBase, kTailExtBase, kTailExt2Base,
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
    panels[(size_t)kNumStages] = tailPanel;

    applyParamTooltips (&help::forParam);
    showPage (shown);
    idle ();
}

void Editor::buildVocoder (CViewContainer* p)
{
    bind (p, new Segmented (CRect (76, 24, 250, 40), this, kVocOrder, {"Gentle", "Medium", "Steep"}));
    knobs (p, {kVocBands, kVocLow, kVocHigh, kVocAttack, kVocRelease, kVocRatio}, 3);
    dimLabel (p, CRect (10, 190, 250, 204), "Each band times its own level:");
    dimLabel (p, CRect (10, 204, 250, 218), "the loud bands and their attacks");
    dimLabel (p, CRect (10, 218, 250, 232), "come out louder. Ratio blends it in.");
    auto* v = new VocoderView (CRect (260, 24, 876, 240), this, [c = ctl] () -> const Meters* {
        auto* b = c->getBridge ();
        return b ? &b->meters : nullptr;
    });
    v->setTooltipText (help::kVocoderView);
    p->addView (v);
    displays[kStageVocoder] = v;
}

void Editor::buildSpike (CViewContainer* p)
{
    bind (p, new Segmented (CRect (76, 24, 190, 40), this, kSpkMode, {"Cut", "Boost"}));
    knobs (p, {kSpkDepth, kSpkSensitivity, kSpkDecay, kSpkSharpness, kSpkDecayTilt, kSpkLink, kSpkLow, kSpkHigh, kSpkMix, kSpkTrim}, 5,
           kKnobX, kKnobY, {kSpkDecayTilt, kSpkTrim});
    auto* v = new SpikeView (CRect (320, 24, 876, 240), this, [c = ctl] () -> const Meters* {
        auto* b = c->getBridge ();
        return b ? &b->meters : nullptr;
    });
    v->setTooltipText (help::kSpikeView);
    p->addView (v);
    displays[kStageSpike] = v;
}

void Editor::buildMotion (CViewContainer* p)
{
    bind (p, new Segmented (CRect (76, 24, 190, 40), this, kMotPattern, {"Orbit", "Swarm"}));
    bind (p, new Toggle (CRect (196, 24, 250, 40), this, kMotFloor, "Floor"));
    knobs (p, {kMotOrbs, kMotSpeed, kMotDistance, kMotRadius, kMotSpread, kMotRandom, kMotMix}, 4);
    dimLabel (p, CRect (10, 190, 250, 204), "Speed in m/s, Distance and Radius in m.");
    dimLabel (p, CRect (10, 204, 250, 218), "True Doppler: the pitch follows each");
    dimLabel (p, CRect (10, 218, 250, 232), "orb's speed towards or away from you.");
    auto* v = new OrbView (CRect (260, 24, 876, 240), this, [c = ctl] () -> const Meters* {
        auto* b = c->getBridge ();
        return b ? &b->meters : nullptr;
    });
    v->setTooltipText (help::kOrbView);
    p->addView (v);
    displays[kStageMotion] = v;
}

void Editor::buildTransient (CViewContainer* p, int i)
{
    const uint32_t b = kTransientBase[i];
    knobs (p, {b + kTrGain, b + kTrThreshold, b + kTrDeadband, b + kTrRatio, b + kTrOvershoot, b + kTrRise, b + kTrRecovery, b + kTrOverdrive,
               b + kTrOutput, b + kTrMix},
           5, kKnobX, kKnobY, {b + kTrGain, b + kTrRatio, b + kTrOutput});
    auto* v = new HistoryView (CRect (320, 24, 876, 240), "GAIN (dB): ATTACKS UP, SUSTAIN DOWN", 18.0, [c = ctl, i] (float& up, float& dn) {
        auto* br = c->getBridge ();
        if (!br)
            return false;
        up = br->meters.trBoostDb[i].exchange (0.0f);
        dn = br->meters.trCutDb[i].exchange (0.0f);
        return true;
    });
    v->setTooltipText (help::kTransientView);
    p->addView (v);
    displays[(size_t)(i == 0 ? kStageTransient1 : kStageTransient2)] = v;
}

void Editor::buildLimiter (CViewContainer* p, int i)
{
    const uint32_t b = kLimiterBase[i];
    bind (p, new Toggle (CRect (76, 24, 166, 40), this, b + kLimTruePeak, "True Peak"));
    knobs (p, {b + kLimGain, b + kLimCeiling, b + kLimLookahead, b + kLimAttack, b + kLimRelease, b + kLimLink}, 3);
    dimLabel (p, CRect (10, 190, 250, 204), "Look-ahead brickwall: the ceiling holds.");
    dimLabel (p, CRect (10, 204, 250, 218), "The latency stays the same whatever");
    dimLabel (p, CRect (10, 218, 250, 232), "the Lookahead.");
    auto* v = new HistoryView (CRect (260, 24, 876, 240), "GAIN REDUCTION (dB)", -24.0, [c = ctl, i] (float& up, float& dn) {
        auto* br = c->getBridge ();
        if (!br)
            return false;
        up = 0.0f;
        dn = -br->meters.limReductionDb[i].exchange (0.0f);
        return true;
    });
    v->setTooltipText (help::kLimiterView);
    p->addView (v);
    displays[(size_t)(i == 0 ? kStageLimiter1 : kStageLimiter2)] = v;
}

void Editor::buildComp (CViewContainer* p, int i)
{
    const uint32_t b = kCompBase[i];
    bind (p, new Toggle (CRect (76, 24, 146, 40), this, b + kCompAutoThreshold, "Auto Thr"));
    bind (p, new Toggle (CRect (150, 24, 220, 40), this, b + kCompAutoRelease, "Auto Rel"));
    bind (p, new Toggle (CRect (224, 24, 300, 40), this, b + kCompAutoGain, "Auto Gain"));
    knobs (p, {b + kCompThreshold, b + kCompRatio, b + kCompAttack, b + kCompRelease, b + kCompHold, b + kCompKnee, b + kCompRange, b + kCompDry,
               b + kCompXoverLow, b + kCompXoverHigh, b + kCompOutput},
           6, kKnobX, kKnobY, {b + kCompOutput});
    auto* v = new TtmView (CRect (380, 24, 876, 240), i, this, [c = ctl] () -> const Meters* {
        auto* br = c->getBridge ();
        return br ? &br->meters : nullptr;
    });
    v->setTooltipText (help::kTtmView);
    p->addView (v);
    displays[(size_t)(i == 0 ? kStageComp1 : kStageComp2)] = v;
}

void Editor::buildTape (CViewContainer* p)
{
    dimLabel (p, CRect (10, 66, 44, 80), "Low", 10.0);
    dimLabel (p, CRect (10, 136, 44, 150), "High", 10.0);
    knobs (p, {kTapeLowDrive, kTapeLowMix, kTapeLowDyn, kTapeLowLevel, kTapeHighDrive, kTapeHighMix, kTapeHighDyn, kTapeHighLevel}, 4, 46.0,
           kKnobY, {kTapeLowDyn, kTapeLowLevel, kTapeHighDyn, kTapeHighLevel});
    bind (p, new Knob (knobRect (296, kKnobY), this, kTapeSplit));
    dimLabel (p, CRect (10, 204, 360, 218), "Linear-phase split, 4x oversampled. Drive changes the colour,");
    dimLabel (p, CRect (10, 218, 360, 232), "not much the level (matched at -18 dBFS).");
    auto* v = new TapeView (CRect (370, 24, 876, 240), this);
    v->setTooltipText (help::kTapeView);
    p->addView (v);
    displays[kStageTape] = v;
}

void Editor::showPage (int page)
{
    shown = std::clamp (page, 0, kPages - 1);
    for (int s = 0; s < kPages; ++s)
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

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tailDisplays)
        tailDisplays->paramChanged (id);
    const int stage = stageOfParam (id);
    if (strip && ((id >= kOrderBase && id < kOrderBase + kNumStages) || id == kTailBase + pk::kTailOn ||
                  (stage >= 0 && id == stageOnParam (stage))))
        strip->invalid ();
    if (stage >= 0 && displays[(size_t)stage])
        displays[(size_t)stage]->invalid ();
}

void Editor::idle ()
{
    if (hit)
        hit->idle ();
    if (tailDisplays)
        tailDisplays->idle ();
    if (auto* v = dynamic_cast<VocoderView*> (displays[kStageVocoder]))
        v->idle ();
    if (auto* v = dynamic_cast<SpikeView*> (displays[kStageSpike]))
        v->idle ();
    if (auto* v = dynamic_cast<OrbView*> (displays[kStageMotion]))
        v->idle ();
    for (int s : {kStageTransient1, kStageTransient2, kStageLimiter1, kStageLimiter2})
        if (auto* v = dynamic_cast<HistoryView*> (displays[(size_t)s]))
            v->idle ();
    for (int s : {kStageComp1, kStageComp2})
        if (auto* v = dynamic_cast<TtmView*> (displays[(size_t)s]))
            v->idle ();
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
    menu->popup (frame, where, [this, sizes, menu, settingsAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

} // namespace detonatr

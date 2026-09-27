#include "Editor.h"

#include "DynDisplay.h"
#include "Help.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

namespace multidyn {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Group;
using pk::Knob;
using pk::Label;
using pk::Panel;
using pk::Segmented;
using pk::Toggle;

namespace {
constexpr double kKnobW = 56, kKnobH = 64;
CRect knobRect (double x, double y) { return CRect (x, y, x + kKnobW, y + kKnobH); }
constexpr double kLaneTop = 42, kLaneStep = 98;
double laneY (int band) { return kLaneTop + (kNumBands - 1 - band) * kLaneStep; } // High on top

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
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose () { display = nullptr; }

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    auto tip = [] (CView* v, const char* t) {
        v->setTooltipText (t);
        return v;
    };

    // top bar
    root->addView (new Label (CRect (12, 6, 200, 28), "MULTIDYN", 14.0, true));
    scStatus = new Label (CRect (200, 6, 560, 28), "", 10.5);
    scStatus->setDim (true);
    root->addView (scStatus);
    root->addView (tip (new ActionButton (CRect (590, 6, 612, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                          [this] { return tooltipsEnabled (); }),
                        "Show or hide these help tooltips."));
    root->addView (new ActionButton (CRect (618, 6, 670, 28), "Menu", [this] { showMenu (CPoint (618, 28)); }));
    const char* tabNames[] = {"T", "B", "A"};
    tabButtons.clear ();
    for (int i = 0; i < 3; ++i)
    {
        auto* b = new ActionButton (CRect (716 + i * 58, 6, 770 + i * 58, 28), tabNames[i], [this, i] { setColumn (i); },
                                    [this, i] { return column == i; });
        tip (b, help::kTabs);
        root->addView (b);
        tabButtons.push_back (b);
    }

    // crossover column + per-band controls
    for (int b = 0; b < kNumBands; ++b)
    {
        const double y = laneY (b);
        if (b == kHigh || b == kLow)
        {
            const uint32_t onId = b == kHigh ? kHighOn : kLowOn;
            const uint32_t fId = b == kHigh ? kHighFreq : kLowFreq;
            bind (root, new Toggle (CRect (8, y + 4, 92, y + 24), this, onId, b == kHigh ? "High" : "Low"));
            bind (root, new Knob (knobRect (22, y + 28), this, fId, "Freq"));
        }
        else
        {
            auto* l = new Label (CRect (8, y + 40, 92, y + 56), "Mid band", 10.5, false, 1);
            l->setDim (true);
            root->addView (l);
        }
        bind (root, new Toggle (CRect (98, y + 4, 140, y + 24), this, bandParam (b, kBandActive), "On"));
        bind (root, new Toggle (CRect (142, y + 4, 162, y + 24), this, bandParam (b, kBandSolo), "S"));
        bind (root, new Knob (knobRect (102, y + 28), this, bandParam (b, kBandInput), "Input", true));
        bind (root, new Knob (knobRect (646, y + 16), this, bandParam (b, kBandOutput), "Output", true));
    }

    display = new DynDisplay (CRect (168, kLaneTop, 640, kLaneTop + 3 * kLaneStep + DynDisplay::kScaleHeight - 4), this, ctl);
    display->setTooltipText (help::kDisplay);
    root->addView (display);

    // right column: T / B / A
    const int fields[3][2] = {{kAttack, kRelease}, {kBelowThresh, kBelowRatio}, {kAboveThresh, kAboveRatio}};
    const char* labels[3][2] = {{"Attack", "Release"}, {"Below", "Ratio"}, {"Above", "Ratio"}};
    for (int m = 0; m < 3; ++m)
    {
        columns[m] = new Group (CRect (706, 0, 900, kHeight));
        root->addView (columns[m]);
        for (int b = 0; b < kNumBands; ++b)
            for (int k = 0; k < 2; ++k)
                bind (columns[m], new Knob (knobRect (14 + k * 88, laneY (b) + 16), this, bandParam (b, fields[m][k]), labels[m][k]));
    }

    // global section
    auto* gp = new Panel (CRect (8, 346, 560, 476), "GLOBAL");
    root->addView (gp);
    bind (gp, new Knob (knobRect (14, 24), this, kOutput, nullptr, true));
    bind (gp, new Knob (knobRect (80, 24), this, kAmount));
    bind (gp, new Knob (knobRect (146, 24), this, kTime));
    bind (gp, new Toggle (CRect (220, 40, 310, 60), this, kSoftKnee, "Soft Knee"));
    gp->addView (new Label (CRect (330, 24, 450, 38), "Detector", 10.5, false, 1));
    bind (gp, new Segmented (CRect (330, 40, 450, 60), this, kDetector, {"Peak", "RMS"}));

    auto* sp = new Panel (CRect (566, 346, 892, 476), "SIDECHAIN");
    root->addView (sp);
    bind (sp, new Toggle (CRect (14, 40, 74, 60), this, kScOn, "On"));
    bind (sp, new Knob (knobRect (86, 24), this, kScGain, "Gain", true));
    bind (sp, new Knob (knobRect (150, 24), this, kScMix, "Dry/Wet"));
    bind (sp, new Toggle (CRect (222, 40, 306, 60), this, kScListen, "Listen"));

    applyParamTooltips (&help::forParam);
    setColumn (column);
    updateVisibility ();
    idle ();
}

void Editor::setColumn (int m)
{
    column = std::clamp (m, 0, 2);
    for (int i = 0; i < 3; ++i)
        if (columns[i])
            columns[i]->setVisible (i == column);
    for (auto* b : tabButtons)
        b->invalid ();
    if (frame)
        frame->invalid ();
}

void Editor::updateVisibility ()
{
    // grey out the controls of bands that are switched off
    const bool lowOn = plainValue (kLowOn) >= 0.5, highOn = plainValue (kHighOn) >= 0.5;
    for (int b = 0; b < kNumBands; ++b)
    {
        const bool used = b == kMid || (b == kLow ? lowOn : highOn);
        for (uint32_t id = bandParam (b, 0); id < bandParam (b, 0) + kBandBlock; ++id)
        {
            auto it = byParam.find (id);
            if (it != byParam.end ())
                for (auto* v : it->second)
                    if (auto* pv = dynamic_cast<pk::ParamView*> (v))
                        pv->setEnabledLook (used);
        }
    }
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (display)
        display->invalid ();
    if (id == kLowOn || id == kHighOn)
        updateVisibility ();
}

void Editor::idle ()
{
    if (display)
        display->idle ();
    if (scStatus)
    {
        const bool on = plainValue (kScOn) >= 0.5;
        Meters* m = ctl->getMeters ();
        const bool connected = m && m->sidechainConnected.load ();
        scStatus->setText (!on          ? ""
                           : connected ? "Side-chain: connected"
                                       : "Side-chain on, but no signal routed to inputs 3/4");
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
    menu->addSeparator ();
    menu->addEntry ("Reset All Ratios to 1:1");
    menu->popup (frame, where, [this, sizes, menu] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
        else if (r == (int32_t)sizes.size () + 1)
            for (int b = 0; b < kNumBands; ++b)
                for (int f : {kAboveRatio, kBelowRatio})
                    ctl->setPlainFromUI (bandParam (b, f), 1.0);
    });
}

} // namespace multidyn

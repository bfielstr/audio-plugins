#include "Editor.h"

#include "DynDisplay.h"
#include "Help.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace multidyn {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::NumberBox;
using pk::Panel;
using pk::Segmented;
using pk::Toggle;

namespace {
constexpr double kKnobW = 56, kKnobH = 64;
CRect knobRect (double x, double y) { return CRect (x, y, x + kKnobW, y + kKnobH); }
const CColor kBelowColor (255, 170, 60), kAboveColor (110, 165, 255);

void place (CView* v, const CRect& r)
{
    if (!v)
        return;
    v->setViewSize (r);
    v->setMouseableArea (r);
}

std::string bandName (int band, int bands)
{
    if (bands == 1)
        return "Full";
    if (band == 0)
        return "Low";
    if (band == bands - 1)
        return "High";
    return bands == 3 ? "Mid" : (band == 1 ? "Mid 1" : "Mid 2");
}

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

void Editor::onClose ()
{
    display = nullptr;
    scStatus = nullptr;
    for (int b = 0; b < kMaxBands; ++b)
    {
        nameLabels[b] = nullptr;
        onToggles[b] = soloToggles[b] = inputKnobs[b] = outputKnobs[b] = nullptr;
        for (auto& v : valueBoxes[b])
            v = nullptr;
    }
    for (auto& v : xoverBoxes)
        v = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    auto tip = [] (CView* v, const char* t) {
        v->setTooltipText (t);
        return v;
    };

    // top bar
    root->addView (new Label (CRect (12, 6, 150, 28), "MULTIDYN", 14.0, true));
    root->addView (new Label (CRect (160, 6, 205, 28), "Bands", 10.5, false, 2));
    bind (root, new Segmented (CRect (210, 7, 330, 27), this, kBands, {"1", "2", "3", "4"}));
    scStatus = new Label (CRect (344, 6, 560, 28), "", 10.5);
    root->addView (new pk::PresetBar (CRect (570, 6, 778, 28), ctl));
    scStatus->setDim (true);
    root->addView (scStatus);
    root->addView (tip (new ActionButton (CRect (786, 6, 808, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                          [this] { return tooltipsEnabled (); }),
                        "Show or hide these help tooltips."));
    root->addView (new ActionButton (CRect (814, 6, 912, 28), "Menu", [this] { showMenu (CPoint (814, 28)); }));

    // column headers outside the display
    auto header = [&] (double x0, double x1, const char* t) {
        auto* l = new Label (CRect (x0, kDisplayTop, x1, kDisplayTop + 14), t, 9.5, true, 1);
        l->setDim (true);
        root->addView (l);
    };
    header (kBandColLeft, kBandColLeft + 92, "Split");
    header (kInputColLeft, kInputColLeft + kKnobW, "Input");
    header (kOutputColLeft, kOutputColLeft + kKnobW, "Output");

    display = new DynDisplay (CRect (kDisplayLeft, kDisplayTop, kDisplayRight, kDisplayBottom), this,
                              [c = ctl] { return c->getMeters (); });
    display->setTooltipText (help::kDisplay);
    root->addView (display);

    // per band: the positions are set in updateLayout()
    const CRect none (0, 0, 0, 0);
    for (int b = 0; b < kMaxBands; ++b)
    {
        nameLabels[b] = new Label (none, "", 10.5, true, 0);
        root->addView (nameLabels[b]);
        onToggles[b] = bind (root, new Toggle (none, this, bandParam (b, kBandActive), "On"));
        soloToggles[b] = bind (root, new Toggle (none, this, bandParam (b, kBandSolo), "S"));
        inputKnobs[b] = bind (root, new Knob (none, this, bandParam (b, kBandInput), "In", true));
        outputKnobs[b] = bind (root, new Knob (none, this, bandParam (b, kBandOutput), "Out", true));
        const int fields[6] = {kBelowThresh, kBelowRatio, kAboveThresh, kAboveRatio, kAttack, kRelease};
        for (int i = 0; i < 6; ++i)
            valueBoxes[b][i] = bind (root, new NumberBox (none, this, bandParam (b, fields[i]),
                                                          i < 2 ? kBelowColor : (i < 4 ? kAboveColor : pk::theme::kTextBright)));
    }
    for (int x = 0; x < kMaxBands - 1; ++x)
        xoverBoxes[x] = bind (root, new NumberBox (none, this, (uint32_t)(kXover1 + x)));

    // global column
    bind (root, new Knob (knobRect (kGlobalColLeft, 44), this, kOutput, nullptr, true));
    bind (root, new Knob (knobRect (kGlobalColLeft, 132), this, kTime));
    bind (root, new Knob (knobRect (kGlobalColLeft, 220), this, kAmount));

    // bottom row
    bind (root, new Segmented (CRect (8, 352, 128, 372), this, kMode, {"Base", "Character"}));
    bind (root, new Toggle (CRect (136, 352, 218, 372), this, kSoftKnee, "Soft Knee"));
    bind (root, new Segmented (CRect (226, 352, 338, 372), this, kDetector, {"Peak", "RMS"}));
    auto* sp = new Panel (CRect (350, 344, 640, 416), "SIDECHAIN");
    root->addView (sp);
    bind (sp, new Toggle (CRect (12, 30, 66, 50), this, kScOn, "On"));
    bind (sp, new Knob (knobRect (78, 6), this, kScGain, "Gain", true));
    bind (sp, new Knob (knobRect (144, 6), this, kScMix, "Dry/Wet"));
    bind (sp, new Toggle (CRect (214, 30, 280, 50), this, kScListen, "Listen"));
    auto* lp = new Panel (CRect (648, 344, 822, 416), "PRE-LIMIT");
    root->addView (lp);
    bind (lp, new Toggle (CRect (12, 30, 66, 50), this, kPreLimit, "On"));
    bind (lp, new Knob (knobRect (96, 6), this, kPreLimitCeiling));
    // the end-of-chain Smacheratr, after the Output gain
    addTailPanel (root, CRect (8, 424, 912, 502), kSatOn);

    applyParamTooltips (&help::forParam);
    updateLayout ();
    idle ();
}

void Editor::updateLayout ()
{
    const int n = std::clamp ((int)std::lround (plainValue (kBands)) + 1, 1, kMaxBands);
    const double top = kDisplayTop + DynDisplay::kHeader;
    const double laneH = (kDisplayBottom - kDisplayTop - DynDisplay::kHeader - DynDisplay::kScaleHeight) / n;
    const double belowX = kDisplayLeft + 4, aboveX = kDisplayRight - DynDisplay::kRightCol + 4, timeX = aboveX + 80;
    for (int b = 0; b < kMaxBands; ++b)
    {
        const bool used = b < n;
        const double laneTop = top + (n - 1 - b) * laneH, cy = laneTop + laneH / 2;
        for (CView* v : {static_cast<CView*> (nameLabels[b]), onToggles[b], soloToggles[b], inputKnobs[b], outputKnobs[b]})
            if (v)
                v->setVisible (used);
        for (auto* v : valueBoxes[b])
            if (v)
                v->setVisible (used);
        if (!used)
            continue;
        nameLabels[b]->setText (bandName (b, n));
        place (nameLabels[b], CRect (kBandColLeft, laneTop + 12, kBandColLeft + 48, laneTop + 26));
        place (onToggles[b], CRect (kBandColLeft + 50, laneTop + 12, kBandColLeft + 76, laneTop + 26));
        place (soloToggles[b], CRect (kBandColLeft + 80, laneTop + 12, kBandColLeft + 98, laneTop + 26));
        const double ky = std::max (laneTop + 1, cy - kKnobH / 2);
        place (inputKnobs[b], knobRect (kInputColLeft, ky));
        place (outputKnobs[b], knobRect (kOutputColLeft, ky));
        const double xs[6] = {belowX, belowX, aboveX, aboveX, timeX, timeX};
        for (int i = 0; i < 6; ++i)
        {
            const double y = i % 2 == 0 ? cy - 20 : cy + 2;
            place (valueBoxes[b][i], CRect (xs[i], y, xs[i] + 68, y + 18));
        }
    }
    for (int x = 0; x < kMaxBands - 1; ++x)
    {
        const bool used = x < n - 1;
        xoverBoxes[x]->setVisible (used);
        if (used)
        {
            const double boundary = top + (n - 1 - x) * laneH; // between band x (below) and x + 1 (above)
            place (xoverBoxes[x], CRect (kBandColLeft + 14, boundary - 9, kBandColLeft + 84, boundary + 9));
        }
    }
    if (frame)
        frame->invalid ();
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (display)
        display->invalid ();
    if (id == kBands)
        updateLayout ();
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
            for (int b = 0; b < kMaxBands; ++b)
                for (int f : {kAboveRatio, kBelowRatio})
                    ctl->setPlainFromUI (bandParam (b, f), 1.0);
    });
}

} // namespace multidyn

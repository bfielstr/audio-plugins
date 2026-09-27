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

std::string shortHz (double hz)
{
    char buf[32];
    if (hz >= 1000.0)
        std::snprintf (buf, sizeof (buf), hz >= 10000.0 ? "%.0fk" : "%.1fk", hz / 1000.0);
    else
        std::snprintf (buf, sizeof (buf), "%.0f", hz);
    return buf;
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
    for (int b = 0; b < kMaxBands; ++b)
    {
        bandColumns[b] = nullptr;
        rangeLabels[b] = nullptr;
        for (auto& g : modeGroups[b])
            g = nullptr;
    }
    for (auto& k : xoverKnobs)
        k = nullptr;
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
    scStatus = new Label (CRect (344, 6, 610, 28), "", 10.5);
    scStatus->setDim (true);
    root->addView (scStatus);
    const char* tabNames[] = {"T", "B", "A"};
    tabButtons.clear ();
    for (int i = 0; i < 3; ++i)
    {
        auto* b = new ActionButton (CRect (620 + i * 52, 6, 668 + i * 52, 28), tabNames[i], [this, i] { setColumn (i); },
                                    [this, i] { return column == i; });
        tip (b, help::kTabs);
        root->addView (b);
        tabButtons.push_back (b);
    }
    root->addView (tip (new ActionButton (CRect (786, 6, 808, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                          [this] { return tooltipsEnabled (); }),
                        "Show or hide these help tooltips."));
    root->addView (new ActionButton (CRect (814, 6, 912, 28), "Menu", [this] { showMenu (CPoint (814, 28)); }));

    display = new DynDisplay (CRect (8, kDisplayTop, 912, kDisplayBottom), this, ctl);
    display->setTooltipText (help::kDisplay);
    root->addView (display);

    // one column per band (lowest on the left), crossover knobs in between
    const int fields[3][2] = {{kAttack, kRelease}, {kBelowThresh, kBelowRatio}, {kAboveThresh, kAboveRatio}};
    const char* labels[3][2] = {{"Attack", "Release"}, {"Below", "Ratio"}, {"Above", "Ratio"}};
    for (int b = 0; b < kMaxBands; ++b)
    {
        const double x = kColumnX + b * kColumnStep;
        char title[16];
        std::snprintf (title, sizeof (title), "BAND %d", b + 1);
        auto* col = new Panel (CRect (x, kColumnTop, x + kColumnW, kColumnTop + 182), title);
        root->addView (col);
        bandColumns[b] = col;
        bind (col, new Toggle (CRect (62, 3, 98, 17), this, bandParam (b, kBandActive), "On"));
        bind (col, new Toggle (CRect (102, 3, 122, 17), this, bandParam (b, kBandSolo), "S"));
        rangeLabels[b] = new Label (CRect (8, 20, 172, 34), "", 10.0, false, 1);
        rangeLabels[b]->setDim (true);
        col->addView (rangeLabels[b]);
        bind (col, new Knob (knobRect (18, 38), this, bandParam (b, kBandInput), "Input", true));
        bind (col, new Knob (knobRect (106, 38), this, bandParam (b, kBandOutput), "Output", true));
        for (int m = 0; m < 3; ++m)
        {
            modeGroups[b][m] = new Group (CRect (0, 104, kColumnW, 176));
            col->addView (modeGroups[b][m]);
            for (int k = 0; k < 2; ++k)
                bind (modeGroups[b][m], new Knob (knobRect (18 + k * 88, 4), this, bandParam (b, fields[m][k]), labels[m][k]));
        }
        if (b < kMaxBands - 1)
        {
            const double gx = x + kColumnW;
            char xl[8];
            std::snprintf (xl, sizeof (xl), "X%d", b + 1);
            xoverKnobs[b] = bind (root, new Knob (CRect (gx + 2, kColumnTop + 60, gx + 58, kColumnTop + 124), this,
                                                 (uint32_t)(kXover1 + b), pk::make::keep (xl)));
        }
    }

    // global section
    auto* gp = new Panel (CRect (8, 458, 560, 572), "GLOBAL");
    root->addView (gp);
    bind (gp, new Knob (knobRect (14, 24), this, kOutput, nullptr, true));
    bind (gp, new Knob (knobRect (80, 24), this, kAmount));
    bind (gp, new Knob (knobRect (146, 24), this, kTime));
    bind (gp, new Toggle (CRect (220, 40, 310, 60), this, kSoftKnee, "Soft Knee"));
    gp->addView (new Label (CRect (330, 24, 450, 38), "Detector", 10.5, false, 1));
    bind (gp, new Segmented (CRect (330, 40, 450, 60), this, kDetector, {"Peak", "RMS"}));

    auto* sp = new Panel (CRect (566, 458, 912, 572), "SIDECHAIN");
    root->addView (sp);
    bind (sp, new Toggle (CRect (14, 40, 74, 60), this, kScOn, "On"));
    bind (sp, new Knob (knobRect (96, 24), this, kScGain, "Gain", true));
    bind (sp, new Knob (knobRect (170, 24), this, kScMix, "Dry/Wet"));
    bind (sp, new Toggle (CRect (246, 40, 330, 60), this, kScListen, "Listen"));

    applyParamTooltips (&help::forParam);
    setColumn (column);
    updateVisibility ();
    idle ();
}

void Editor::setColumn (int m)
{
    column = std::clamp (m, 0, 2);
    updateVisibility ();
    for (auto* b : tabButtons)
        b->invalid ();
}

void Editor::updateVisibility ()
{
    const int n = std::clamp ((int)std::lround (plainValue (kBands)) + 1, 1, kMaxBands);
    for (int b = 0; b < kMaxBands; ++b)
    {
        if (bandColumns[b])
            bandColumns[b]->setVisible (b < n);
        for (int m = 0; m < 3; ++m)
            if (modeGroups[b][m])
                modeGroups[b][m]->setVisible (m == column);
        if (b < kMaxBands - 1 && xoverKnobs[b])
            xoverKnobs[b]->setVisible (b < n - 1);
        if (rangeLabels[b])
        {
            const double lo = b == 0 ? 0.0 : plainValue ((uint32_t)(kXover1 + b - 1));
            const double hi = b == n - 1 ? 0.0 : plainValue ((uint32_t)(kXover1 + b));
            std::string t = b == 0 ? "below " + shortHz (hi) + " Hz" : (b == n - 1 ? "above " + shortHz (lo) + " Hz"
                                                                                   : shortHz (lo) + " - " + shortHz (hi) + " Hz");
            if (n == 1)
                t = "full range";
            rangeLabels[b]->setText (t);
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
    if (id == kBands || id == kXover1 || id == kXover2 || id == kXover3)
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
            for (int b = 0; b < kMaxBands; ++b)
                for (int f : {kAboveRatio, kBelowRatio})
                    ctl->setPlainFromUI (bandParam (b, f), 1.0);
    });
}

} // namespace multidyn

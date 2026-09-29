#include "Editor.h"

#include "ColorView.h"
#include "Help.h"
#include "ShaperView.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>

namespace smacheratr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::NumberBox;
using pk::Toggle;

namespace {
constexpr double kKnobW = 56, kKnobH = 64;
CRect knobRect (double x, double y, double w = kKnobW, double h = kKnobH) { return CRect (x, y, x + w, y + h); }

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
    shaper = nullptr;
    color = nullptr;
    status = nullptr;
    thresholdView = nullptr;
    colorViews.clear ();
    clarityViews.clear ();
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "smacheratr", 14.0, true));
    status = new Label (CRect (200, 6, 430, 28), "", 10.5);
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    status->setDim (true);
    root->addView (status);
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (CPoint (672, 28)); }));

    // left: the device as Live shows it, with the pre-limiter in front of the curve
    bind (root, new Toggle (CRect (kShaperLeft, 40, kShaperLeft + 76, 62), this, kPreLimit, "Pre-Limit"));
    thresholdView = bind (root, new NumberBox (CRect (kShaperLeft + 80, 42, kShaperLeft + 140, 60), this, kPreLimitThreshold));
    bind (root, new Toggle (CRect (kShaperLeft + 150, 40, kShaperLeft + 210, 62), this, kClarity, "Clarity"));
    clarityViews.push_back (bind (root, new NumberBox (CRect (kShaperLeft + 214, 42, kShaperLeft + 258, 60), this, kClarityFreq)));
    clarityViews.push_back (bind (root, new NumberBox (CRect (kShaperLeft + 262, 42, kShaperLeft + 300, 60), this, kClarityWidth)));
    shaper = new ShaperView (CRect (kShaperLeft, kShaperTop, kShaperLeft + kShaperWidth, kShaperTop + kShaperHeight), this,
                             [c = ctl] () -> const Meters* {
                                 auto* s = c->getShared ();
                                 return s ? &s->meters : nullptr;
                             });
    shaper->setTooltipText (help::kShaperDisplay);
    root->addView (shaper);
    bind (root, new Choice (CRect (8, 266, 112, 288), this, kPostClip));
    bind (root, new Toggle (CRect (120, 266, 176, 288), this, kColorOn, "Color"));
    colorViews.push_back (bind (root, new Knob (knobRect (184, 262), this, kColorLo)));
    bind (root, new Knob (knobRect (24, 346, 68, 78), this, kDrive, nullptr, true));
    bind (root, new Knob (knobRect (128, 346, 68, 78), this, kOutput));
    bind (root, new Knob (knobRect (232, 346, 68, 78), this, kDryWet));

    // right: the colour curve and its controls
    color = new ColorView (CRect (kColorLeft, kColorTop, kColorLeft + kColorViewWidth, kColorTop + kColorViewHeight), this, ctl);
    color->setTooltipText (help::kColorDisplay);
    root->addView (color);
    const uint32_t colorIds[3] = {kColorHi, kColorFreq, kColorWidth};
    for (int i = 0; i < 3; ++i)
        colorViews.push_back (bind (root, new Knob (knobRect (kColorLeft + 60 + i * 130, 346), this, colorIds[i])));

    applyParamTooltips (&help::forParam);
    updateLooks ();
    idle ();
}

void Editor::updateLooks ()
{
    if (thresholdView)
        thresholdView->setEnabledLook (plainValue (kPreLimit) >= 0.5);
    const bool on = plainValue (kColorOn) >= 0.5;
    for (auto* v : colorViews)
        v->setEnabledLook (on);
    for (auto* v : clarityViews)
        v->setEnabledLook (plainValue (kClarity) >= 0.5);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (shaper)
        shaper->invalid ();
    if (color)
        color->invalid ();
    if (id == kPreLimit || id == kColorOn || id == kClarity)
        updateLooks ();
}

void Editor::idle ()
{
    if (shaper)
        shaper->idle ();
    if (status)
        if (auto* s = ctl->getShared ())
        {
            char buf[96];
            std::snprintf (buf, sizeof (buf), "%s%s, latency %d samples",
                           plainValue (kHiQuality) >= 0.5 ? "Hi-Quality: 4x oversampling" : "Hi-Quality off",
                           plainValue (kMidSide) >= 0.5 ? ", Mid/Side" : "", s->latency.load ());
            status->setText (buf);
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
    menu->addEntry ("Hi-Quality (4x oversampling)", -1, plainValue (kHiQuality) >= 0.5 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    menu->addEntry ("Pre-DC Filter", -1, plainValue (kDcFilter) >= 0.5 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    menu->addEntry ("Mid/Side (saturate mid and side apart)", -1,
                    plainValue (kMidSide) >= 0.5 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    menu->popup (frame, where, [this, sizes, menu] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
        else if (r == (int32_t)sizes.size () + 1)
            ctl->setPlainFromUI (kHiQuality, plainValue (kHiQuality) >= 0.5 ? 0.0 : 1.0);
        else if (r == (int32_t)sizes.size () + 2)
            ctl->setPlainFromUI (kDcFilter, plainValue (kDcFilter) >= 0.5 ? 0.0 : 1.0);
        else if (r == (int32_t)sizes.size () + 3)
            ctl->setPlainFromUI (kMidSide, plainValue (kMidSide) >= 0.5 ? 0.0 : 1.0);
    });
}

} // namespace smacheratr

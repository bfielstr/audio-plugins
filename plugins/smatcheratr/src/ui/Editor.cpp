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

namespace smatcheratr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::Panel;
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
    bassView = nullptr;
    wsViews.clear ();
    colorViews.clear ();
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "SMATCHERATR", 14.0, true));
    status = new Label (CRect (200, 6, 430, 28), "", 10.5);
    root->addView (new pk::PresetBar (CRect (440, 6, 636, 28), ctl));
    status->setDim (true);
    root->addView (status);
    auto* helpBtn = new ActionButton (CRect (644, 6, 666, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (672, 6, 752, 28), "Menu", [this] { showMenu (CPoint (672, 28)); }));

    // left: the device as Live shows it
    bind (root, new Choice (CRect (kShaperLeft, 40, kShaperLeft + kShaperWidth, 62), this, kCurve));
    shaper = new ShaperView (CRect (kShaperLeft, kShaperTop, kShaperLeft + kShaperWidth, kShaperTop + kShaperHeight), this, ctl);
    shaper->setTooltipText (help::kShaperDisplay);
    root->addView (shaper);
    bind (root, new Choice (CRect (8, 266, 112, 288), this, kPostClip));
    bind (root, new Toggle (CRect (120, 266, 176, 288), this, kColorOn, "Color"));
    colorViews.push_back (bind (root, new Knob (knobRect (184, 262), this, kColorLo)));
    bassView = bind (root, new Knob (knobRect (252, 262), this, kBassThreshold, "Bass Thr"));
    bind (root, new Knob (knobRect (24, 346, 68, 78), this, kDrive, nullptr, true));
    bind (root, new Knob (knobRect (128, 346, 68, 78), this, kOutput));
    bind (root, new Knob (knobRect (232, 346, 68, 78), this, kDryWet));

    // right: the expanded view
    color = new ColorView (CRect (kColorLeft, kColorTop, kColorLeft + kColorViewWidth, kColorTop + kColorViewHeight), this, ctl);
    color->setTooltipText (help::kColorDisplay);
    root->addView (color);
    const uint32_t colorIds[3] = {kColorHi, kColorFreq, kColorWidth};
    for (int i = 0; i < 3; ++i)
        colorViews.push_back (bind (root, new Knob (knobRect (kColorLeft + 60 + i * 130, 206), this, colorIds[i])));

    auto* wp = new Panel (CRect (kColorLeft, 280, 752, 432), "WAVESHAPER");
    root->addView (wp);
    const uint32_t wsIds[6] = {kWsDrive, kWsCurve, kWsDepth, kWsLinear, kWsDamp, kWsPeriod};
    const char* wsLabels[6] = {"Drive", "Curve", "Depth", "Linear", "Damp", "Period"};
    for (int i = 0; i < 6; ++i)
        wsViews.push_back (bind (wp, new Knob (knobRect (10 + i * 71, 30), this, wsIds[i], wsLabels[i])));
    auto* wsNote = new Label (CRect (8, 112, 428, 126), "Active with the Waveshaper curve", 9.5, false, 1);
    wsNote->setDim (true);
    wp->addView (wsNote);

    applyParamTooltips (&help::forParam);
    updateLooks ();
    idle ();
}

void Editor::updateLooks ()
{
    const int curve = (int)std::lround (plainValue (kCurve));
    for (auto* v : wsViews)
        v->setEnabledLook (curve == kWaveshaper);
    if (bassView)
        bassView->setEnabledLook (curve == kBassShaper);
    const bool on = plainValue (kColorOn) >= 0.5;
    for (auto* v : colorViews)
        v->setEnabledLook (on);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (shaper)
        shaper->invalid ();
    if (color)
        color->invalid ();
    if (id == kCurve || id == kColorOn)
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
            std::snprintf (buf, sizeof (buf), "%s, latency %d samples",
                           plainValue (kHiQuality) >= 0.5 ? "Hi-Quality: 4x oversampling" : "Hi-Quality off",
                           s->latency.load ());
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
    menu->popup (frame, where, [this, sizes, menu] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
        else if (r == (int32_t)sizes.size () + 1)
            ctl->setPlainFromUI (kHiQuality, plainValue (kHiQuality) >= 0.5 ? 0.0 : 1.0);
        else if (r == (int32_t)sizes.size () + 2)
            ctl->setPlainFromUI (kDcFilter, plainValue (kDcFilter) >= 0.5 ? 0.0 : 1.0);
    });
}

} // namespace smatcheratr

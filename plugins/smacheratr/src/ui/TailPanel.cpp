#include "TailPanel.h"

#include "BandPush.h"
#include "Help.h"

#include "pluginkit/ui/InfoBox.h"
#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cctype>

namespace smacheratr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
// A part's On switch in its strip: switching it on opens the part, off folds it.
class StripToggle : public pk::Toggle
{
public:
    StripToggle (const CRect& r, pk::ParamHost* h, uint32_t id, const char* label, std::function<void (bool)> f)
        : pk::Toggle (r, h, id, label), toggled (std::move (f))
    {
    }
    void onMouseDownEvent (MouseDownEvent& e) override
    {
        pk::Toggle::onMouseDownEvent (e);
        if (e.consumed && toggled)
            toggled (host->norm (param) >= 0.5);
    }

private:
    std::function<void (bool)> toggled;
};
} // namespace

// One part of the section: a panel frame whose top kStrip px are its header (a fold mark, its On switch,
// its name); a click on the strip that no control takes folds or opens it.
class TailPanel::Part : public pk::Panel
{
public:
    Part (const CRect& r, std::string name, std::function<bool ()> open, std::function<void ()> toggle)
        : pk::Panel (r), name (std::move (name)), isOpen (std::move (open)), toggleOpen (std::move (toggle))
    {
        for (auto& ch : this->name)
            ch = (char)std::toupper ((unsigned char)ch);
    }
    void drawBackgroundRect (CDrawContext* ctx, const CRect& update) override
    {
        pk::Panel::drawBackgroundRect (ctx, update);
        if (!update.rectOverlap (CRect (0, 0, getViewSize ().getWidth (), kStrip)))
            return;
        // the fold mark: pointing down while open, right while folded
        const bool open = isOpen && isOpen ();
        if (auto path = owned (ctx->createGraphicsPath ()))
        {
            const double cx = 14.0, cy = 11.5;
            if (open)
            {
                path->beginSubpath (CPoint (cx - 4, cy - 2));
                path->addLine (CPoint (cx + 4, cy - 2));
                path->addLine (CPoint (cx, cy + 3));
            }
            else
            {
                path->beginSubpath (CPoint (cx - 2, cy - 4));
                path->addLine (CPoint (cx + 3, cy));
                path->addLine (CPoint (cx - 2, cy + 4));
            }
            path->closeSubpath ();
            ctx->setFillColor (theme::kCopperPale);
            ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
        }
        ctx->setFont (theme::font (9.5, true));
        ctx->setFontColor (theme::kCopperPale);
        ctx->drawString (name.c_str (), CRect (68, 3, getViewSize ().getWidth () - 8, 19), kLeftText, true);
    }
    void onMouseDownEvent (MouseDownEvent& e) override
    {
        pk::Panel::onMouseDownEvent (e);
        if (e.consumed || !e.buttonState.isLeft ())
            return;
        if (e.mousePosition.y - getViewSize ().top < kStrip && toggleOpen)
        {
            toggleOpen ();
            e.consumed = true;
            e.ignoreFollowUpMoveAndUpEvents (true);
        }
    }

private:
    std::string name;
    std::function<bool ()> isOpen;
    std::function<void ()> toggleOpen;
};

TailPanel::TailPanel (pk::EditorBase* ed, const TailBases& b, ColorView::RateSource r, ColorView::MeterSource m, const char* t)
    : editor (ed), bases (b), rate (std::move (r)), meters (std::move (m)), title (t ? t : "")
{
    host = std::make_unique<pk::MappedParamHost> (ed, paramTable (), [b] (uint32_t id) -> int64_t {
        const int f = tailFieldOf (id);
        return f < 0 ? -1 : (int64_t)tailParamOf ((uint32_t)f, b);
    });
}

void TailPanel::add (CViewContainer* parent, const CRect& r)
{
    area = CRect (r.left, r.top, r.right, r.top + kOpenHeight);
    bottomGap = std::max (0.0, editor->contentHeightMade () - area.bottom);
    const double w = area.getWidth ();
    // what this instance had open (a new one: the saturator open while it is on, Gentlr while both are)
    auto* ctl = editor->controllerBase ();
    const int kept = ctl->uiTailOpen;
    if (kept < 0)
    {
        satOpen = editor->plainValue (bases.base + pk::kTailOn) >= 0.5;
        gOpen = satOpen && plainOf (kClarity) >= 0.5;
    }
    else
    {
        satOpen = (kept & pk::ControllerBase::kTailOpenSaturator) != 0;
        gOpen = (kept & pk::ControllerBase::kTailOpenGentlr) != 0;
    }
    front = ctl->uiColorLayer == 1 ? ColorView::Layer::Gentlr : ColorView::Layer::Color;
    satBody.clear ();
    colorRow.clear ();
    gentlrRow.clear ();
    for (auto& v : bandViews)
        v.clear ();
    gentlrBody.clear ();
    advancedViews.clear ();
    paramViews.clear ();

    auto tip = [] (CView* v, const char* t) {
        if (t)
            v->setTooltipText (t);
        return v;
    };
    auto row = [] (double x0, double x1, double top) { return CRect (x0, top, x1, top + 18); };
    // a control on Smacheratr's ID (through the mapping), its help Smacheratr's
    auto addTo = [this] (CViewContainer* p, pk::ParamView* v, std::vector<CView*>& list) {
        p->addView (v);
        paramViews.push_back (v);
        list.push_back (v);
        if (const char* t = help::forParam (v->paramId ()))
            v->setTooltipText (t);
        return v;
    };

    // --- the saturator's part
    satPart = new Part (CRect (area.left, area.top, area.right, area.top + kSaturatorOpen), title, [this] { return satOpen; },
                        [this] { setOpen (!satOpen, gOpen); });
    satPart->setTooltipText ("The saturator at the end of the chain. Click its strip to fold its controls away or open them again.");
    parent->addView (satPart);
    auto* on = new StripToggle (row (24, 60, 2), editor, bases.base + pk::kTailOn, "On", [this] (bool v) { setOpen (v, gOpen); });
    on->setTooltipText ("Smacheratr at the very end of this plug-in: off, the sound passes untouched (switching it on opens its "
                        "controls, off folds them; a click on the strip opens or folds them too).");
    satPart->addView (on);
    paramViews.push_back (on);
    // the layer of the colour display in front, above the display's right end
    layerSwitch = new pk::ViewSwitch (row (w - 140, w - 10, 2), {"Color", "Gentlr"}, [this] { return front == ColorView::Layer::Gentlr ? 1 : 0; },
                                      [this] (int i) { setLayer (i == 1 ? ColorView::Layer::Gentlr : ColorView::Layer::Color); });
    layerSwitch->setTooltipText ("Which layer of the colour display is in front: the colour filters (their points, and their switch "
                                 "and amounts in the row under the display) or Gentlr (its band handles, and the selected band's "
                                 "controls). The other layer is drawn faint behind.");
    satPart->addView (layerSwitch);
    satBody.push_back (layerSwitch);

    // the displays: the Analog curve, the colour display (with Advanced, the Threshold sliders at its right)
    const CRect displays (10, kDisplayTop, w - 10, kDisplayTop + kDisplayHeight);
    const double sw = std::min (230.0, displays.getWidth () * 0.34);
    shaper = new ShaperView (CRect (displays.left, displays.top, displays.left + sw, displays.bottom), host.get (), meters);
    pk::setHelp (shaper, "Analog Curve", help::kShaperDisplay);
    satPart->addView (shaper);
    satBody.push_back (shaper);
    colorArea = CRect (displays.left + sw + 8, displays.top, displays.right, displays.bottom);
    color = new ColorView (colorArea, host.get (), rate, meters);
    pk::setHelp (color, "Colour EQ", help::kColorDisplay);
    color->onBandPicked = [this] (int k) { selectBand (k); };
    color->setLayer (front);
    satPart->addView (color);
    satBody.push_back (color);
    for (int k = 0; k < kGentlrBands; ++k)
    {
        sliders[k] = new ThresholdSlider (CRect (0, 0, 1, 1), host.get (), k, meters);
        sliders[k]->setTooltipText (help::forParam (kGentlrThresholdIds[k]));
        // grabbing a band's Threshold selects the band (and brings Gentlr's layer to the front)
        sliders[k]->onPicked = [this] (int b) {
            setLayer (ColorView::Layer::Gentlr);
            selectBand (b);
        };
        satPart->addView (sliders[k]);
        paramViews.push_back (sliders[k]);
    }

    // the saturator's switches
    addTo (satPart, new pk::Toggle (row (10, 80, kRowA), host.get (), kPreLimit, "Pre-Limit"), satBody);
    addTo (satPart, new pk::NumberBox (row (84, 140, kRowA), host.get (), kPreLimitThreshold), satBody);
    addTo (satPart, new pk::Choice (row (146, 246, kRowA), host.get (), kPostClip), satBody);
    addTo (satPart, new pk::Toggle (row (252, 294, kRowA), host.get (), kMidSide, "M/S"), satBody);
    addTo (satPart, new pk::Choice (row (298, 344, kRowA), host.get (), kOversampling), satBody);
    addTo (satPart, new pk::Toggle (row (348, 380, kRowA), host.get (), kDcFilter, "DC"), satBody);
    // the layer row: the colour filters' switch and amounts ...
    addTo (satPart, new pk::Toggle (row (10, 60, kRowB), host.get (), kColorOn, "Color"), colorRow);
    addTo (satPart, new pk::NumberBox (row (64, 105, kRowB), host.get (), kColorLo), colorRow);
    addTo (satPart, new pk::NumberBox (row (108, 149, kRowB), host.get (), kColorHi), colorRow);
    addTo (satPart, new pk::NumberBox (row (152, 208, kRowB), host.get (), kColorFreq), colorRow);
    addTo (satPart, new pk::NumberBox (row (211, 245, kRowB), host.get (), kColorWidth), colorRow);
    // ... or Gentlr's band selector and the selected band's values (every band's made, one shown)
    static const char* const bandNames[kGentlrBands] = {"1", "2", "S", "H"};
    static const char* const bandTips[kGentlrBands] = {
        "Gentlr's first band: its controls here, its handle lit in the display, its Threshold slider lit (Advanced).",
        "Gentlr's second band (Gentlr 2 in the display): its controls here, its handle and Threshold slider lit.",
        "Gentlr's Sub band: from the bottom of the spectrum, it starts to taper at its Freq.",
        "Gentlr's High band: from its Freq, where it starts to taper, to the top of the spectrum."};
    for (int k = 0; k < kGentlrBands; ++k)
    {
        auto* bt = new pk::ActionButton (row (10 + k * 21, 28 + k * 21, kRowB), bandNames[k], [this, k] { selectBand (k); },
                                         [this, k] { return band == k; });
        tip (bt, bandTips[k]);
        satPart->addView (bt);
        gentlrRow.push_back (bt);
        if (hasWidth (k))
        {
            addTo (satPart, new pk::NumberBox (row (97, 153, kRowB), host.get (), kClarityFreqIds[k]), bandViews[k]);
            addTo (satPart, new pk::NumberBox (row (157, 189, kRowB), host.get (), kClarityWidthIds[k]), bandViews[k]);
            addTo (satPart, new pk::NumberBox (row (193, 237, kRowB), host.get (), kClarityRangeIds[k]), bandViews[k]);
        }
        else
        {
            // the Sub and High bands: Frequency and Range where the others have theirs (no width)
            addTo (satPart, new pk::NumberBox (row (97, 153, kRowB), host.get (), kGentlrFreqIds[k]), bandViews[k]);
            addTo (satPart, new pk::NumberBox (row (193, 237, kRowB), host.get (), kGentlrRangeIds[k]), bandViews[k]);
        }
    }
    // the drive, the mix and the output, at the right of the rows
    const double ky = kRowA - 4;
    addTo (satPart, new pk::Knob (CRect (w - 188, ky, w - 132, ky + 64), host.get (), kDrive, nullptr, true), satBody);
    addTo (satPart, new pk::Knob (CRect (w - 128, ky, w - 72, ky + 64), host.get (), kDryWet), satBody);
    addTo (satPart, new pk::Knob (CRect (w - 68, ky, w - 12, ky + 64), host.get (), kOutput), satBody);

    // --- Gentlr's part
    gentlrPart = new Part (CRect (area.left, area.top + kSaturatorOpen + kGap, area.right, area.top + kSaturatorOpen + kGap + kGentlrOpen),
                           "gentlr", [this] { return gOpen; }, [this] { setOpen (satOpen, !gOpen); });
    gentlrPart->setTooltipText ("Gentlr in the saturator: its mode, Slope and No Overlap (its bands are in the colour display, with Gentlr "
                                "in front). Click its strip to fold or open it.");
    parent->addView (gentlrPart);
    auto* gOn = new StripToggle (row (24, 60, 2), host.get (), kClarity, "On", [this] (bool v) { setOpen (satOpen, v); });
    gOn->setTooltipText ("Gentlr: a compressor on up to four bands (the Sub band from the bottom, the High band to the top) before "
                         "the curve, so a hard-pushed drive does not go muddy or harsh. A band works while its Range is above 0 dB "
                         "(its handle in the display with Gentlr in front). Switching it on opens these controls, off folds them.");
    gentlrPart->addView (gOn);
    paramViews.push_back (gOn);
    addTo (gentlrPart, new pk::Toggle (row (10, 80, kGentlrRow), host.get (), kClarityAdvanced, "Advanced"), gentlrBody);
    addTo (gentlrPart, new pk::Toggle (row (86, 130, kGentlrRow), host.get (), kClarityDrive, "Drive"), advancedViews);
    addTo (gentlrPart, new pk::NumberBox (row (134, 186, kGentlrRow), host.get (), kClarityDriveAmount), advancedViews);
    auto* slopeLabel = new pk::Label (row (200, 236, kGentlrRow), "Slope", 10.5);
    gentlrPart->addView (slopeLabel);
    gentlrBody.push_back (slopeLabel);
    tip (addTo (gentlrPart, new pk::Choice (row (240, 344, kGentlrRow), host.get (), kClaritySlope), gentlrBody), help::kSlope);
    addTo (gentlrPart, new NoOverlapToggle (row (352, 442, kGentlrRow), host.get (), smacheratrBandParams ()), gentlrBody);

    selectBand (band);
    layout ();
    updateLooks ();
}

double TailPanel::height () const { return (satOpen ? kSaturatorOpen : kStrip) + kGap + (gOpen ? kGentlrOpen : kStrip); }

void TailPanel::keepOpenState ()
{
    editor->controllerBase ()->uiTailOpen =
        (satOpen ? pk::ControllerBase::kTailOpenSaturator : 0) | (gOpen ? pk::ControllerBase::kTailOpenGentlr : 0);
}

void TailPanel::setOpen (bool s, bool g)
{
    const bool changed = s != satOpen || g != gOpen;
    satOpen = s;
    gOpen = g;
    keepOpenState ();
    if (changed && satPart)
        layout ();
}

void TailPanel::layout ()
{
    if (!satPart || !gentlrPart)
        return;
    // the parts: folded to their strips, Gentlr's under the saturator's
    const double satH = satOpen ? kSaturatorOpen : kStrip, gH = gOpen ? kGentlrOpen : kStrip;
    const CRect sr (area.left, area.top, area.right, area.top + satH);
    satPart->setViewSize (sr);
    satPart->setMouseableArea (sr);
    const CRect gr (area.left, sr.bottom + kGap, area.right, sr.bottom + kGap + gH);
    gentlrPart->setViewSize (gr);
    gentlrPart->setMouseableArea (gr);
    // what shows in them
    const bool gentlrFront = front == ColorView::Layer::Gentlr;
    for (auto* v : satBody)
        v->setVisible (satOpen);
    for (auto* v : colorRow)
        v->setVisible (satOpen && !gentlrFront);
    for (auto* v : gentlrRow)
        v->setVisible (satOpen && gentlrFront);
    for (int k = 0; k < kGentlrBands; ++k)
        for (auto* v : bandViews[k])
            v->setVisible (satOpen && gentlrFront && k == band);
    const bool advanced = plainOf (kClarityAdvanced) >= 0.5;
    ThresholdSlider::layout (color, sliders, colorArea, advanced);
    if (!satOpen)
        for (auto* s : sliders)
            s->setVisible (false);
    for (auto* v : gentlrBody)
        v->setVisible (gOpen);
    for (auto* v : advancedViews)
        v->setVisible (gOpen && advanced);
    satPart->invalid ();
    gentlrPart->invalid ();
    // the editor follows: the content ends under the section (a host that keeps the window leaves the
    // space empty)
    editor->setContentHeight (area.top + height () + bottomGap);
    if (auto* p = satPart->getParentView ())
        p->invalidRect (CRect (area.left, area.top, area.right, area.top + kOpenHeight));
}

void TailPanel::setLayer (ColorView::Layer l)
{
    front = l;
    editor->controllerBase ()->uiColorLayer = l == ColorView::Layer::Gentlr ? 1 : 0;
    if (color)
        color->setLayer (l);
    if (layerSwitch)
        layerSwitch->invalid ();
    layout ();
}

void TailPanel::selectBand (int k)
{
    band = std::clamp (k, 0, kGentlrBands - 1);
    if (color)
        color->setSelectedBand (band);
    for (auto* s : sliders)
        if (s)
            s->setSelected (s->bandIndex () == band);
    for (auto* b : gentlrRow)
        b->invalid ();
    const bool show = satOpen && front == ColorView::Layer::Gentlr;
    for (int i = 0; i < kGentlrBands; ++i)
        for (auto* v : bandViews[i])
            v->setVisible (show && i == band);
}

void TailPanel::updateLooks ()
{
    const bool gentlr = plainOf (kClarity) >= 0.5, colorOn = plainOf (kColorOn) >= 0.5;
    for (int k = 0; k < kGentlrBands; ++k)
        if (sliders[k])
            sliders[k]->setEnabledLook (smacheratrBandParams ().works (host.get (), k));
    for (auto* v : paramViews)
    {
        const uint32_t id = v->paramId ();
        if (v->paramHost () != host.get ())
            continue; // (the plug-in's own On)
        if (id == kColorLo || id == kColorHi || id == kColorFreq || id == kColorWidth)
            v->setEnabledLook (colorOn);
        else if (id == kPreLimitThreshold)
            v->setEnabledLook (plainOf (kPreLimit) >= 0.5);
        else if (id == kClarityDriveAmount)
            v->setEnabledLook (gentlr && plainOf (kClarityDrive) >= 0.5);
        else if (id == kClarityAdvanced || id == kClarityDrive || id == kClaritySlope || id == kClarityNoOverlap)
            v->setEnabledLook (gentlr);
        else
            for (int k = 0; k < kGentlrBands; ++k)
                if (id == kGentlrFreqIds[k] || id == kGentlrRangeIds[k] || (hasWidth (k) && id == kClarityWidthIds[k]))
                    v->setEnabledLook (gentlr);
    }
}

void TailPanel::idle ()
{
    if (!satOpen)
        return;
    if (shaper)
        shaper->idle ();
    if (color)
        color->idle ();
    for (auto* s : sliders)
        if (s && s->isVisible ())
            s->idle ();
}

void TailPanel::paramChanged (uint32_t id)
{
    if (!isTailParam (id) || !satPart)
        return;
    if (id == bases.ext2Base + pk::kTailExt2Advanced)
        layout ();
    updateLooks ();
    for (auto* v : paramViews)
        v->invalid ();
    for (CView* v : {(CView*)shaper, (CView*)color})
        if (v)
            v->invalid ();
}

void TailPanel::closed ()
{
    satPart = gentlrPart = nullptr;
    shaper = nullptr;
    color = nullptr;
    layerSwitch = nullptr;
    for (auto& s : sliders)
        s = nullptr;
    satBody.clear ();
    colorRow.clear ();
    gentlrRow.clear ();
    for (auto& v : bandViews)
        v.clear ();
    gentlrBody.clear ();
    advancedViews.clear ();
    paramViews.clear ();
}

} // namespace smacheratr

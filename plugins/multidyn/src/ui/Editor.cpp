#include "Editor.h"

#include "Thresholds.h"

#include "DynDisplay.h"
#include "Help.h"
#include "plugin/Controller.h"

#include "pluginkit/vst/Clipboard.h"
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
// the Below and Above value fields: told apart by their columns (one text colour, docs/THEME.md)
const CColor kBelowColor = pk::theme::kText, kAboveColor = pk::theme::kText;

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
        // the ground, the header band and the copper window frame (docs/THEME.md, "Window")
        pk::draw::window (ctx, CRect (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ()), 34);
    }
};
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    tail.reset ();
    display = nullptr;
    scStatus = nullptr;
    rmsWindowBox = nullptr;
    softKneeToggle = detectorSeg = softenKnob = nullptr;
    for (int b = 0; b < kMaxBands; ++b)
    {
        nameLabels[b] = nullptr;
        onToggles[b] = soloToggles[b] = inputKnobs[b] = outputKnobs[b] = nullptr;
        for (auto& v : valueBoxes[b])
            v = nullptr;
    }
    for (auto& v : xoverBoxes)
        v = nullptr;
    subName = nullptr;
    for (auto& v : subBoxes)
        v = nullptr;
    subInBox = subOutBox = nullptr;
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
    root->addView (new Label (CRect (12, 6, 150, 28), "multidyn", 14.0, true));
    root->addView (new Label (CRect (160, 6, 205, 28), "Bands", 10.5, false, 2));
    bind (root, new Segmented (CRect (210, 7, 330, 27), this, kBands, {"1", "2", "3", "4"}));
    root->addView (new Label (CRect (kStyleLeft - 42, 6, kStyleLeft - 4, 28), "Style", 10.5, false, 2));
    bind (root, new Segmented (CRect (kStyleLeft, kStyleTop, kStyleRight, kStyleTop + 20), this, kStyle, {"OTT", "Character"}));
    scStatus = new Label (CRect (8, kScStatusTop, 342, kScStatusTop + 16), "", 9.5);
    root->addView (new pk::PresetBar (CRect (578, 6, 778, 28), ctl));
    scStatus->setDim (true);
    root->addView (scStatus);
    root->addView (tip (new ActionButton (CRect (786, 6, 808, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                          [this] { return tooltipsEnabled (); }),
                        "Show or hide the floating help tooltips (the info box at the bottom shows the same help either way)."));
    root->addView (new ActionButton (CRect (814, 6, 912, 28), "Menu", [this] { showMenu (layoutPoint (CPoint (814, 28))); }));

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
    pk::setHelp (display, "Bands", help::kDisplay);
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
                                                          i < 2 ? kBelowColor : (i < 4 ? kAboveColor : pk::theme::kText)));
    }
    for (int x = 0; x < kMaxBands - 1; ++x)
        xoverBoxes[x] = bind (root, new NumberBox (none, this, (uint32_t)(kXover1 + x)));
    // the Sub band's lane (shown when it is on)
    subName = new Label (none, "Sub", 10.5, true, 0);
    root->addView (subName);
    const uint32_t subFields[4] = {kSubThresh, kSubRatio, kSubAttack, kSubRelease};
    for (int i = 0; i < 4; ++i)
        subBoxes[i] = bind (root, new NumberBox (none, this, subFields[i], i < 2 ? kAboveColor : pk::theme::kText));
    subInBox = bind (root, new NumberBox (none, this, kSubInput));
    subOutBox = bind (root, new NumberBox (none, this, kSubOutput));

    // global column
    bind (root, new Knob (knobRect (kGlobalColLeft, 40), this, kOutput, nullptr, true));
    bind (root, new Knob (knobRect (kGlobalColLeft, 114), this, kTime));
    bind (root, new Knob (knobRect (kGlobalColLeft, 188), this, kAmount));
    softenKnob = bind (root, new Knob (knobRect (kGlobalColLeft, 262), this, kSoften));
    bind (root, new Toggle (CRect (kGlobalColLeft, kColorTop, kGlobalColLeft + 60, kColorTop + 18), this, kSoftenColor, "Color"));

    // bottom row
    softKneeToggle = bind (root, new Toggle (CRect (8, 352, 96, 372), this, kSoftKnee, "Soft Knee"));
    detectorSeg = bind (root, new Segmented (CRect (104, 352, 184, 372), this, kDetector, {"Peak", "RMS"}));
    auto* rl = new Label (CRect (190, 354, 250, 370), "Window", 9.5, true, 2);
    rl->setDim (true);
    root->addView (rl);
    rmsWindowBox = bind (root, new NumberBox (CRect (254, 353, 314, 371), this, kRmsWindow));
    // second row: the crossovers' Slope, the Sub band
    auto* sl = new Label (CRect (8, kRow2Top + 2, kSlopeLeft - 4, kRow2Top + 18), "Slope", 9.5, true, 2);
    sl->setDim (true);
    root->addView (sl);
    bind (root, new Choice (CRect (kSlopeLeft, kRow2Top, kSlopeLeft + 90, kRow2Top + 20), this, kXoverSlope));
    bind (root, new Toggle (CRect (kSubOnLeft, kRow2Top, kSubOnLeft + 48, kRow2Top + 20), this, kSubOn, "Sub"));
    bind (root, new NumberBox (CRect (kSubFreqLeft, kRow2Top + 1, kSubFreqLeft + 64, kRow2Top + 19), this, kSubFreq));
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
    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    tail = makeTail ();
    tail->add (root, layoutRegion ("tail", CRect (8, 424, 912, 424 + smacheratr::TailPanel::kOpenHeight)));

    applyParamTooltips (&help::forParam);
    updateLayout ();
    updateLooks ();
    idle ();
}

smacheratr::TailBases Editor::tailBases () { return {kSatOn, kSatExtBase, kSatExt2Base, kSatExt3Base, kSatExt4Base}; }

std::unique_ptr<smacheratr::TailPanel> Editor::makeTail ()
{
    return std::make_unique<smacheratr::TailPanel> (this, tailBases (),
                                                    [c = ctl] { auto* m = c->getMeters (); return m ? m->sampleRate.load () : 48000.0; },
                                                    [c = ctl] () -> const smacheratr::Meters* { auto* m = c->getMeters (); return m ? &m->satMeters : nullptr; });
}

pk::basic::Spec Editor::basicSpec ()
{
    // how many bands and which compressor (Bands, Style), how hard (Amount) and how fast (Time); Output
    using namespace pk::basic;
    Spec s;
    s.title = "multidyn";
    s.capture = [c = ctl] () -> const pk::CaptureBuffer* { auto* m = c->getMeters (); return m ? &m->capture : nullptr; };
    s.displayHeight = 240;
    s.display = [this] (const CRect& r) -> CView* {
        display = new DynDisplay (r, this, [c = ctl] { return c->getMeters (); });
        pk::setHelp (display, "Bands", help::kDisplay);
        return display;
    };
    s.rows = {{segmented (kBands, "Bands", {"1", "2", "3", "4"}, 200), segmented (kStyle, "Style", {"OTT", "Character"})},
              {knob (kAmount), knob (kTime)}};
    s.output = {knob (kOutput, {}, true)};
    smacheratr::TailPanel::addToBasic (s, this, tailBases (), tail, [this] { return makeTail (); });
    s.menu = [this] (CPoint p) { showMenu (p); };
    s.help = &help::forParam;
    s.advancedSwitch = CRect (504, 6, 570, 28);
    return s;
}

void Editor::updateLayout ()
{
    if (!display || !nameLabels[0]) // (the Basic page has the display alone)
        return;
    const int n = std::clamp ((int)std::lround (plainValue (kBands)) + 1, 1, kMaxBands);
    const bool sub = display->subShown ();
    const double top = kDisplayTop + DynDisplay::kHeader;
    const double laneH = (kDisplayBottom - kDisplayTop - DynDisplay::kHeader - DynDisplay::kScaleHeight) / display->lanes ();
    const double knobH = std::min (kKnobH, laneH - 2);
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
        const double ky = std::max (laneTop + 1, cy - knobH / 2);
        place (inputKnobs[b], CRect (kInputColLeft, ky, kInputColLeft + kKnobW, ky + knobH));
        place (outputKnobs[b], CRect (kOutputColLeft, ky, kOutputColLeft + kKnobW, ky + knobH));
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
    // the Sub band's lane, at the bottom
    for (CView* v : {static_cast<CView*> (subName), subBoxes[0], subBoxes[1], subBoxes[2], subBoxes[3], subInBox, subOutBox})
        if (v)
            v->setVisible (sub);
    if (sub)
    {
        const double laneTop = top + n * laneH, cy = laneTop + laneH / 2;
        place (subName, CRect (kBandColLeft, laneTop + 12, kBandColLeft + 48, laneTop + 26));
        const double xs[4] = {aboveX, aboveX, timeX, timeX};
        for (int i = 0; i < 4; ++i)
        {
            const double y = i % 2 == 0 ? cy - 20 : cy + 2;
            place (subBoxes[i], CRect (xs[i], y, xs[i] + 68, y + 18));
        }
        place (subInBox, CRect (kInputColLeft, cy - 9, kInputColLeft + kKnobW + 10, cy + 9));
        place (subOutBox, CRect (kOutputColLeft, cy - 9, kOutputColLeft + kKnobW + 10, cy + 9));
    }
    if (frame)
        frame->invalid ();
}

void Editor::setNorm (uint32_t id, double v)
{
    pk::EditorBase::setNorm (id, v);
    pushThresholds (*this, id, v);
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tail)
        tail->paramChanged (id);
    if (display)
        display->invalid ();
    if (id == kBands || id == kSubOn)
        updateLayout ();
    if (id == kDetector || id == kStyle)
        updateLooks ();
}

void Editor::updateLooks ()
{
    // OTT style (Engine.h): Peak/RMS, the RMS Window, Soft Knee and Soften do nothing there
    const bool character = std::lround (plainValue (kStyle)) == kStyleCharacter;
    for (pk::ParamView* v : {softKneeToggle, detectorSeg, softenKnob})
        if (v)
            v->setEnabledLook (character);
    if (rmsWindowBox)
        rmsWindowBox->setEnabledLook (character && std::lround (plainValue (kDetector)) == kRms);
}

void Editor::idle ()
{
    if (tail)
        tail->idle ();
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
    const int settingsAt = pk::addSettingsMenuEntries (menu);
    addLayoutMenu (menu);
    menu->popup (frame, where, [this, sizes, menu, settingsAt] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (pickedInSubMenu (m)) // (Layout: its entries act by themselves)
            return;
        if (settingsMenuPicked (r, settingsAt))
            return;
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
        else if (r == (int32_t)sizes.size () + 1)
            for (int b = 0; b < kMaxBands; ++b)
                for (int f : {kAboveRatio, kBelowRatio})
                    ctl->setPlainFromUI (bandParam (b, f), 1.0);
            // and the Sub band's
            if (r == (int32_t)sizes.size () + 1)
                ctl->setPlainFromUI (kSubRatio, 1.0);
    });
}

pk::layout::Spec Editor::layoutSpec (bool) const
{
    pk::layout::Spec s;
    // the bands (the display with each band's controls around it) beside the crossover row, the side-chain
    // and the pre-limiter stacked; the end saturator under them. The band controls the display places as
    // the band count changes (those not shown yet have no place) go with the bands.
    s.panels = {
        {"bands", "bands", {kBandColLeft, kDisplayTop, kWidth - 8, 348}, 0},
        {"crossover", "crossover", {kBandColLeft, 350, 342, 420}, 0, 0},
        {"sidechain", "", {350, 344, 640, 416}, 0, 0},
        {"prelimit", "", {648, 344, 822, 416}, 0, 0},
        {"tail", "end of the chain", {8, 424, kWidth - 8, 424 + smacheratr::TailPanel::kOpenHeight}, 1, -1, true},
    };
    s.fallback = "bands";
    return s;
}

} // namespace multidyn

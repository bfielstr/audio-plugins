// The optional Smacheratr at the end of another plug-in's chain (its saturator section, at the bottom of
// every editor but Smacheratr's own): two parts that fold away and open on their own, each a header strip
// with a fold mark, its On switch and its name, and its controls under it.
//   SMACHERATR: the Analog curve and the colour display, the saturator's switches (Pre-Limit and its
//     threshold, Post Clip, M/S, Oversampling, DC), a row for the layer of the colour display in front
//     (the Color | Gentlr switch in the strip, above the display: Color shows the colour filters' switch and
//     amounts, Gentlr its band selector and the selected band's Frequency, Width and Range), and Drive,
//     Dry/Wet and Output. With Gentlr's Advanced on, its bands' Threshold sliders sit at the right of the
//     colour display.
//   GENTLR: Gentlr's On in the strip; Advanced (and with it the region Drive), the bands' Slope and No
//     Overlap.
// Clicking a strip (anywhere but its controls) folds or opens its part; switching a part on opens it and
// switching it off folds it. A new instance opens the saturator only while it is on (and Gentlr only
// while both are on); what the user opens and folds is kept with the controller's state
// (pk::ControllerBase::uiTailOpen), as is the layer in front (uiColorLayer: Gentlr in a new instance).
// Folding shrinks the editor (pk::EditorBase::setContentHeight); a host that keeps the window as it is
// leaves the space empty.
//   A Gentlr band is selected in one place for all: its handle in the display, its button (1, 2, S, H)
// or its Threshold slider; the band's controls show in the layer row, its handle is lit and its
// Threshold slider lit too (grabbing a slider brings Gentlr's layer to the front).
//   The controls work on the plug-in's tail parameters (its five blocks, TailBases) through a mapping
// from Smacheratr's IDs, so they read Smacheratr's names, ranges and help (the On switch is the
// plug-in's own); the levels and the sample rate come through functions.
#pragma once

#include "ColorView.h"
#include "ShaperView.h"
#include "ThresholdSlider.h"

#include "../core/TailExt.h"

#include "pluginkit/ui/Widgets.h"
#include "pluginkit/vst/EditorBase.h"

#include <functional>
#include <memory>
#include <vector>

namespace smacheratr {

class TailPanel
{
public:
    static constexpr double kStrip = 22.0;          // a part's header strip
    static constexpr double kGap = 6.0;             // between the two parts
    static constexpr double kDisplayHeight = 148.0; // the Analog curve and the colour display
    static constexpr double kSaturatorOpen = 244.0; // the saturator's part open (its strip included)
    static constexpr double kGentlrOpen = 50.0;     // Gentlr's
    static constexpr double kOpenHeight = kSaturatorOpen + kGap + kGentlrOpen; // the whole section, both open
    static constexpr double kFoldedHeight = kStrip + kGap + kStrip;            // both folded
    // inside the saturator's part (its top-left at 0, 0): the displays, the two rows under them
    static constexpr double kDisplayTop = 24.0, kRowA = 178.0, kRowB = 204.0;
    static constexpr double kGentlrRow = 26.0; // inside Gentlr's part

    // `editor`: the plug-in's editor (the host of its own parameters, and the window that folds)
    TailPanel (pk::EditorBase* editor, const TailBases& bases, ColorView::RateSource rate, ColorView::MeterSource meters,
               const char* title = "smacheratr  (end of the chain)");

    // Builds the section into `parent` at r's top-left, r's width wide (kOpenHeight tall with both parts
    // open). It must be the last thing in the editor's content: the content ends where it ends, plus the
    // gap under it as built.
    void add (VSTGUI::CViewContainer* parent, const VSTGUI::CRect& r);
    // A Basic page's extras (pk::basic::Spec::extras, untitled: extrasHeight kBasicHeight): the section in a
    // group of r's size, which the page adds. The page keeps its height when a part folds.
    static constexpr double kBasicHeight = kOpenHeight + 16.0;
    VSTGUI::CView* basicExtras (const VSTGUI::CRect& r);
    void idle ();
    void paramChanged (uint32_t id); // redraws when a tail parameter changes
    void closed ();                  // the editor closed: its views are gone

    bool saturatorOpen () const { return satOpen; }
    bool gentlrOpen () const { return gOpen; }
    void setOpen (bool saturator, bool gentlr); // (kept for the instance)
    double height () const;                     // the section now
    ColorView::Layer layer () const { return front; }
    void setLayer (ColorView::Layer l);
    int selectedBand () const { return band; }
    void selectBand (int k);

private:
    class Part;
    bool isTailParam (uint32_t id) const { return tailFieldIn (id, bases) >= 0; }
    double plainOf (uint32_t smacheratrId) { return host->plainValue (smacheratrId); }
    void layout ();       // the parts' sizes and what shows in them
    void updateLooks ();  // dim what is off
    void keepOpenState (); // the open parts, into the controller's state

    pk::EditorBase* editor;
    std::unique_ptr<pk::MappedParamHost> host; // Smacheratr's IDs on the plug-in's tail parameters
    TailBases bases;
    ColorView::RateSource rate;
    ColorView::MeterSource meters;
    std::string title;
    VSTGUI::CRect area;     // the section open, in the parent
    double bottomGap = 8.0; // the content under the section
    bool satOpen = true, gOpen = true;
    ColorView::Layer front = ColorView::Layer::Color;
    int band = 0;

    Part* satPart = nullptr;
    Part* gentlrPart = nullptr;
    ShaperView* shaper = nullptr;
    ColorView* color = nullptr;
    VSTGUI::CRect colorArea; // the colour display without Advanced's sliders (in the saturator's part)
    ThresholdSlider* sliders[kGentlrBands] = {nullptr, nullptr, nullptr, nullptr};
    std::vector<VSTGUI::CView*> satBody;      // the saturator's controls (shown while open)
    std::vector<VSTGUI::CView*> colorRow;     // the layer row with Color in front
    std::vector<VSTGUI::CView*> gentlrRow;    // with Gentlr in front: the band buttons
    std::vector<VSTGUI::CView*> bandViews[kGentlrBands]; // and each band's values (the selected band's shown)
    std::vector<VSTGUI::CView*> gentlrBody;   // Gentlr's part's controls
    std::vector<VSTGUI::CView*> advancedViews; // the region Drive (with Advanced)
    std::vector<pk::ParamView*> paramViews;   // every control, for repainting
    VSTGUI::CView* layerSwitch = nullptr;
};

} // namespace smacheratr

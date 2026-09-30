#include "EditorBase.h"

#include "pluginkit/TailParams.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"

#include <algorithm>
#include <cmath>

namespace pk {

using namespace VSTGUI;
using namespace Steinberg;

EditorBase::EditorBase (ControllerBase* c, double w, double h)
: VSTGUIEditor (c), controller (c), baseWidth (w), baseHeight (h)
{
    scale = std::clamp (c->uiScale, 0.5, 2.0);
    ViewRect vr (0, 0, (int32)std::lround (baseWidth * scale), (int32)std::lround (baseHeight * scale));
    setRect (vr);
    setIdleRate (33);
}

bool PLUGIN_API EditorBase::open (void* parent, const PlatformType& platformType)
{
    if (frame)
        return false;
    frame = new CFrame (CRect (0, 0, baseWidth, baseHeight), this);
    byParam.clear ();
    buildUI (frame);
    frame->enableTooltips (controller->uiShowTips, 600);
    frame->open (parent, platformType);
    frame->setZoom (scale);
    return true;
}

void PLUGIN_API EditorBase::close ()
{
    onClose ();
    byParam.clear ();
    if (frame)
    {
        frame->forget ();
        frame = nullptr;
    }
}

tresult PLUGIN_API EditorBase::checkSizeConstraint (ViewRect* rect)
{
    if (!rect)
        return kInvalidArgument;
    const double sx = rect->getWidth () / baseWidth, sy = rect->getHeight () / baseHeight;
    const double s = std::clamp (std::min (sx, sy), 0.5, 2.0);
    rect->right = rect->left + (int32)std::lround (baseWidth * s);
    rect->bottom = rect->top + (int32)std::lround (baseHeight * s);
    return kResultTrue;
}

tresult PLUGIN_API EditorBase::onSize (ViewRect* newSize)
{
    if (!newSize)
        return kInvalidArgument;
    scale = std::clamp (newSize->getWidth () / baseWidth, 0.5, 2.0);
    controller->uiScale = scale;
    if (frame)
        frame->setZoom (scale);
    return VSTGUIEditor::onSize (newSize);
}

void EditorBase::resizeTo (double s)
{
    s = std::clamp (s, 0.5, 2.0);
    ViewRect vr (0, 0, (int32)std::lround (baseWidth * s), (int32)std::lround (baseHeight * s));
    if (plugFrame)
        plugFrame->resizeView (this, &vr);
}

CMessageResult EditorBase::notify (CBaseObject* sender, const char* message)
{
    if (message == CVSTGUITimer::kMsgTimer && frame)
        idle ();
    return VSTGUIEditor::notify (sender, message);
}

void EditorBase::paramChanged (uint32_t id)
{
    auto it = byParam.find (id);
    if (it != byParam.end ())
        for (auto* v : it->second)
            v->invalid ();
}

void EditorBase::setTooltipsEnabled (bool on)
{
    controller->uiShowTips = on;
    if (frame)
    {
        frame->enableTooltips (on, 600);
        frame->invalid ();
    }
}

Panel* EditorBase::addTailPanel (CViewContainer* parent, const CRect& r, uint32_t base, uint32_t extBase, uint32_t ext2Base,
                                 const char* title)
{
    auto* p = new Panel (r, title);
    parent->addView (p);
    // two rows of switches and values under the title, the knobs to the right of them
    const double y = r.getHeight () - 72.0, rowA = y + 22, rowB = y + 48;
    auto tip = [] (CView* v, const char* t) { v->setTooltipText (t); };
    auto row = [] (double x0, double x1, double top) { return CRect (x0, top, x1, top + 18); };
    tip (bind (p, new Toggle (row (10, 50, rowA), this, base + kTailOn, "On")),
         "Smacheratr (the Analog curve) at the very end of this plug-in: off, the sound passes untouched.");
    tip (bind (p, new Toggle (row (54, 124, rowA), this, base + kTailPreLimit, "Pre-Limit")),
         "A look-ahead limiter before the drive, so transients do not push further into the curve than the rest.");
    tip (bind (p, new NumberBox (row (128, 184, rowA), this, base + kTailThreshold)),
         "Level the pre-limiter holds the signal to, before the drive.");
    tip (bind (p, new Choice (row (190, 290, rowA), this, base + kTailPostClip)),
         "Clip the output at 0 dB after the curve (Soft: the Analog curve again, Hard: a digital clip).");
    tip (bind (p, new Toggle (row (296, 338, rowA), this, extBase + kTailExtMidSide, "M/S")),
         "Saturate the mid and the side apart: the side is driven by its own, lower level, so a wide sound stays wide.");
    tip (bind (p, new Toggle (row (342, 388, rowA), this, extBase + kTailExtHiQuality, "Hi-Q")),
         "Run the curve 4x oversampled to reduce aliasing (a little more CPU).");
    tip (bind (p, new Toggle (row (392, 424, rowA), this, extBase + kTailExtDcFilter, "DC")), "Remove DC offset before the curve.");
    tip (bind (p, new Toggle (row (430, 480, rowA), this, extBase + kTailExtColorOn, "Color")),
         "Colour filters: an EQ before the curve, undone after it, so the curve bites harder or softer on some frequencies.");
    // Gently: one button, Advanced, a band selector and the selected band's controls (all bands' are
    // made; the other band's are hidden)
    tip (bind (p, new Toggle (row (10, 60, rowB), this, extBase + kTailExtClarity, "Gently")),
         "Gently: a compressor on up to three bands (the third is the Sub band, from the bottom), so a hard-pushed drive does not go muddy or harsh (12 dB/oct below, "
         "6 dB/oct above each band). A band works while its Range is above 0 dB.");
    tip (bind (p, new Toggle (row (64, 124, rowB), this, ext2Base + kTailExt2Advanced, "Advanced")),
         "Gently's Advanced mode: a Threshold per band (the sliders at the right of the frequency display) and a Drive "
         "for the region it cuts. Off, Gently starts cutting at -18 dB, as it always did.");
    for (auto& v : tailBandViews)
        v.clear ();
    tailBandButtons.clear ();
    static const char* const bandNames[3] = {"1", "2", "S"};
    static const char* const bandTips[3] = {
        "Show Gently's first band (green in the display).", "Show Gently's second band (blue in the display).",
        "Show Gently's Sub band: from the bottom of the spectrum, it starts to taper at its Freq."};
    for (int k = 0; k < 3; ++k)
    {
        auto* bt = new ActionButton (row (128 + k * 18, 144 + k * 18, rowB), bandNames[k], [this, k] { showTailBand (k); },
                                     [this, k] { return tailBand == k; });
        bt->setTooltipText (bandTips[k]);
        p->addView (bt);
        tailBandButtons.push_back (bt);
        CView* views[3];
        if (k < 2)
        {
            const uint32_t f = extBase + (k == 0 ? kTailExtClarityFreq : kTailExtClarity2Freq);
            const uint32_t w = extBase + (k == 0 ? kTailExtClarityWidth : kTailExtClarity2Width);
            const uint32_t g = extBase + (k == 0 ? kTailExtClarityRange : kTailExtClarity2Range);
            views[0] = bind (p, new NumberBox (row (184, 228, rowB), this, f));
            views[1] = bind (p, new NumberBox (row (232, 264, rowB), this, w));
            views[2] = bind (p, new NumberBox (row (268, 306, rowB), this, g));
            tip (views[0], "Gently: the centre of this band.");
            tip (views[1], "Gently: this band's width in octaves (or Alt-drag the band in the display).");
            tip (views[2], "Gently: the most this band is turned down; at 0 dB the band does nothing.");
        }
        else
        {
            views[0] = bind (p, new NumberBox (row (184, 228, rowB), this, ext2Base + kTailExt2SubFreq));
            views[1] = bind (p, new Toggle (row (232, 264, rowB), this, ext2Base + kTailExt2Sub, "Sub"));
            views[2] = bind (p, new NumberBox (row (268, 306, rowB), this, ext2Base + kTailExt2SubRange));
            tip (views[0], "Gently's Sub band: where it starts to taper off.");
            tip (views[1], "Gently's Sub band on or off (it works once its Range is above 0 dB).");
            tip (views[2], "Gently's Sub band: the most it turns the sub region down; at 0 dB it does nothing.");
        }
        for (auto* v : views)
            tailBandViews[k].push_back (v);
    }
    showTailBand (tailBand);
    // the colour filters' amounts (their button is at the end of the row above)
    tip (bind (p, new NumberBox (row (312, 350, rowB), this, extBase + kTailExtColorLo)), "Colour: the low shelf amount.");
    tip (bind (p, new NumberBox (row (353, 391, rowB), this, extBase + kTailExtColorHi)), "Colour: the peak amount.");
    tip (bind (p, new NumberBox (row (394, 444, rowB), this, extBase + kTailExtColorFreq)), "Colour: the peak's frequency.");
    tip (bind (p, new NumberBox (row (447, 480, rowB), this, extBase + kTailExtColorWidth)), "Colour: the peak's width.");
    tip (bind (p, new Knob (CRect (490, y + 4, 546, y + 68), this, base + kTailDrive, nullptr, true)),
         "Gain into the Analog curve (0 dB: only peaks past half scale are shaped).");
    tip (bind (p, new Knob (CRect (552, y + 4, 608, y + 68), this, base + kTailMix)), "Dry/wet of the saturator.");
    tip (bind (p, new Knob (CRect (614, y + 4, 670, y + 68), this, extBase + kTailExtOutput)), "Output level of the saturator.");
    return p;
}

void EditorBase::showTailBand (int band)
{
    tailBand = band < 0 ? 0 : band > 2 ? 2 : band;
    for (int k = 0; k < 3; ++k)
        for (auto* v : tailBandViews[k])
            v->setVisible (k == tailBand);
    for (auto* b : tailBandButtons)
        b->invalid ();
}

void EditorBase::applyParamTooltips (const char* (*helpFor) (uint32_t))
{
    for (auto& [id, views] : byParam)
        if (const char* t = helpFor (id))
            for (auto* v : views)
                v->setTooltipText (t);
}

} // namespace pk

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

Panel* EditorBase::addTailPanel (CViewContainer* parent, const CRect& r, uint32_t base, uint32_t extBase, const char* title)
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
    tip (bind (p, new Toggle (row (392, 460, rowA), this, extBase + kTailExtDcFilter, "DC Filter")),
         "Remove DC offset before the curve.");
    tip (bind (p, new Toggle (row (10, 70, rowB), this, extBase + kTailExtClarity, "Clarity")),
         "A compressor on one band of the low mids, so a hard-pushed drive does not go muddy (12 dB/oct below, 6 dB/oct "
         "above).");
    tip (bind (p, new NumberBox (row (74, 128, rowB), this, extBase + kTailExtClarityFreq)), "Clarity: the centre of its band.");
    tip (bind (p, new NumberBox (row (132, 184, rowB), this, extBase + kTailExtClarityWidth)), "Clarity: the band's width in octaves.");
    tip (bind (p, new Toggle (row (190, 246, rowB), this, extBase + kTailExtColorOn, "Color")),
         "Colour filters: an EQ before the curve, undone after it, so the curve bites harder or softer on some frequencies.");
    tip (bind (p, new NumberBox (row (250, 300, rowB), this, extBase + kTailExtColorLo)), "Colour: the low shelf amount.");
    tip (bind (p, new NumberBox (row (304, 354, rowB), this, extBase + kTailExtColorHi)), "Colour: the peak amount.");
    tip (bind (p, new NumberBox (row (358, 414, rowB), this, extBase + kTailExtColorFreq)), "Colour: the peak's frequency.");
    tip (bind (p, new NumberBox (row (418, 460, rowB), this, extBase + kTailExtColorWidth)), "Colour: the peak's width.");
    tip (bind (p, new Knob (CRect (470, y + 4, 526, y + 68), this, base + kTailDrive, nullptr, true)),
         "Gain into the Analog curve (0 dB: only peaks past half scale are shaped).");
    tip (bind (p, new Knob (CRect (534, y + 4, 590, y + 68), this, base + kTailMix)), "Dry/wet of the saturator.");
    tip (bind (p, new Knob (CRect (598, y + 4, 654, y + 68), this, extBase + kTailExtOutput)), "Output level of the saturator.");
    return p;
}

void EditorBase::applyParamTooltips (const char* (*helpFor) (uint32_t))
{
    for (auto& [id, views] : byParam)
        if (const char* t = helpFor (id))
            for (auto* v : views)
                v->setTooltipText (t);
}

} // namespace pk

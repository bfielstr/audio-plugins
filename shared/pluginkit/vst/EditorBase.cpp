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

Panel* EditorBase::addTailPanel (CViewContainer* parent, const CRect& r, uint32_t base, const char* title)
{
    auto* p = new Panel (r, title);
    parent->addView (p);
    const double y = r.getHeight () - 72.0; // knobs at the bottom, the other controls centred on them
    auto tip = [] (CView* v, const char* t) { v->setTooltipText (t); };
    tip (bind (p, new Toggle (CRect (10, y + 28, 56, y + 48), this, base + kTailOn, "On")),
         "Smacheratr (the Analog curve) at the very end of this plug-in: off, the sound passes untouched.");
    tip (bind (p, new Toggle (CRect (64, y + 28, 144, y + 48), this, base + kTailPreLimit, "Pre-Limit")),
         "A look-ahead limiter before the drive, so transients do not push further into the curve than the rest.");
    tip (bind (p, new NumberBox (CRect (150, y + 29, 216, y + 47), this, base + kTailThreshold)),
         "Level the pre-limiter holds the signal to, before the drive.");
    tip (bind (p, new Knob (CRect (228, y + 4, 284, y + 68), this, base + kTailDrive, nullptr, true)),
         "Gain into the Analog curve (0 dB: only peaks past half scale are shaped).");
    tip (bind (p, new Choice (CRect (298, y + 28, 408, y + 48), this, base + kTailPostClip)),
         "Clip the output at 0 dB after the curve (Soft: the Analog curve again, Hard: a digital clip).");
    tip (bind (p, new Knob (CRect (420, y + 4, 476, y + 68), this, base + kTailMix)), "Dry/wet of the saturator.");
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

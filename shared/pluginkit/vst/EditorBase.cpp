#include "EditorBase.h"

#include "pluginkit/TailParams.h"
#include "pluginkit/vst/Clipboard.h"
#include "pluginkit/ui/LayoutCheck.h"
#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pk {

using namespace VSTGUI;
using namespace Steinberg;

EditorBase::EditorBase (ControllerBase* c, double w, double h)
: VSTGUIEditor (c), controller (c), contentHeight (h), baseWidth (w), baseHeight (h + kInfoHeight)
{
    scale = std::clamp (c->uiScale, kMinZoom, kMaxZoom);
    ViewRect vr (0, 0, (int32)std::lround (baseWidth * scale), (int32)std::lround (baseHeight * scale));
    setRect (vr);
    setIdleRate (33);
    hoverWatch.editor = this;
}

bool PLUGIN_API EditorBase::open (void* parent, const PlatformType& platformType)
{
    if (frame)
        return false;
    frame = new CFrame (CRect (0, 0, baseWidth, baseHeight), this);
    // the margins left when the window's shape differs from the UI's (layoutFrame) show the ground
    frame->setBackgroundColor (theme::kGround);
    byParam.clear ();
    buildUI (frame);
    addInfoStrip ();
    writeLayoutReport ();
    // every tooltip set while building, wrapped to lines (views made later are wrapped when the mouse
    // first enters them: helpFor, from HoverWatch)
    prepareTooltips (frame);
    frame->registerMouseObserver (&hoverWatch);
    frame->enableTooltips (controller->uiShowTips, 600);
    frame->open (parent, platformType);
    frame->setZoom (scale);
    // the host may have given the window another shape than the UI's (a size it kept, or one it chose)
    layoutFrame (rect.getWidth (), rect.getHeight ());
    return true;
}

VSTGUI::CFrame* EditorBase::openDetached (double zoom, const RootWrapper& wrap)
{
    if (frame)
        return nullptr;
    // as open () builds it, without the window, the tooltips and the hover help
    frame = new CFrame (CRect (0, 0, baseWidth, baseHeight), this);
    frame->setBackgroundColor (theme::kGround);
    byParam.clear ();
    buildUI (frame);
    addInfoStrip ();
    if (wrap && frame->getNbViews () > 0)
    {
        CView* root = frame->getView (0);
        frame->removeView (root, false); // (the reference the frame adopted goes over to the holder)
        CViewContainer* holder = wrap (frame->getViewSize ());
        holder->addView (root);
        frame->addView (holder);
    }
    frame->setZoom (std::clamp (zoom, kMinZoom, kMaxZoom));
    frame->attached (frame); // (the views find their frame and parents, as in a window)
    return frame;
}

void PLUGIN_API EditorBase::close ()
{
    onClose ();
    byParam.clear ();
    if (frame)
    {
        frame->unregisterMouseObserver (&hoverWatch);
        hoverWatch.hovered = nullptr;
        info = nullptr;
        frame->forget ();
        frame = nullptr;
    }
}

void EditorBase::addInfoStrip ()
{
    // The plug-in built its content into one root container of baseWidth x contentHeight (its
    // Background, which draws the window). The root grows by the strip, so the window's frame and
    // ground run around the info box too, and nothing the plug-in placed moves.
    CViewContainer* root = frame;
    if (frame->getNbViews () > 0)
        if (auto* c = frame->getView (0)->asViewContainer ())
            root = c;
    if (root != frame)
    {
        CRect rr = root->getViewSize ();
        rr.bottom = rr.top + baseHeight;
        root->setViewSize (rr);
        root->setMouseableArea (rr);
    }
    // the box: 8 px in from the window's sides and bottom, starting where the content ends (the
    // content's last panels end 8 px above that)
    info = new InfoBox (CRect (8, contentHeight, baseWidth - 8, baseHeight - 8));
    root->addView (info);
}

void EditorBase::writeLayoutReport ()
{
    // A debug aid: with PK_LAYOUT_REPORT set to a file, every editor opened appends the controls that
    // overlap or touch and the texts that spill out of their boxes (pk::layoutReport), under a line
    // naming the editor and the count. The macOS host tests set it; nothing is written otherwise.
    const char* path = std::getenv ("PK_LAYOUT_REPORT");
    if (!path || !*path)
        return;
    if (FILE* f = std::fopen (path, "a"))
    {
        const auto lines = layoutReport (frame);
        std::fprintf (f, "== %s: %zu\n", typeName (typeid (*this)).c_str (), lines.size ());
        for (const auto& l : lines)
            std::fprintf (f, "%s\n", l.c_str ());
        std::fclose (f);
    }
}

void EditorBase::layoutFrame (double w, double h)
{
    if (!frame || w <= 0 || h <= 0)
        return;
    // Keep shape, pad: the UI is zoomed as large as fits the window both ways and centred in it, the
    // rest of the window is the frame's ground. setZoom gives the frame its scale (and tells the
    // platform layer, which renders text and paths for it); then the frame takes the window's size,
    // and its transform both scales and offsets the content to the centre. VSTGUI maps mouse
    // positions, invalid rectangles, drawing and pop-up menus through the frame's whole transform, so
    // the offset is followed everywhere, as the zoom always was.
    const double z = zoomFor (w, h, baseWidth, baseHeight);
    frame->setZoom (z);
    frame->setSize (w, h);
    const double dx = std::round ((w - baseWidth * z) / 2), dy = std::round ((h - baseHeight * z) / 2);
    frame->setTransform (CGraphicsTransform (z, 0, 0, z, std::max (0.0, dx), std::max (0.0, dy)));
    frame->invalid ();
}

tresult PLUGIN_API EditorBase::checkSizeConstraint (ViewRect* rect)
{
    if (!rect)
        return kInvalidArgument;
    // Any shape: the window may be resized freely, the UI keeps its own shape inside it (layoutFrame).
    // Only the size is bounded, each side between the smallest and the largest zoom's.
    const int32 w = std::clamp (rect->getWidth (), (int32)std::lround (baseWidth * kMinZoom), (int32)std::lround (baseWidth * kMaxZoom));
    const int32 h = std::clamp (rect->getHeight (), (int32)std::lround (baseHeight * kMinZoom), (int32)std::lround (baseHeight * kMaxZoom));
    rect->right = rect->left + w;
    rect->bottom = rect->top + h;
    return kResultTrue;
}

tresult PLUGIN_API EditorBase::onSize (ViewRect* newSize)
{
    if (!newSize)
        return kInvalidArgument;
    scale = zoomFor (newSize->getWidth (), newSize->getHeight (), baseWidth, baseHeight);
    controller->uiScale = scale; // (what is kept: the next window opens at this zoom, in the UI's shape)
    if (frame)
        layoutFrame (newSize->getWidth (), newSize->getHeight ());
    return VSTGUIEditor::onSize (newSize);
}

void EditorBase::resizeTo (double s)
{
    s = std::clamp (s, kMinZoom, kMaxZoom);
    ViewRect vr (0, 0, (int32)std::lround (baseWidth * s), (int32)std::lround (baseHeight * s));
    if (plugFrame)
        plugFrame->resizeView (this, &vr);
}

void EditorBase::HoverWatch::onMouseEntered (CView* view, CFrame*)
{
    // the frame enters views from the outside in: the last one entered is the innermost
    hovered = view;
    if (editor->info)
        editor->info->showFor (view);
}

void EditorBase::HoverWatch::onMouseExited (CView* view, CFrame* f)
{
    // left (or removed): its parent is under the mouse again, until the next view is entered
    if (hovered != view)
        return;
    CView* parent = view->getParentView ();
    hovered = parent == f ? nullptr : parent;
    if (editor->info)
        editor->info->showFor (hovered);
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
                                 uint32_t ext3Base, const char* title)
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
    // Gentlr: one button, Advanced, a band selector and the selected band's controls (all bands' are
    // made; the other band's are hidden)
    tip (bind (p, new Toggle (row (10, 60, rowB), this, extBase + kTailExtClarity, "Gentlr")),
         "Gentlr: a compressor on up to four bands (the Sub band from the bottom, the High band to the top), so a hard-pushed drive does not go muddy or "
         "harsh (each band's shape: the Slope, at the right of the title). A band works while its Range is above 0 dB.");
    tip (bind (p, new Toggle (row (64, 132, rowB), this, ext2Base + kTailExt2Advanced, "Advanced")),
         "Gentlr's Advanced mode: a Threshold per band (the sliders at the right of the frequency display) and a Drive "
         "for the region it cuts. Off, Gentlr starts cutting at -18 dB, as it always did.");
    for (auto& v : tailBandViews)
        v.clear ();
    tailBandButtons.clear ();
    static const char* const bandNames[4] = {"1", "2", "S", "H"};
    static const char* const bandTips[4] = {
        "Show Gentlr's first band (Gentlr in the display).", "Show Gentlr's second band (Gentlr 2 in the display).",
        "Show Gentlr's Sub band: from the bottom of the spectrum, it starts to taper at its Freq.",
        "Show Gentlr's High band: from its Freq, where it starts to taper, to the top of the spectrum."};
    for (int k = 0; k < 4; ++k)
    {
        auto* bt = new ActionButton (row (136 + k * 21, 154 + k * 21, rowB), bandNames[k], [this, k] { showTailBand (k); },
                                     [this, k] { return tailBand == k; });
        bt->setTooltipText (bandTips[k]);
        p->addView (bt);
        tailBandButtons.push_back (bt);
        auto& views = tailBandViews[k];
        if (k < 2)
        {
            const uint32_t f = extBase + (k == 0 ? kTailExtClarityFreq : kTailExtClarity2Freq);
            const uint32_t w = extBase + (k == 0 ? kTailExtClarityWidth : kTailExtClarity2Width);
            const uint32_t g = extBase + (k == 0 ? kTailExtClarityRange : kTailExtClarity2Range);
            views.push_back (bind (p, new NumberBox (row (221, 277, rowB), this, f)));
            views.push_back (bind (p, new NumberBox (row (281, 313, rowB), this, w)));
            views.push_back (bind (p, new NumberBox (row (317, 361, rowB), this, g)));
            tip (views[0], "Gentlr: the centre of this band.");
            tip (views[1], "Gentlr: this band's width in octaves (or Alt-drag the band in the display).");
            tip (views[2], "Gentlr: the most this band is turned down; at 0 dB the band does nothing.");
        }
        // the Sub and High bands: Frequency and Range where the other bands have theirs (no width, and no
        // button: a band works while its Range is above 0 dB)
        else if (k == 2)
        {
            views.push_back (bind (p, new NumberBox (row (221, 277, rowB), this, ext2Base + kTailExt2SubFreq)));
            views.push_back (bind (p, new NumberBox (row (317, 361, rowB), this, ext2Base + kTailExt2SubRange)));
            tip (views[0], "Gentlr's Sub band: where it starts to taper off.");
            tip (views[1], "Gentlr's Sub band: the most it turns the sub region down; at 0 dB (the default) it does nothing.");
        }
        else
        {
            views.push_back (bind (p, new NumberBox (row (221, 277, rowB), this, ext3Base + kTailExt3HighFreq)));
            views.push_back (bind (p, new NumberBox (row (317, 361, rowB), this, ext3Base + kTailExt3HighRange)));
            tip (views[0], "Gentlr's High band: where it starts to taper off, going down (2 to 16 kHz).");
            tip (views[1], "Gentlr's High band: the most it turns the top of the spectrum down; at 0 dB (the default) it does nothing.");
        }
    }
    showTailBand (tailBand);
    // the colour filters' amounts (their button is at the end of the row above)
    tip (bind (p, new NumberBox (row (365, 406, rowB), this, extBase + kTailExtColorLo)), "Colour: the low shelf amount.");
    tip (bind (p, new NumberBox (row (409, 450, rowB), this, extBase + kTailExtColorHi)), "Colour: the peak amount.");
    tip (bind (p, new NumberBox (row (453, 509, rowB), this, extBase + kTailExtColorFreq)), "Colour: the peak's frequency.");
    tip (bind (p, new NumberBox (row (512, 546, rowB), this, extBase + kTailExtColorWidth)), "Colour: the peak's width.");
    tip (bind (p, new Knob (CRect (552, y + 4, 608, y + 68), this, base + kTailDrive, nullptr, true)),
         "Gain into the Analog curve (0 dB: only peaks past half scale are shaped).");
    tip (bind (p, new Knob (CRect (612, y + 4, 668, y + 68), this, base + kTailMix)), "Dry/wet of the saturator.");
    tip (bind (p, new Knob (CRect (672, y + 4, 728, y + 68), this, extBase + kTailExtOutput)), "Output level of the saturator.");
    return p;
}

void EditorBase::showTailBand (int band)
{
    tailBand = band < 0 ? 0 : band > 3 ? 3 : band;
    for (int k = 0; k < 4; ++k)
        for (auto* v : tailBandViews[k])
            v->setVisible (k == tailBand);
    for (auto* b : tailBandButtons)
        b->invalid ();
}

void EditorBase::applyParamTooltips (const char* (*helpFor) (uint32_t))
{
    // (wrapped for the floating tooltip, the text kept whole for the info box: prepareTooltip)
    for (auto& [id, views] : byParam)
        if (const char* t = helpFor (id))
            for (auto* v : views)
            {
                v->setTooltipText (t);
                prepareTooltip (v);
            }
}

} // namespace pk

namespace pk {

void EditorBase::copySettings () { putClipboardText (frame, controller->settingsText ()); }

bool EditorBase::pasteSettings ()
{
    const bool ok = controller->applySettingsText (clipboardText (frame));
    if (ok)
        refresh ();
    return ok;
}

bool EditorBase::settingsMenuPicked (int index, int first)
{
    if (index == first)
        copySettings ();
    else if (index == first + 1)
        pasteSettings ();
    else
        return false;
    return true;
}

} // namespace pk

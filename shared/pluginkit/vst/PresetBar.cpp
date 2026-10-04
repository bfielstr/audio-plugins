#include "PresetBar.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/ui/Widgets.h"
#include "pluginkit/vst/Presets.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/events.h"

#include <cmath>
#include <string>
#include <vector>

namespace pk {

using namespace VSTGUI;

PresetBar::PresetBar (const CRect& r, ControllerBase* c) : CView (r), ctl (c)
{
    setTooltipText ("Presets: pick one from your preset folder, save the current settings as one, load a "
                    ".vstpreset file, or reset every control to its default.");
}

void PresetBar::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    // a field like the kit's Choice: a well in a thin copper outline, the name in text colour (text dim
    // with no preset), a stroked chevron for the caret
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (r, kDrawFilled);
    draw::outline (ctx, r, theme::kCopper);
    const std::string& name = ctl->presetName ();
    CRect t = r;
    t.inset (8, 0);
    t.right -= 10;
    ctx->setFont (theme::font (10.5, !name.empty ()));
    ctx->setFontColor (name.empty () ? theme::kTextDim : theme::kText);
    ctx->drawString (name.empty () ? "Presets" : name.c_str (), t, kLeftText, true);
    const double cx = std::floor (r.right - 10) + 0.5, cy = std::floor (r.getCenter ().y) + 0.5;
    ctx->setLineWidth (1.0);
    ctx->setFrameColor (theme::kCopperPale);
    ctx->drawLine (CPoint (cx - 3, cy - 1.5), CPoint (cx, cy + 1.5));
    ctx->drawLine (CPoint (cx, cy + 1.5), CPoint (cx + 3, cy - 1.5));
}

void PresetBar::onMouseDownEvent (MouseDownEvent& e)
{
    if (!e.buttonState.isLeft ())
        return;
    showMenu ();
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents (true);
}

void PresetBar::showMenu ()
{
    auto* frame = getFrame ();
    if (!frame)
        return;
    const auto presets = presets::list (ctl->presetFolder ());
    auto menu = makeOwned<COptionMenu> ();
    for (const auto& p : presets)
        menu->addEntry (p.name.c_str (), -1, p.name == ctl->presetName () ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    if (!presets.empty ())
        menu->addSeparator ();
    const int32_t base = (int32_t)presets.size () + (presets.empty () ? 0 : 1);
    menu->addEntry ("Save Preset...");
    menu->addEntry ("Load Preset File...");
    menu->addEntry ("Reset to Defaults");
    CPoint where (getViewSize ().left, getViewSize ().bottom);
    localToFrame (where);
    SharedPointer<CView> self (this);
    menu->popup (frame, where, [this, self, menu, presets, base] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r < 0)
            return;
        if (r < (int32_t)presets.size ())
            ctl->loadPreset (presets[(size_t)r].path);
        else if (r == base)
            savePresetAs ();
        else if (r == base + 1)
            loadPresetFile ();
        else if (r == base + 2)
            ctl->resetToDefaults ();
        invalid ();
    });
}

void PresetBar::savePresetAs ()
{
    auto* frame = getFrame ();
    if (!frame)
        return;
    auto sel = owned (CNewFileSelector::create (frame, CNewFileSelector::kSelectSaveFile));
    if (!sel)
        return;
    sel->setTitle ("Save Preset");
    const std::string folder = ctl->presetFolder ();
    if (!folder.empty ())
        sel->setInitialDirectory (folder.c_str ());
    const std::string name = ctl->presetName ().empty () ? "Preset" : ctl->presetName ();
    sel->setDefaultSaveName ((name + ".vstpreset").c_str ());
    sel->addFileExtension (CFileExtension ("VST3 Preset", "vstpreset"));
    SharedPointer<CView> self (this);
    sel->run ([this, self] (CNewFileSelector* s) {
        if (s->getNumSelectedFiles () > 0)
            if (UTF8StringPtr p = s->getSelectedFile (0))
                ctl->savePreset (presets::withExtension (p));
        invalid ();
    });
}

void PresetBar::loadPresetFile ()
{
    auto* frame = getFrame ();
    if (!frame)
        return;
    auto sel = owned (CNewFileSelector::create (frame, CNewFileSelector::kSelectFile));
    if (!sel)
        return;
    sel->setTitle ("Load Preset");
    const std::string folder = ctl->presetFolder ();
    if (!folder.empty ())
        sel->setInitialDirectory (folder.c_str ());
    sel->addFileExtension (CFileExtension ("VST3 Preset", "vstpreset"));
    SharedPointer<CView> self (this);
    sel->run ([this, self] (CNewFileSelector* s) {
        if (s->getNumSelectedFiles () > 0)
            if (UTF8StringPtr p = s->getSelectedFile (0))
                ctl->loadPreset (p);
        invalid ();
    });
}

} // namespace pk

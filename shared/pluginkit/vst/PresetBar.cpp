#include "PresetBar.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/ui/Widgets.h"
#include "pluginkit/vst/Presets.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/events.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace pk {

using namespace VSTGUI;

namespace {

void drawText (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, bool bold, CHoriTxtAlign align)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, align, true);
}

// ---- the prompt ------------------------------------------------------------------------------------
class PromptField : public CTextEdit
{
public:
    PromptField (const CRect& r) : CTextEdit (r, nullptr, -1)
    {
        setBackColor (theme::kWell);
        setFrameColor (theme::kCopper);
        setFontColor (theme::kText);
        setFont (theme::font (11.0));
        setHoriAlign (kLeftText);
        setTextInset (CPoint (6, 0));
        setFrameWidth (1.0);
        setStyle (getStyle () & ~(kNoFrame | kRoundRectStyle));
    }
    std::function<void ()> onReturn, onEscape;

protected:
    void platformOnKeyboardEvent (KeyboardEvent& e) override
    {
        const bool down = e.type == EventType::KeyDown;
        const VirtualKey virt = e.virt;
        CTextEdit::platformOnKeyboardEvent (e);
        if (!down)
            return;
        if (virt == VirtualKey::Return && onReturn)
            onReturn ();
        else if (virt == VirtualKey::Escape && onEscape)
            onEscape ();
    }
};

class Prompt : public CViewContainer
{
public:
    static constexpr double kWidth = 340, kRow = 42, kPad = 14;

    Prompt (const CRect& area, const std::string& title, std::vector<PresetBar::Field> fields, const std::string& okLabel,
            PresetBar::PromptOk ok)
    : CViewContainer (area), titleText (title), onOk (std::move (ok))
    {
        setTransparency (true);
        const double h = kPad + 22 + kRow * (double)fields.size () + 18 + 26 + kPad;
        const CPoint c = CRect (0, 0, area.getWidth (), area.getHeight ()).getCenter ();
        box = CRect (std::round (c.x - kWidth / 2), std::round (c.y - h / 2), std::round (c.x + kWidth / 2), std::round (c.y + h / 2));
        double y = box.top + kPad + 22;
        for (const auto& f : fields)
        {
            labels.push_back ({CRect (box.left + kPad, y, box.right - kPad, y + 14), f.label});
            auto* edit = new PromptField (CRect (box.left + kPad, y + 15, box.right - kPad, y + 37));
            edit->setText (f.text.c_str ());
            edit->onReturn = [this] { accept (); };
            edit->onEscape = [this] { cancel (); };
            addView (edit);
            edits.push_back (edit);
            y += kRow;
        }
        messageRect = CRect (box.left + kPad, y, box.right - kPad, y + 16);
        const double by = box.bottom - kPad - 24;
        addView (new ActionButton (CRect (box.right - kPad - 160, by, box.right - kPad - 84, by + 24), "Cancel", [this] { cancel (); }));
        addView (new ActionButton (CRect (box.right - kPad - 76, by, box.right - kPad, by + 24), okLabel, [this] { accept (); }));
    }

    void focusFirst ()
    {
        if (!edits.empty () && getFrame ())
            getFrame ()->setFocusView (edits.front ());
    }

    void drawBackgroundRect (CDrawContext* ctx, const CRect&) override
    {
        // the editor dimmed under it; the prompt a lifted panel in a copper outline
        ctx->setFillColor (theme::withAlpha (theme::kGround, 190));
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ()), kDrawFilled);
        ctx->setFillColor (theme::kPanel);
        ctx->drawRect (box, kDrawFilled);
        draw::outline (ctx, box, theme::kCopper);
        draw::brackets (ctx, box, 8, theme::kCopperPale);
        CRect t (box.left + kPad, box.top + kPad - 2, box.right - kPad, box.top + kPad + 16);
        drawText (ctx, titleText, t, theme::kText, 12.0, true, kLeftText);
        for (const auto& [r, s] : labels)
            drawText (ctx, s, r, theme::kCopperPale, 10.0, false, kLeftText);
        if (!message.empty ())
            drawText (ctx, message, messageRect, theme::kEnergyPeak, 10.0, false, kLeftText);
    }

    void onMouseDownEvent (MouseDownEvent& e) override
    {
        CViewContainer::onMouseDownEvent (e);
        e.consumed = true; // modal: nothing under it gets the click
    }

private:
    std::vector<std::string> values ()
    {
        // a field being typed in hands its text over when it loses the focus
        if (auto* f = getFrame ())
            if (f->getFocusView () && std::find (edits.begin (), edits.end (), f->getFocusView ()) != edits.end ())
                f->setFocusView (nullptr);
        std::vector<std::string> out;
        for (auto* e : edits)
            out.push_back (e->getText ().getString ());
        return out;
    }

    void accept ()
    {
        if (closing)
            return;
        const std::string err = onOk ? onOk (values ()) : std::string ();
        if (!err.empty ())
        {
            message = err;
            invalid ();
            return;
        }
        close ();
    }
    void cancel () { close (); }
    void close ()
    {
        if (closing)
            return;
        closing = true;
        if (auto* f = getFrame ())
        {
            SharedPointer<CView> self (this);
            f->setFocusView (nullptr);
            f->doAfterEventProcessing ([self] {
                if (auto* parent = self->getParentView () ? self->getParentView ()->asViewContainer () : nullptr)
                    parent->removeView (self, true);
            });
        }
    }

    std::string titleText, message;
    PresetBar::PromptOk onOk;
    CRect box, messageRect;
    std::vector<std::pair<CRect, std::string>> labels;
    std::vector<PromptField*> edits;
    bool closing = false;
};

// ---- the menu ---------------------------------------------------------------------------------------
// Fills `menu` from the model; every selectable entry's tag is its index in `flat`.
void fillMenu (COptionMenu* menu, const std::vector<presets::MenuEntry>& entries, std::vector<presets::MenuEntry>& flat)
{
    menu->setStyle (menu->getStyle () | COptionMenu::kMultipleCheckStyle);
    for (const auto& e : entries)
    {
        if (e.separator)
        {
            menu->addSeparator ();
            continue;
        }
        if (!e.sub.empty ())
        {
            auto sub = makeOwned<COptionMenu> ();
            fillMenu (sub, e.sub, flat);
            auto* item = menu->addEntry (sub, e.title.c_str ());
            if (item && e.checked)
                item->setChecked (true);
            continue;
        }
        auto* item = new CMenuItem (e.title.c_str (), (int32_t)flat.size ());
        if (e.heading)
            item->setIsTitle (true);
        item->setEnabled (e.enabled && !e.heading);
        item->setChecked (e.checked);
        menu->addEntry (item);
        flat.push_back (e);
    }
}

} // namespace

namespace {
// the plug-in's own presets: its controller's
class ControllerPresets : public PresetSource
{
public:
    explicit ControllerPresets (ControllerBase* c) : ctl (c) {}
    std::string presetName () override { return ctl->presetName (); }
    presets::Kind presetKind () override { return ctl->presetKind (); }
    std::string presetPath () override { return ctl->presetPath (); }
    std::string presetFolder () override { return ctl->presetFolder (); }
    std::string& tagFilter () override { return ctl->tagFilter; }
    std::vector<presets::MenuEntry> presetMenu (std::vector<presets::Item>* f, std::vector<presets::Item>* u) override
    {
        return ctl->presetMenu (f, u);
    }
    const std::vector<presets::FactoryPreset>& factoryPresets () override { return ctl->factoryPresets (); }
    bool loadInit () override { return ctl->loadInit (); }
    bool loadFactory (int index) override { return ctl->loadFactory (index); }
    bool loadPreset (const std::string& path) override { return ctl->loadPreset (path); }
    bool savePreset (const std::string& path) override { return ctl->savePreset (path); }
    std::string userPresetPath (const std::string& name, const std::string& category) override { return ctl->userPresetPath (name, category); }
    bool saveUserPreset (const std::string& name, const std::string& category, const std::vector<std::string>& tags) override
    {
        return ctl->saveUserPreset (name, category, tags);
    }
    bool setPresetTags (const std::string& path, const std::vector<std::string>& tags) override { return ctl->setPresetTags (path, tags); }
    bool renamePreset (const std::string& path, const std::string& n) override { return ctl->renamePreset (path, n); }
    bool deletePreset (const std::string& path) override { return ctl->deletePreset (path); }
    bool saveAsDefault () override { return ctl->saveAsDefault (); }
    bool loadDefault () override { return ctl->loadDefault (); }
    bool resetDefault () override { return ctl->resetDefault (); }

private:
    ControllerBase* ctl;
};
} // namespace

PresetBar::PresetBar (const CRect& r, std::shared_ptr<PresetSource> source) : CView (r), src (std::move (source)) {}

PresetBar::PresetBar (const CRect& r, ControllerBase* c) : PresetBar (r, std::make_shared<ControllerPresets> (c))
{
    setTooltipText ("Presets: Init, the factory presets and yours (sub-menus by category; Tags shows only the "
                    "presets with a tag). Save, rename, tag or delete your presets; Save as Default makes a new "
                    "instance start from the current settings.");
}

void PresetBar::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    // a field like the kit's Choice: a well in a thin copper outline, the name in text colour (text dim
    // with no preset), a stroked chevron for the caret
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (r, kDrawFilled);
    draw::outline (ctx, r, theme::kCopper);
    const std::string name = src->presetName ();
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
    std::vector<presets::Item> factory, user;
    const auto model = src->presetMenu (&factory, &user);
    auto menu = makeOwned<COptionMenu> ();
    auto flat = std::make_shared<std::vector<presets::MenuEntry>> ();
    fillMenu (menu, model, *flat);
    CPoint where (getViewSize ().left, getViewSize ().bottom);
    localToFrame (where);
    SharedPointer<CView> self (this);
    menu->popup (frame, where, [this, self, flat, user] (COptionMenu* m) {
        int32_t idx = -1;
        COptionMenu* in = m->getLastItemMenu (idx);
        if (!in || idx < 0)
            return;
        CMenuItem* item = in->getEntry (idx);
        if (!item || item->getTag () < 0 || item->getTag () >= (int32_t)flat->size ())
            return;
        const presets::MenuEntry e = (*flat)[(size_t)item->getTag ()];
        using A = presets::Action;
        switch (e.action)
        {
            case A::Init: src->loadInit (); break;
            case A::Factory: src->loadFactory (e.index); break;
            case A::User:
                if (e.index >= 0 && e.index < (int)user.size ())
                    src->loadPreset (user[(size_t)e.index].path);
                break;
            case A::FilterTag: src->tagFilter () = e.tag; break;
            case A::ClearFilter: src->tagFilter ().clear (); break;
            case A::Save: src->savePreset (src->presetPath ()); break;
            case A::SaveAs: saveAs (); break;
            case A::Rename: renameCurrent (); break;
            case A::EditTags: editTags (); break;
            case A::Delete: deleteCurrent (); break;
            case A::SaveDefault: src->saveAsDefault (); break;
            case A::LoadDefault: src->loadDefault (); break;
            case A::ResetDefault: src->resetDefault (); break;
            case A::SaveFile: savePresetFile (); break;
            case A::LoadFile: loadPresetFile (); break;
            case A::None: break;
        }
        invalid ();
    });
}

void PresetBar::prompt (const std::string& title, std::vector<Field> fields, const std::string& okLabel, PromptOk onOk)
{
    showPrompt (getFrame (), title, std::move (fields), okLabel, std::move (onOk));
}

void showPrompt (CFrame* frame, const std::string& title, std::vector<PresetBar::Field> fields, const std::string& okLabel,
                 PresetBar::PromptOk onOk)
{
    if (!frame || frame->getNbViews () == 0)
        return;
    // over the plug-in's root view (the window's content), the last child: drawn on top, gets the mouse
    const CRect area = frame->getView (0)->getViewSize ();
    auto* p = new Prompt (area, title, std::move (fields), okLabel, std::move (onOk));
    frame->addView (p);
    p->invalid ();
    p->focusFirst ();
}

namespace {
std::string currentCategory (PresetSource* ctl)
{
    if (ctl->presetKind () == presets::Kind::User)
        return presets::readMeta (ctl->presetPath ()).category;
    if (ctl->presetKind () == presets::Kind::Factory)
        for (const auto& f : ctl->factoryPresets ())
            if (f.path == ctl->presetPath ())
                return f.category;
    return {};
}
std::vector<std::string> currentTags (PresetSource* ctl)
{
    if (ctl->presetKind () == presets::Kind::User)
        return presets::readMeta (ctl->presetPath ()).tags;
    if (ctl->presetKind () == presets::Kind::Factory)
        for (const auto& f : ctl->factoryPresets ())
            if (f.path == ctl->presetPath ())
                return f.tags;
    return {};
}
const char* const kBadName = "Use a name without / \\ : * ? \" < > | (and not Init).";
} // namespace

void PresetBar::saveAs ()
{
    const presets::Kind k = src->presetKind ();
    std::string name = src->presetName ();
    if (k == presets::Kind::Init || k == presets::Kind::Default || k == presets::Kind::None)
        name.clear ();
    SharedPointer<CView> self (this);
    prompt ("Save Preset", {{"Name", name}, {"Category (optional)", currentCategory (src.get ())}, {"Tags (comma separated)", presets::joinTags (currentTags (src.get ()))}},
            "Save", [this, self] (const std::vector<std::string>& v) -> std::string {
                const std::string n = presets::trim (v[0]), cat = presets::trim (v[1]);
                const auto tags = presets::parseTags (v[2]);
                if (!presets::validName (n))
                    return kBadName;
                if (!cat.empty () && !presets::validName (cat, true))
                    return "Use a category without / \\ : * ? \" < > |.";
                std::error_code ec;
                const std::string path = src->userPresetPath (n, cat);
                if (!path.empty () && std::filesystem::exists (path, ec) && path != src->presetPath ())
                {
                    // ask before replacing another preset
                    if (auto* f = getFrame ())
                        f->doAfterEventProcessing ([this, self, n, cat, tags] {
                            prompt ("Replace \"" + n + "\"?", {}, "Replace", [this, self, n, cat, tags] (const std::vector<std::string>&) {
                                src->saveUserPreset (n, cat, tags);
                                invalid ();
                                return std::string ();
                            });
                        });
                    return {};
                }
                if (!src->saveUserPreset (n, cat, tags))
                    return "Could not save the preset.";
                invalid ();
                return {};
            });
}

void PresetBar::renameCurrent ()
{
    if (src->presetKind () != presets::Kind::User)
        return;
    const std::string path = src->presetPath ();
    SharedPointer<CView> self (this);
    prompt ("Rename Preset", {{"Name", src->presetName ()}}, "Rename", [this, self, path] (const std::vector<std::string>& v) -> std::string {
        const std::string n = presets::trim (v[0]);
        if (n == presets::nameOf (path))
            return {};
        if (!presets::validName (n))
            return kBadName;
        if (!src->renamePreset (path, n))
            return "A preset of that name is already there.";
        invalid ();
        return {};
    });
}

void PresetBar::editTags ()
{
    if (src->presetKind () != presets::Kind::User)
        return;
    const std::string path = src->presetPath ();
    prompt ("Tags for \"" + src->presetName () + "\"", {{"Tags (comma separated)", presets::joinTags (presets::readMeta (path).tags)}}, "OK",
            [this, path] (const std::vector<std::string>& v) -> std::string {
                return src->setPresetTags (path, presets::parseTags (v[0])) ? std::string () : std::string ("Could not write the preset.");
            });
}

void PresetBar::deleteCurrent ()
{
    if (src->presetKind () != presets::Kind::User)
        return;
    const std::string path = src->presetPath ();
    SharedPointer<CView> self (this);
    prompt ("Delete \"" + src->presetName () + "\"?", {}, "Delete", [this, self, path] (const std::vector<std::string>&) -> std::string {
        if (!src->deletePreset (path))
            return "Could not delete the preset.";
        invalid ();
        return {};
    });
}

void PresetBar::savePresetFile ()
{
    auto* frame = getFrame ();
    if (!frame)
        return;
    auto sel = owned (CNewFileSelector::create (frame, CNewFileSelector::kSelectSaveFile));
    if (!sel)
        return;
    sel->setTitle ("Save Preset File");
    const std::string folder = src->presetFolder ();
    if (!folder.empty ())
        sel->setInitialDirectory (folder.c_str ());
    const std::string name = src->presetName ().empty () ? "Preset" : src->presetName ();
    sel->setDefaultSaveName ((name + ".vstpreset").c_str ());
    sel->addFileExtension (CFileExtension ("VST3 Preset", "vstpreset"));
    SharedPointer<CView> self (this);
    sel->run ([this, self] (CNewFileSelector* s) {
        if (s->getNumSelectedFiles () > 0)
            if (UTF8StringPtr p = s->getSelectedFile (0))
                src->savePreset (presets::withExtension (p));
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
    const std::string folder = src->presetFolder ();
    if (!folder.empty ())
        sel->setInitialDirectory (folder.c_str ());
    sel->addFileExtension (CFileExtension ("VST3 Preset", "vstpreset"));
    SharedPointer<CView> self (this);
    sel->run ([this, self] (CNewFileSelector* s) {
        if (s->getNumSelectedFiles () > 0)
            if (UTF8StringPtr p = s->getSelectedFile (0))
                src->loadPreset (p);
        invalid ();
    });
}

} // namespace pk

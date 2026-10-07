#include "ControllerBase.h"

#include "pluginkit/SettingsText.h"

#include "pluginkit/CrashDump.h"

#include "EditorBase.h"
#include "Presets.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace pk {

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace fs = std::filesystem;

TableParameter::TableParameter (const ParamTable& t, uint32_t id) : table (t)
{
    const auto& p = t.info (id);
    info.id = id;
    StringConvert::convert (p.name, info.title);
    StringConvert::convert (p.shortName, info.shortTitle);
    StringConvert::convert ("", info.units);
    info.stepCount = p.stepCount ();
    info.defaultNormalizedValue = t.defaultNormalized (id);
    info.unitId = kRootUnitId;
    info.flags = ParameterInfo::kCanAutomate;
    if (p.type == PType::Choice)
        info.flags |= ParameterInfo::kIsList;
    valueNormalized = info.defaultNormalizedValue;
}

void TableParameter::toString (ParamValue n, String128 string) const
{
    StringConvert::convert (table.toText (info.id, table.toPlain (info.id, n)), string);
}

bool TableParameter::fromString (const TChar* string, ParamValue& n) const
{
    const std::string s = StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (string)));
    double plainValue;
    if (!table.fromText (info.id, s, plainValue))
        return false;
    n = table.toNormalized (info.id, plainValue);
    return true;
}

ParamValue TableParameter::toPlain (ParamValue n) const { return table.toPlain (info.id, n); }
ParamValue TableParameter::toNormalized (ParamValue p) const { return table.toNormalized (info.id, p); }

tresult PLUGIN_API ControllerBase::initialize (FUnknown* context)
{
    const tresult r = EditController::initialize (context);
    if (r != kResultOk)
        return r;
    installCrashDump (); // a crash in this plug-in leaves a dump in Documents/bfielstr/CrashDumps
    for (uint32_t id = 0; id < tableRef.size (); ++id)
        parameters.addParameter (makeParameter (id));
    applyStartupDefault ();
    return kResultOk;
}

Parameter* ControllerBase::makeParameter (uint32_t id) { return new TableParameter (tableRef, id); }

tresult PLUGIN_API ControllerBase::setParamNormalized (ParamID tag, ParamValue value)
{
    const tresult r = EditController::setParamNormalized (tag, value);
    if (tag < tableRef.size ())
        for (auto* e : editors)
            e->paramChanged (tag);
    checkLatency ();
    return r;
}

void ControllerBase::watchLatency (const std::atomic<int>* source)
{
    if (!source || std::find (latencySources.begin (), latencySources.end (), source) != latencySources.end ())
        return;
    latencySources.push_back (source);
    latencySeen.push_back (source->load (std::memory_order_relaxed));
}

void ControllerBase::unwatchLatency ()
{
    latencySources.clear ();
    latencySeen.clear ();
}

void ControllerBase::checkLatency ()
{
    bool moved = false;
    for (size_t i = 0; i < latencySources.size (); ++i)
    {
        const int now = latencySources[i]->load (std::memory_order_relaxed);
        if (now == latencySeen[i] || now < 0)
            continue;
        moved |= latencySeen[i] >= 0; // (the first value known is the one the host read)
        latencySeen[i] = now;
    }
    if (moved && componentHandler)
        componentHandler->restartComponent (Steinberg::Vst::kLatencyChanged);
}

namespace {
// The view state's tag in the controller's state, then a count and that many values (uiTailOpen,
// uiColorLayer): a reader takes the ones it knows.
constexpr int32 kViewStateTag = 0x56575354; // 'VWST'
// The editor's layout after it, tagged: its text and its name (uiLayout, uiLayoutName; 0.14).
constexpr int32 kLayoutTag = 0x4c594f54; // 'LYOT'
} // namespace

tresult PLUGIN_API ControllerBase::setState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    IBStreamer s (stream, kLittleEndian);
    double v = 1.0;
    if (s.readDouble (v) && v >= 0.5 && v <= 2.0)
        uiScale = v;
    bool tips = true;
    if (s.readBool (tips))
        uiShowTips = tips;
    if (char8* name = s.readStr8 ())
    {
        presetTitle = name;
        delete[] name;
    }
    // which preset that is ("<kind>:<path>", 0.13; before it: not known)
    currentKind = presets::Kind::None;
    currentPath.clear ();
    if (char8* ref = s.readStr8 ())
    {
        const std::string r = ref;
        delete[] ref;
        const size_t colon = r.find (':');
        if (colon != std::string::npos)
        {
            const int k = std::atoi (r.substr (0, colon).c_str ());
            if (k >= (int)presets::Kind::None && k <= (int)presets::Kind::File)
            {
                currentKind = (presets::Kind)k;
                currentPath = r.substr (colon + 1);
            }
        }
    }
    // the view state after it, tagged (states from before it end here: the defaults stay)
    int32 tag = 0, count = 0;
    if (s.readInt32 (tag) && tag == kViewStateTag && s.readInt32 (count) && count >= 0 && count < 64)
    {
        int32 values[2] = {-1, 0};
        for (int32 i = 0; i < count; ++i)
        {
            int32 v = 0;
            if (!s.readInt32 (v))
                break;
            if (i < 2)
                values[i] = v;
        }
        uiTailOpen = values[0] < 0 ? -1 : values[0] & (kTailOpenSaturator | kTailOpenGentlr);
        uiColorLayer = values[1] == 1 ? 1 : 0;
    }
    // the layout after that, tagged. A state with it keeps the layout it saved, whatever it is ("" too:
    // 0.14 wrote that for its Default, the Classic layout now); a state from before layouts has none and
    // opens in the layout a new instance has (Wide; pluginkit/Layout.h)
    uiLayout = layout::kDefaultLayout;
    uiLayoutName = layout::templateName (uiLayout);
    if (s.readInt32 (tag) && tag == kLayoutTag)
    {
        uiLayout.clear ();
        uiLayoutName.clear ();
        if (char8* text = s.readStr8 ())
        {
            uiLayout = text;
            delete[] text;
        }
        if (char8* name = s.readStr8 ())
        {
            uiLayoutName = name;
            delete[] name;
        }
    }
    refreshEditor ();
    return kResultOk;
}

tresult PLUGIN_API ControllerBase::getState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    IBStreamer s (stream, kLittleEndian);
    // fields are only ever appended: an older version reads what it knows and ignores the rest (the preset
    // reference, then the view state behind its tag)
    const std::string ref = std::to_string ((int)currentKind) + ":" + currentPath;
    return s.writeDouble (uiScale) && s.writeBool (uiShowTips) && s.writeStr8 (presetTitle.c_str ()) &&
                   s.writeStr8 (ref.c_str ()) && s.writeInt32 (kViewStateTag) && s.writeInt32 (2) &&
                   s.writeInt32 (uiTailOpen) && s.writeInt32 (uiColorLayer) && s.writeInt32 (kLayoutTag) &&
                   s.writeStr8 (uiLayout.c_str ()) && s.writeStr8 (uiLayoutName.c_str ())
               ? kResultOk
               : kResultFalse;
}

void ControllerBase::editorAttached (EditorView* e)
{
    if (auto* b = dynamic_cast<EditorBase*> (e))
        if (std::find (editors.begin (), editors.end (), b) == editors.end ())
            editors.push_back (b);
}

void ControllerBase::editorRemoved (EditorView* e)
{
    editors.erase (std::remove (editors.begin (), editors.end (), e), editors.end ());
}

void ControllerBase::refreshEditor ()
{
    for (auto* e : editors)
        e->refresh ();
}

void ControllerBase::setFromUI (uint32_t id, double normalized)
{
    normalized = std::clamp (normalized, 0.0, 1.0);
    setParamNormalized (id, normalized);
    performEdit (id, normalized);
}

void ControllerBase::setPlainFromUI (uint32_t id, double plainValue)
{
    beginEdit (id);
    setFromUI (id, tableRef.toNormalized (id, plainValue));
    endEdit (id);
}

void ControllerBase::markDirty ()
{
    if (!componentHandler)
        return;
    FUnknownPtr<IComponentHandler2> h2 (componentHandler);
    if (h2)
        h2->setDirty (true);
}

//------------------------------------------------------------------------------------------------
void ControllerBase::setPresetInfo (const FUID& processorClassId, const char* pluginName, const char* formerName)
{
    presetClassId = processorClassId;
    presetPlugin = pluginName ? pluginName : "";
    presetFormer = formerName ? formerName : "";
}

layout::Saved ControllerBase::savedLayouts () const { return layout::readSaved (presetFolder ()); }

bool ControllerBase::writeSavedLayouts (const layout::Saved& s) const { return layout::writeSaved (presetFolder (), s); }

std::string ControllerBase::presetFolder () const
{
    return presetPlugin.empty () ? std::string () : presets::userFolder (presetPlugin.c_str (), presetFormer.c_str ());
}

namespace {
// true when `path` is in `folder` or one of its sub-folders (a category)
bool inFolder (const std::string& path, const std::string& folder)
{
    if (path.empty () || folder.empty ())
        return false;
    std::error_code ec;
    const fs::path f = fs::weakly_canonical (folder, ec);
    fs::path p = fs::weakly_canonical (path, ec).parent_path ();
    for (int i = 0; i < 2 && !p.empty (); ++i, p = p.parent_path ())
        if (p == f)
            return true;
    return false;
}
} // namespace

bool ControllerBase::savePreset (const std::string& path, const presets::Meta* meta)
{
    if (path.empty () || !presetClassId.isValid ())
    {
        pendingIsDefault = false;
        return false;
    }
    // the processor answers with its state (see notify), which writes the file
    pendingSavePath = path;
    if (meta)
        pendingMeta = *meta;
    else if (currentKind == presets::Kind::User && path == currentPath)
        pendingMeta = presets::readMeta (path); // Save: keeps its tags and category
    else
        pendingMeta = {};
    pendingMeta.plugin = presetPlugin;
    if (pendingMeta.name.empty () || (!meta && !pendingIsDefault))
        pendingMeta.name = presets::nameOf (path);
    lastSaveOk = false;
    if (IMessage* msg = allocateMessage ())
    {
        msg->setMessageID (presets::kMsgGetState);
        sendMessage (msg);
        msg->release ();
    }
    pendingSavePath.clear ();
    pendingIsDefault = false;
    return lastSaveOk;
}

bool ControllerBase::loadPreset (const std::string& path)
{
    std::vector<char> component, controllerState;
    if (!presetClassId.isValid () || !presets::read (path, presetClassId, component, controllerState))
        return false;
    if (!component.empty ())
    {
        // the processor first (it stores the state and reloads it in the audio thread), then us
        if (IMessage* msg = allocateMessage ())
        {
            msg->setMessageID (presets::kMsgSetState);
            msg->getAttributes ()->setBinary (presets::kAttrData, component.data (), (uint32)component.size ());
            sendMessage (msg);
            msg->release ();
        }
        MemoryStream ms (component.data (), (TSize)component.size ());
        setComponentState (&ms);
    }
    if (!controllerState.empty ())
    {
        // (the preset's editor state, but the layout the editor has now: a preset does not rearrange it)
        const std::string layoutNow = uiLayout, layoutName = uiLayoutName;
        MemoryStream ms (controllerState.data (), (TSize)controllerState.size ());
        setState (&ms);
        uiLayout = layoutNow;
        uiLayoutName = layoutName;
    }
    if (path == defaultPath ())
    {
        currentKind = presets::Kind::Default;
        currentPath.clear ();
        presetTitle = presets::kDefaultName;
    }
    else
    {
        currentKind = inFolder (path, presetFolder ()) ? presets::Kind::User : presets::Kind::File;
        currentPath = path;
        presetTitle = presets::nameOf (path);
    }
    if (componentHandler)
        componentHandler->restartComponent (kParamValuesChanged);
    markDirty ();
    refreshEditor ();
    return true;
}

void ControllerBase::resetToDefaults ()
{
    for (uint32_t id = 0; id < tableRef.size (); ++id)
    {
        if (!isSetting (id))
            continue;
        const double def = tableRef.defaultNormalized (id);
        beginEdit (id);
        setParamNormalized (id, def);
        performEdit (id, def);
        endEdit (id);
    }
    presetTitle.clear ();
    currentKind = presets::Kind::None;
    currentPath.clear ();
    markDirty ();
    refreshEditor ();
}

void ControllerBase::applyValues (const SettingValues& values)
{
    std::vector<double> n (tableRef.size ());
    for (uint32_t id = 0; id < tableRef.size (); ++id)
        n[id] = tableRef.defaultNormalized (id);
    for (const auto& [id, v] : values)
        if (id < tableRef.size ())
            n[id] = std::clamp (v, 0.0, 1.0);
    for (uint32_t id = 0; id < tableRef.size (); ++id)
    {
        if (!isSetting (id)) // (smemplr's hidden MIDI parameters: performance, not settings)
            continue;
        beginEdit (id);
        setParamNormalized (id, n[id]);
        performEdit (id, n[id]);
        endEdit (id);
    }
    resetExtraState ();
}

bool ControllerBase::loadInit ()
{
    applyValues ({});
    currentKind = presets::Kind::Init;
    currentPath.clear ();
    presetTitle = presets::kInitName;
    markDirty ();
    refreshEditor ();
    return true;
}

const std::vector<presets::FactoryPreset>& ControllerBase::factoryPresets ()
{
    if (!factoryParsed)
    {
        factoryParsed = true;
        for (const auto& f : presets::factoryFiles ())
        {
            presets::FactoryPreset fp;
            std::string err;
            if (f.path && f.text && presets::parseFactoryPreset (f.text, f.path, tableRef, fp, err))
                factory.push_back (std::move (fp));
        }
        std::stable_sort (factory.begin (), factory.end (), [] (const presets::FactoryPreset& a, const presets::FactoryPreset& b) {
            return a.category != b.category ? a.category < b.category : a.name < b.name;
        });
    }
    return factory;
}

bool ControllerBase::loadFactory (int index)
{
    const auto& f = factoryPresets ();
    if (index < 0 || index >= (int)f.size ())
        return false;
    applyValues (f[(size_t)index].values);
    currentKind = presets::Kind::Factory;
    currentPath = f[(size_t)index].path;
    presetTitle = f[(size_t)index].name;
    markDirty ();
    refreshEditor ();
    return true;
}

std::vector<presets::Item> ControllerBase::factoryItems ()
{
    std::vector<presets::Item> out;
    const auto& f = factoryPresets ();
    for (size_t i = 0; i < f.size (); ++i)
    {
        presets::Item it;
        it.name = f[i].name;
        it.category = f[i].category;
        it.path = f[i].path;
        it.tags = f[i].tags;
        it.factory = true;
        it.factoryIndex = (int)i;
        out.push_back (std::move (it));
    }
    return out;
}

std::vector<presets::Item> ControllerBase::userItems () const { return presets::listUser (presetFolder ()); }

std::string ControllerBase::userPresetPath (const std::string& name, const std::string& category) const
{
    const std::string folder = presetFolder ();
    if (folder.empty ())
        return {};
    fs::path p (folder);
    if (!category.empty ())
        p /= category;
    p /= name + presets::kExtension;
    return p.string ();
}

bool ControllerBase::saveUserPreset (const std::string& name, const std::string& category, const std::vector<std::string>& tags)
{
    if (!presets::validName (name) || (!category.empty () && !presets::validName (category, true)))
        return false;
    const std::string path = userPresetPath (name, category);
    if (path.empty ())
        return false;
    std::error_code ec;
    fs::create_directories (fs::path (path).parent_path (), ec);
    presets::Meta m;
    m.name = name;
    m.category = category;
    m.tags = tags;
    return savePreset (path, &m);
}

bool ControllerBase::setPresetTags (const std::string& path, const std::vector<std::string>& tags)
{
    presets::Meta m = presets::readMeta (path);
    m.tags = tags;
    m.plugin = presetPlugin;
    if (m.name.empty ())
        m.name = presets::nameOf (path);
    return presets::rewriteMeta (path, presetClassId, m);
}

bool ControllerBase::renamePreset (const std::string& path, const std::string& newName)
{
    if (!presets::validName (newName))
        return false;
    std::error_code ec;
    const fs::path to = fs::path (path).parent_path () / (newName + presets::kExtension);
    if (fs::exists (to, ec) && !fs::equivalent (to, path, ec))
        return false;
    fs::rename (path, to, ec);
    if (ec)
        return false;
    presets::Meta m = presets::readMeta (to.string ());
    m.name = newName;
    m.plugin = presetPlugin;
    presets::rewriteMeta (to.string (), presetClassId, m); // the file name is the name: this keeps the metadata in step
    if (currentPath == path)
    {
        currentPath = to.string ();
        presetTitle = newName;
        markDirty ();
        refreshEditor ();
    }
    return true;
}

bool ControllerBase::deletePreset (const std::string& path)
{
    std::error_code ec;
    if (!fs::remove (path, ec))
        return false;
    if (currentPath == path)
    {
        currentKind = presets::Kind::None;
        currentPath.clear ();
        presetTitle.clear ();
        markDirty ();
        refreshEditor ();
    }
    return true;
}

std::string ControllerBase::defaultPath () const { return presets::defaultPresetPath (presetPlugin); }

bool ControllerBase::hasDefault () const
{
    std::error_code ec;
    const std::string p = defaultPath ();
    return !p.empty () && fs::is_regular_file (p, ec);
}

bool ControllerBase::saveAsDefault ()
{
    if (presetPlugin.empty () || presetFolder ().empty ()) // (presetFolder makes the folder)
        return false;
    presets::Meta m;
    m.name = presets::kDefaultName;
    pendingIsDefault = true;
    return savePreset (defaultPath (), &m);
}

bool ControllerBase::loadDefault () { return hasDefault () ? loadPreset (defaultPath ()) : loadInit (); }

bool ControllerBase::resetDefault ()
{
    std::error_code ec;
    return fs::remove (defaultPath (), ec);
}

void ControllerBase::applyStartupDefault ()
{
    // the user's default layout (Menu > Layout > Use as Default Layout), then the saved default preset's
    // settings (which keep it, as every preset does)
    if (const layout::Saved saved = savedLayouts (); saved.hasDefault)
    {
        uiLayout = saved.defaultLayout;
        uiLayoutName.clear ();
        for (const auto& n : saved.layouts)
            if (n.layout == uiLayout)
                uiLayoutName = n.name;
        if (!layout::templateName (uiLayout).empty ())
            uiLayoutName = layout::templateName (uiLayout);
    }
    applyDefaultPreset ();
    // then the Gentlr defaults (Menu > Defaults), over the default preset's values: the user asked for them
    // explicitly. (No host is connected yet: the processor applies the same in its initialize.)
    for (const auto& [id, n] : gentlrDefaultValues (gentlr, gentlrDefaults ()))
        if (id < tableRef.size ())
            setParamNormalized (id, n);
}

void ControllerBase::applyDefaultPreset ()
{
    if (!presetClassId.isValid () || !hasDefault ())
        return;
    std::vector<char> component, controllerState;
    if (!presets::read (defaultPath (), presetClassId, component, controllerState) || component.empty ())
        return;
    MemoryStream ms (component.data (), (TSize)component.size ());
    if (setComponentState (&ms) != kResultOk)
        return;
    if (!controllerState.empty ())
    {
        const std::string layoutNow = uiLayout, layoutName = uiLayoutName;
        MemoryStream cs (controllerState.data (), (TSize)controllerState.size ());
        setState (&cs);
        uiLayout = layoutNow;
        uiLayoutName = layoutName;
    }
    currentKind = presets::Kind::Default;
    currentPath.clear ();
    presetTitle = presets::kDefaultName;
}

GentlrDefaults ControllerBase::gentlrDefaults () const { return readGentlrDefaults (presetFolder ()); }

bool ControllerBase::writeGentlrDefaults (const GentlrDefaults& d) const { return pk::writeGentlrDefaults (presetFolder (), d); }

std::vector<presets::MenuEntry> ControllerBase::presetMenu (std::vector<presets::Item>* factoryOut,
                                                            std::vector<presets::Item>* userOut)
{
    auto f = factoryItems ();
    auto u = userItems ();
    if (!tagFilter.empty ())
    {
        std::vector<presets::Item> both = f;
        both.insert (both.end (), u.begin (), u.end ());
        if (!presets::hasTag (presets::allTags (both), tagFilter))
            tagFilter.clear (); // no preset has it any more
    }
    presets::MenuState st;
    st.kind = currentKind;
    st.path = currentPath;
    st.tagFilter = tagFilter;
    st.hasDefault = hasDefault ();
    auto menu = presets::buildMenu (f, u, st);
    if (factoryOut)
        *factoryOut = std::move (f);
    if (userOut)
        *userOut = std::move (u);
    return menu;
}

std::string ControllerBase::settingsText ()
{
    SettingValues v;
    for (uint32_t id = 0; id < tableRef.size (); ++id)
        if (isSetting (id))
            v.emplace_back (id, getParamNormalized (id));
    return settingsToText (presetPlugin, v, &tableRef);
}

bool ControllerBase::applySettingsText (const std::string& text)
{
    SettingValues v;
    if (presetPlugin.empty () || !settingsFromText (text, presetPlugin, v, presetFormer))
        return false;
    for (const auto& [id, n] : v)
    {
        if (id >= tableRef.size () || !isSetting (id))
            continue;
        beginEdit (id);
        setParamNormalized (id, n);
        performEdit (id, n);
        endEdit (id);
    }
    presetTitle.clear ();
    currentKind = presets::Kind::None;
    currentPath.clear ();
    markDirty ();
    refreshEditor ();
    return true;
}

tresult PLUGIN_API ControllerBase::notify (IMessage* message)
{
    if (!message || !message->getMessageID ())
        return EditController::notify (message);
    const char* id = message->getMessageID ();
    if (std::strcmp (id, presets::kMsgState) == 0)
    {
        const void* data = nullptr;
        uint32 size = 0;
        if (!pendingSavePath.empty () && message->getAttributes ()->getBinary (presets::kAttrData, data, size) == kResultOk)
        {
            std::vector<char> component ((const char*)data, (const char*)data + size);
            MemoryStream own;
            getState (&own);
            std::vector<char> controllerState ((const char*)own.getData (), (const char*)own.getData () + own.getSize ());
            lastSaveOk = presets::write (pendingSavePath, presetClassId, component, controllerState, &pendingMeta);
            if (lastSaveOk && !pendingIsDefault)
            {
                currentKind = inFolder (pendingSavePath, presetFolder ()) ? presets::Kind::User : presets::Kind::File;
                currentPath = pendingSavePath;
                presetTitle = presets::nameOf (pendingSavePath);
                markDirty ();
                refreshEditor ();
            }
        }
        pendingSavePath.clear ();
        return kResultOk;
    }
    if (std::strcmp (id, kMsgSetLayout) == 0)
    {
        auto text = [&] (const char* attr) {
            const void* data = nullptr;
            uint32 size = 0;
            return message->getAttributes ()->getBinary (attr, data, size) == kResultOk && data ? std::string ((const char*)data, size)
                                                                                               : std::string ();
        };
        uiLayout = text ("text");
        uiLayoutName = text ("name");
        markDirty ();
        for (auto* e : editors)
            e->setLayout (uiLayout, uiLayoutName, true);
        return kResultOk;
    }
    if (std::strcmp (id, kMsgFindControl) == 0)
    {
        int64 param = -1;
        VSTGUI::CRect r;
        if (message->getAttributes ()->getInt ("id", param) == kResultOk && param >= 0 && !editors.empty () &&
            editors.front ()->findControl ((uint32_t)param, r))
        {
            const double v[4] = {r.left, r.top, r.right, r.bottom};
            message->getAttributes ()->setBinary ("rect", v, sizeof (v));
        }
        return kResultOk;
    }
    if (std::strcmp (id, presets::kMsgMenu) == 0)
    {
        std::string items;
        for (const auto& t : presets::menuTitles (presetMenu ()))
            items += t + "\n";
        message->getAttributes ()->setBinary (presets::kAttrItems, items.data (), (uint32)items.size ());
        return kResultOk;
    }
    if (std::strcmp (id, presets::kMsgSave) == 0 || std::strcmp (id, presets::kMsgLoad) == 0)
    {
        TChar buf[1024] = {0};
        if (message->getAttributes ()->getString (presets::kAttrPath, buf, sizeof (buf)) == kResultOk)
        {
            const std::string path = StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (buf)));
            if (std::strcmp (id, presets::kMsgSave) == 0)
                savePreset (path);
            else
                loadPreset (path);
        }
        return kResultOk;
    }
    return EditController::notify (message);
}

} // namespace pk

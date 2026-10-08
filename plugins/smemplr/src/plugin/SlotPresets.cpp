#include "SlotPresets.h"

#include "Controller.h"
#include "RackPresetIO.h"
#include "RackPresets.h"

#include "pluginkit/vst/Presets.h"

#include <filesystem>

namespace smemplr {

namespace fs = std::filesystem;
namespace presets = pk::presets;

namespace {

class SlotPresets : public pk::PresetSource
{
public:
    SlotPresets (Controller* c, int s) : ctl (c), slot (s) {}

    std::string presetName () override { return state ().title; }
    presets::Kind presetKind () override { return state ().kind; }
    std::string presetPath () override { return state ().path; }
    std::string presetFolder () override { return rackio::folderOf (type ()); }
    std::string& tagFilter () override { return state ().tagFilter; }

    std::vector<presets::MenuEntry> presetMenu (std::vector<presets::Item>* factoryOut, std::vector<presets::Item>* userOut) override
    {
        auto f = presets::factoryItems (factoryPresets ());
        auto u = presets::listUser (rackio::folderOf (type ()));
        auto& st = state ();
        if (!st.tagFilter.empty ())
        {
            std::vector<presets::Item> both = f;
            both.insert (both.end (), u.begin (), u.end ());
            if (!presets::hasTag (presets::allTags (both), st.tagFilter))
                st.tagFilter.clear (); // no preset has it any more
        }
        presets::MenuState ms;
        ms.kind = st.kind;
        ms.path = st.path;
        ms.tagFilter = st.tagFilter;
        ms.hasDefault = rackio::hasDefault (type ());
        auto menu = presets::buildMenu (f, u, ms);
        if (factoryOut)
            *factoryOut = std::move (f);
        if (userOut)
            *userOut = std::move (u);
        return menu;
    }

    const std::vector<presets::FactoryPreset>& factoryPresets () override { return rackio::factoryPresetsOf (type ()); }

    bool loadInit () override
    {
        if (!hostedPlugin (type ()))
            return false;
        ctl->applySlotValues (slot, pluginDefaults (type ()));
        setState (presets::Kind::Init, "", presets::kInitName);
        return true;
    }

    bool loadFactory (int index) override
    {
        const auto& f = factoryPresets ();
        if (index < 0 || index >= (int)f.size ())
            return false;
        ctl->applySlotValues (slot, pluginValuesWith (type (), f[(size_t)index].values));
        setState (presets::Kind::Factory, f[(size_t)index].path, f[(size_t)index].name);
        return true;
    }

    bool loadPreset (const std::string& path) override
    {
        std::vector<double> v;
        if (!rackio::readValues (type (), path, v))
            return false;
        ctl->applySlotValues (slot, v);
        if (path == rackio::defaultPathOf (type ()))
            setState (presets::Kind::Default, "", presets::kDefaultName);
        else
            setState (presets::inFolder (path, rackio::folderOf (type ())) ? presets::Kind::User : presets::Kind::File, path,
                      presets::nameOf (path));
        return true;
    }

    bool savePreset (const std::string& path) override
    {
        auto& st = state ();
        // Save keeps the preset's tags and category; a file elsewhere gets none
        presets::Meta m = st.kind == presets::Kind::User && path == st.path ? presets::readMeta (path) : presets::Meta {};
        m.name = presets::nameOf (path);
        return write (path, m);
    }

    std::string userPresetPath (const std::string& name, const std::string& category) override
    {
        const std::string folder = rackio::folderOf (type ());
        if (folder.empty ())
            return {};
        fs::path p (folder);
        if (!category.empty ())
            p /= category;
        p /= name + presets::kExtension;
        return p.string ();
    }

    bool saveUserPreset (const std::string& name, const std::string& category, const std::vector<std::string>& tags) override
    {
        if (!presets::validName (name) || (!category.empty () && !presets::validName (category, true)))
            return false;
        const std::string path = userPresetPath (name, category);
        if (path.empty ())
            return false;
        presets::Meta m;
        m.name = name;
        m.category = category;
        m.tags = tags;
        return write (path, m);
    }

    bool setPresetTags (const std::string& path, const std::vector<std::string>& tags) override
    {
        const auto* c = rackio::codecOf (type ());
        const HostedPlugin* hp = hostedPlugin (type ());
        if (!c || !hp)
            return false;
        presets::Meta m = presets::readMeta (path);
        m.tags = tags;
        m.plugin = hp->name;
        if (m.name.empty ())
            m.name = presets::nameOf (path);
        return presets::rewriteMeta (path, c->classId, m);
    }

    bool renamePreset (const std::string& path, const std::string& newName) override
    {
        const auto* c = rackio::codecOf (type ());
        const HostedPlugin* hp = hostedPlugin (type ());
        if (!c || !hp || !presets::validName (newName))
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
        m.plugin = hp->name;
        presets::rewriteMeta (to.string (), c->classId, m); // (the file name is the name: the metadata follows)
        if (state ().path == path)
            setState (state ().kind, to.string (), newName);
        return true;
    }

    bool deletePreset (const std::string& path) override
    {
        std::error_code ec;
        if (!fs::remove (path, ec))
            return false;
        if (state ().path == path)
            setState (presets::Kind::None, "", "");
        return true;
    }

    bool saveAsDefault () override
    {
        if (rackio::folderOf (type ()).empty ()) // (makes the folder, as the plug-in does)
            return false;
        presets::Meta m;
        m.name = presets::kDefaultName;
        return rackio::writeValues (type (), rackio::defaultPathOf (type ()), values (), m);
    }

    bool loadDefault () override { return rackio::hasDefault (type ()) ? loadPreset (rackio::defaultPathOf (type ())) : loadInit (); }

    bool resetDefault () override
    {
        std::error_code ec;
        const std::string p = rackio::defaultPathOf (type ());
        return !p.empty () && fs::remove (p, ec);
    }

private:
    int type () { return ctl->slotType (slot); }
    Controller::SlotPreset& state () { return ctl->slotPreset (slot); }
    void setState (presets::Kind k, const std::string& path, const std::string& title)
    {
        auto& st = state ();
        st.kind = k;
        st.path = path;
        st.title = title;
    }
    std::vector<double> values ()
    {
        return pluginValuesOf (slot, type (), [this] (uint32_t id) { return ctl->getParamNormalized (id); });
    }
    bool write (const std::string& path, const presets::Meta& m)
    {
        if (!rackio::writeValues (type (), path, values (), m))
            return false;
        setState (presets::inFolder (path, rackio::folderOf (type ())) ? presets::Kind::User : presets::Kind::File, path, presets::nameOf (path));
        return true;
    }

    Controller* ctl;
    int slot;
};

} // namespace

std::shared_ptr<pk::PresetSource> makeSlotPresets (Controller* controller, int slot)
{
    return std::make_shared<SlotPresets> (controller, slot);
}

} // namespace smemplr

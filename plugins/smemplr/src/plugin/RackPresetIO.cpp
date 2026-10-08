#include "RackPresetIO.h"

#include "Params.h"
#include "RackPresets.h"

#include "pluginkit/GentlrDefaults.h"
#include "pluginkit/vst/Presets.h"

#include <array>
#include <filesystem>
#include <memory>
#include <mutex>

namespace smemplr::rackio {

namespace fs = std::filesystem;

const hosted::Codec* codecOf (int type)
{
    switch (type)
    {
        case kFxPara: return &hosted::codecPara ();
        case kFxMultidyn: return &hosted::codecMultidyn ();
        case kFxSmacheratr: return &hosted::codecSmacheratr ();
        case kFxWidr: return &hosted::codecWidr ();
        case kFxWubr: return &hosted::codecWubr ();
        case kFxLevlr: return &hosted::codecLevlr ();
        case kFxGentlr: return &hosted::codecGentlr ();
        case kFxSmoothr: return &hosted::codecSmoothr ();
        default: return nullptr;
    }
}

std::string folderOf (int type)
{
    const HostedPlugin* hp = hostedPlugin (type);
    return hp ? pk::presets::userFolder (hp->name, hp->formerName) : std::string ();
}

std::string folderPathOf (int type)
{
    const HostedPlugin* hp = hostedPlugin (type);
    return hp ? (fs::path (pk::presets::suiteFolder ()) / hp->name).string () : std::string ();
}

std::string defaultPathOf (int type)
{
    const HostedPlugin* hp = hostedPlugin (type);
    return hp ? pk::presets::defaultPresetPath (hp->name) : std::string ();
}

bool hasDefault (int type)
{
    const std::string p = defaultPathOf (type);
    std::error_code ec;
    return !p.empty () && fs::is_regular_file (p, ec);
}

const std::vector<pk::presets::FactoryPreset>& factoryPresetsOf (int type)
{
    static std::mutex m;
    static std::array<std::unique_ptr<std::vector<pk::presets::FactoryPreset>>, kNumFxTypes> parsed;
    static const std::vector<pk::presets::FactoryPreset> none;
    const HostedPlugin* hp = hostedPlugin (type);
    if (!hp || !codecOf (type)) // (the codec registers the plug-in's factory files)
        return none;
    std::lock_guard<std::mutex> lock (m);
    auto& p = parsed[(size_t)type];
    if (!p)
        p = std::make_unique<std::vector<pk::presets::FactoryPreset>> (
            pk::presets::parseFactoryFiles (pk::presets::factoryFilesOf (hp->name), *hp->table));
    return *p;
}

bool readValues (int type, const std::string& path, std::vector<double>& values)
{
    const hosted::Codec* c = codecOf (type);
    std::error_code ec;
    if (!c || path.empty () || !fs::is_regular_file (path, ec))
        return false;
    std::vector<char> component, controller;
    if (!pk::presets::read (path, c->classId, component, controller))
        return false;
    return c->read (component, values);
}

bool writeValues (int type, const std::string& path, const std::vector<double>& values, const pk::presets::Meta& meta)
{
    const hosted::Codec* c = codecOf (type);
    const HostedPlugin* hp = hostedPlugin (type);
    if (!c || !hp || path.empty ())
        return false;
    std::vector<char> component;
    if (!c->write (values, component))
        return false;
    std::error_code ec;
    fs::create_directories (fs::path (path).parent_path (), ec);
    pk::presets::Meta m = meta;
    m.plugin = hp->name;
    if (m.name.empty ())
        m.name = pk::presets::nameOf (path);
    return pk::presets::write (path, c->classId, component, {}, &m);
}

std::vector<double> newSlotValues (int type, bool* appliedDefault)
{
    std::vector<double> saved;
    const bool has = hasDefault (type) && readValues (type, defaultPathOf (type), saved);
    if (appliedDefault)
        *appliedDefault = has;
    // (the switches from the folder the plug-in's processor reads them from, without making it)
    return smemplr::newSlotValues (type, has ? &saved : nullptr, pk::readGentlrDefaults (folderPathOf (type)));
}

} // namespace smemplr::rackio

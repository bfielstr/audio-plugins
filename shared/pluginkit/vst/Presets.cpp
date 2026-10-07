#include "Presets.h"

#include "pluginkit/GentlrDefaults.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace pk::presets {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace fs = std::filesystem;

namespace {
bool readAll (IBStream* s, TSize offset, TSize size, std::vector<char>& out)
{
    out.assign ((size_t)std::max<TSize> (0, size), 0);
    if (size <= 0)
        return true;
    if (s->seek (offset, IBStream::kIBSeekSet) != kResultOk)
        return false;
    int32 got = 0;
    return s->read (out.data (), (int32)size, &got) == kResultOk && got == size;
}

std::vector<FactoryFile>& factoryRegistry ()
{
    static std::vector<FactoryFile> files;
    return files;
}
} // namespace

bool write (const std::string& path, const FUID& classId, const std::vector<char>& component,
            const std::vector<char>& controller, const Meta* meta)
{
    IBStream* file = FileStream::open (path.c_str (), "wb");
    if (!file)
        return false;
    MemoryStream comp, ctrl;
    if (!component.empty ())
        comp.write ((void*)component.data (), (int32)component.size (), nullptr);
    if (!controller.empty ())
        ctrl.write ((void*)controller.data (), (int32)controller.size (), nullptr);
    comp.seek (0, IBStream::kIBSeekSet, nullptr);
    ctrl.seek (0, IBStream::kIBSeekSet, nullptr);
    const std::string xml = meta ? metaToXml (*meta) : std::string ();
    const bool ok = PresetFile::savePreset (file, classId, &comp, controller.empty () ? nullptr : &ctrl,
                                            meta ? xml.c_str () : nullptr, meta ? (int32)xml.size () : -1);
    file->release ();
    return ok;
}

bool read (const std::string& path, const FUID& classId, std::vector<char>& component, std::vector<char>& controller)
{
    IBStream* file = FileStream::open (path.c_str (), "rb");
    if (!file)
        return false;
    bool ok = false;
    {
        PresetFile pf (file);
        if (pf.readChunkList () && pf.getClassID () == classId)
        {
            if (const auto* e = pf.getEntry (kComponentState))
                ok = readAll (file, e->offset, e->size, component);
            controller.clear ();
            if (ok)
                if (const auto* e = pf.getEntry (kControllerState))
                    readAll (file, e->offset, e->size, controller);
        }
    }
    file->release ();
    return ok;
}

Meta readMeta (const std::string& path)
{
    Meta m;
    std::string xml;
    if (readMetaXml (path, xml))
        metaFromXml (xml, m);
    return m;
}

bool rewriteMeta (const std::string& path, const FUID& classId, const Meta& meta)
{
    std::vector<char> component, controller;
    if (!read (path, classId, component, controller))
        return false;
    // a new file beside it, then over it: a failed write leaves the preset as it was
    const std::string tmp = path + ".tmp";
    if (!write (tmp, classId, component, controller, &meta))
    {
        std::error_code ec;
        fs::remove (tmp, ec);
        return false;
    }
    std::error_code ec;
    fs::rename (tmp, path, ec);
    if (ec)
    {
        fs::remove (tmp, ec);
        return false;
    }
    return true;
}

bool registerFactory (const FactoryFile* files, int count)
{
    auto& reg = factoryRegistry ();
    for (int i = 0; i < count; ++i)
        reg.push_back (files[i]);
    return true;
}

const std::vector<FactoryFile>& factoryFiles () { return factoryRegistry (); }

bool applyDefault (AudioEffect& fx, const FUID& classId, const char* pluginName)
{
    if (!pluginName || !*pluginName)
        return false;
    const std::string path = defaultPresetPath (pluginName);
    std::error_code ec;
    if (path.empty () || !fs::is_regular_file (path, ec))
        return false;
    std::vector<char> component, controller;
    if (!read (path, classId, component, controller) || component.empty ())
        return false;
    MemoryStream state (component.data (), (TSize)component.size ());
    return fx.setState (&state) == kResultOk;
}

bool applyDefault (AudioEffect& fx, const FUID& classId, const char* pluginName, const GentlrIds& ids,
                   const std::function<void (uint32_t, double)>& set)
{
    const bool applied = applyDefault (fx, classId, pluginName);
    if (pluginName && *pluginName && set)
    {
        // (the folder the controller's presetFolder () is, read without making it)
        const std::string folder = (fs::path (suiteFolder ()) / pluginName).string ();
        for (const auto& [id, n] : gentlrDefaultValues (ids, readGentlrDefaults (folder)))
            set (id, n);
    }
    return applied;
}

bool handleProcessorMessage (AudioEffect& fx, IMessage* message)
{
    if (!message || !message->getMessageID ())
        return false;
    if (std::strcmp (message->getMessageID (), kMsgGetState) == 0)
    {
        MemoryStream state;
        if (fx.getState (&state) != kResultOk)
            return true;
        if (IMessage* reply = fx.allocateMessage ())
        {
            reply->setMessageID (kMsgState);
            reply->getAttributes ()->setBinary (kAttrData, state.getData (), (uint32)state.getSize ());
            fx.sendMessage (reply);
            reply->release ();
        }
        return true;
    }
    if (std::strcmp (message->getMessageID (), kMsgSetState) == 0)
    {
        const void* data = nullptr;
        uint32 size = 0;
        if (message->getAttributes ()->getBinary (kAttrData, data, size) == kResultOk && data && size > 0)
        {
            MemoryStream state (const_cast<void*> (data), (TSize)size);
            fx.setState (&state);
        }
        return true;
    }
    return false;
}

} // namespace pk::presets

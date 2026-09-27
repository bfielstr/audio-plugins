#include "Presets.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace pk::presets {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace fs = std::filesystem;

namespace {
const char* kExtension = ".vstpreset";

std::string envOr (const char* name, const char* fallback)
{
    const char* v = std::getenv (name);
    return v && *v ? v : fallback;
}

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
} // namespace

std::string userFolder (const char* pluginName)
{
    fs::path base;
#if defined(_WIN32)
    std::string home = envOr ("USERPROFILE", "");
    if (home.empty ())
        home = envOr ("HOMEDRIVE", "") + envOr ("HOMEPATH", "");
    base = fs::path (home) / "Documents" / "VST3 Presets";
#elif defined(__APPLE__)
    base = fs::path (envOr ("HOME", "")) / "Library" / "Audio" / "Presets";
#else
    base = fs::path (envOr ("HOME", "")) / ".vst3" / "presets";
#endif
    const fs::path folder = base / "bfielstr" / pluginName;
    std::error_code ec;
    fs::create_directories (folder, ec);
    return fs::is_directory (folder, ec) ? folder.string () : std::string ();
}

std::vector<Entry> list (const std::string& folder)
{
    std::vector<Entry> out;
    std::error_code ec;
    if (folder.empty () || !fs::is_directory (folder, ec))
        return out;
    for (const auto& e : fs::directory_iterator (folder, ec))
    {
        if (!e.is_regular_file (ec))
            continue;
        std::string ext = e.path ().extension ().string ();
        std::transform (ext.begin (), ext.end (), ext.begin (), [] (unsigned char c) { return (char)std::tolower (c); });
        if (ext == kExtension)
            out.push_back ({e.path ().stem ().string (), e.path ().string ()});
    }
    std::sort (out.begin (), out.end (), [] (const Entry& a, const Entry& b) { return a.name < b.name; });
    return out;
}

std::string nameOf (const std::string& path) { return fs::path (path).stem ().string (); }

std::string withExtension (const std::string& path)
{
    std::string ext = fs::path (path).extension ().string ();
    std::transform (ext.begin (), ext.end (), ext.begin (), [] (unsigned char c) { return (char)std::tolower (c); });
    return ext == kExtension ? path : path + kExtension;
}

bool write (const std::string& path, const FUID& classId, const std::vector<char>& component,
            const std::vector<char>& controller)
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
    const bool ok = PresetFile::savePreset (file, classId, &comp, controller.empty () ? nullptr : &ctrl);
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

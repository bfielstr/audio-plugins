#include "Controller.h"

#include "Cids.h"
#include "State.h"
#include "ui/Editor.h"

#include "smemplr/src/core/FxSlot.h"

#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace moistr {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
    const tresult r = pk::ControllerBase::initialize (context);
    if (r != kResultOk)
        return r;
    // (the saved default, when there is one, was applied; the recipe touches none of Menu > Defaults' parameters)
    if (!hasDefault ())
        for (const auto& [id, n] : newInstanceValues ())
            setParamNormalized (id, n);
    return kResultOk;
}

tresult PLUGIN_API Controller::terminate ()
{
    unwatchLatency ();
    if (shared)
    {
        shared->release ();
        shared = nullptr;
    }
    return pk::ControllerBase::terminate ();
}

tresult PLUGIN_API Controller::setComponentState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    State st;
    if (!readState (stream, st))
        return kResultFalse;
    for (uint32_t id = 0; id < kNumParams; ++id)
        setParamNormalized (id, st.norm[id]);
    user = st.user; // (the processor has them from its own state)
    setUserScene (st.scene);
    refreshEditor ();
    return kResultOk;
}

std::string Controller::gestureFolder () const
{
    const std::string base = presetFolder ();
    return base.empty () ? std::string () : (std::filesystem::path (base) / "Gestures").string ();
}

std::vector<Controller::GestureFileItem> Controller::gestureFiles () const
{
    namespace fs = std::filesystem;
    std::vector<GestureFileItem> out;
    std::error_code ec;
    const std::string folder = gestureFolder ();
    if (folder.empty () || !fs::is_directory (folder, ec))
        return out;
    for (const auto& e : fs::directory_iterator (folder, ec))
    {
        const fs::path& p = e.path ();
        std::string ext = p.extension ().string ();
        std::transform (ext.begin (), ext.end (), ext.begin (), [] (unsigned char c) { return (char)std::tolower (c); });
        if (ext == ".json" && e.is_regular_file (ec) && !p.filename ().string ().empty () && p.filename ().string ()[0] != '.')
            out.push_back ({p.stem ().string (), p.string ()});
    }
    std::sort (out.begin (), out.end (), [] (const GestureFileItem& a, const GestureFileItem& b) { return a.name < b.name; });
    return out;
}

bool Controller::loadUserGesture (int slot, const std::string& path, std::string& error)
{
    if (slot < 0 || slot >= kNumGestureSlots)
        return false;
    std::ifstream f (std::filesystem::path (path), std::ios::binary);
    if (!f)
    {
        error = "cannot open the file";
        return false;
    }
    std::stringstream text;
    text << f.rdbuf ();
    GestureData g;
    Gesture check;
    if (!parseGestureJson (text.str (), std::filesystem::path (path).stem ().string (), g, error))
        return false;
    if (!toGesture (g, check))
    {
        error = "not a curve moistr can play";
        return false;
    }
    user[(size_t)slot] = std::move (g);
    sendUserGesture (slot);
    setPlainFromUI (gestureId (slot, kGestureChoice), kUserGesture);
    markDirty ();
    refreshEditor ();
    return true;
}

void Controller::clearUserGesture (int slot)
{
    if (slot < 0 || slot >= kNumGestureSlots)
        return;
    user[(size_t)slot] = {};
    sendUserGesture (slot);
}

void Controller::sendUserGesture (int slot)
{
    if (auto msg = owned (allocateMessage ()))
    {
        msg->setMessageID (kGestureMessageId);
        msg->getAttributes ()->setInt (kGestureSlotAttr, slot);
        const std::string json = user[(size_t)slot].empty () ? std::string () : gestureJson (user[(size_t)slot]);
        msg->getAttributes ()->setBinary (kGestureJsonAttr, json.data (), (uint32)json.size ());
        sendMessage (msg);
    }
}

void Controller::setUserScene (SceneData d)
{
    userSceneFile = std::move (d);
    userSceneCurve.reset ();
    if (userSceneFile.empty ())
        return;
    auto s = std::make_unique<Scene> ();
    if (toScene (userSceneFile, *s) && s->count > 0)
        userSceneCurve = std::move (s);
}

bool Controller::loadUserScene (const std::string& path, std::string& error)
{
    std::ifstream f (std::filesystem::path (path), std::ios::binary);
    if (!f)
    {
        error = "cannot open the file";
        return false;
    }
    std::stringstream text;
    text << f.rdbuf ();
    SceneData d;
    if (!parseSceneJson (text.str (), std::filesystem::path (path).stem ().string (), d, error))
        return false;
    auto check = std::make_unique<Scene> ();
    if (!toScene (d, *check) || check->count == 0)
    {
        error = check->count == 0 ? "no lane has a target" : "not a gesture moistr can play";
        return false;
    }
    setUserScene (std::move (d));
    sendUserScene ();
    setPlainFromUI (kScene, kSceneUser);
    markDirty ();
    refreshEditor ();
    return true;
}

void Controller::clearUserScene ()
{
    setUserScene ({});
    sendUserScene ();
}

void Controller::sendUserScene ()
{
    if (auto msg = owned (allocateMessage ()))
    {
        msg->setMessageID (kSceneMessageId);
        const std::string json = userSceneFile.empty () ? std::string () : sceneJson (userSceneFile);
        msg->getAttributes ()->setBinary (kGestureJsonAttr, json.data (), (uint32)json.size ());
        sendMessage (msg);
    }
}

std::string Controller::makeGestureFolder () const
{
    const std::string folder = gestureFolder ();
    if (!folder.empty ())
    {
        std::error_code ec;
        std::filesystem::create_directories (folder, ec);
    }
    return folder;
}

void Controller::resetExtraState ()
{
    if (!userSceneFile.empty ())
        clearUserScene ();
    for (int g = 0; g < kNumGestureSlots; ++g)
        if (!user[(size_t)g].empty ())
            clearUserGesture (g);
}

namespace {
// A value in a LAB slot's block: shown and typed in the units of the slot's kind (as smemplr's rack slots).
class LabSlotParameter : public pk::TableParameter
{
public:
    LabSlotParameter (const pk::ParamTable& t, uint32_t id, Controller* c, int s, uint32_t j) : pk::TableParameter (t, id), ctl (c), slot (s), index (j) {}
    void toString (ParamValue n, String128 string) const override
    {
        const auto& t = smemplr::fxBlockTable (ctl->labKind (slot));
        if (index >= t.size ())
        {
            pk::TableParameter::toString (n, string);
            return;
        }
        Steinberg::Vst::StringConvert::convert (t.toText (index, t.toPlain (index, n)), string);
    }
    bool fromString (const TChar* string, ParamValue& n) const override
    {
        const auto& t = smemplr::fxBlockTable (ctl->labKind (slot));
        if (index >= t.size ())
            return pk::TableParameter::fromString (string, n);
        double v;
        if (!t.fromText (index, Steinberg::Vst::StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (string))), v))
            return false;
        n = t.toNormalized (index, v);
        return true;
    }

private:
    Controller* ctl;
    int slot;
    uint32_t index;
};
} // namespace

int Controller::labKind (int slot)
{
    const int t = (int)std::lround (plain (labSlotParam (slot, kLabType)));
    return t > 0 && t < smemplr::kNumFxTypes ? t : smemplr::kFxEmpty;
}

Parameter* Controller::makeParameter (uint32_t id)
{
    if (isLabSlotParam (id) && labFieldOf (id) >= kLabBlock)
        return new LabSlotParameter (tableRef, id, this, labSlotOf (id), labFieldOf (id) - kLabBlock);
    Parameter* p = pk::ControllerBase::makeParameter (id);
    if (isChainSpare (id))
        p->getInfo ().flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden; // (kept for later: not shown)
    return p;
}

IPlugView* PLUGIN_API Controller::createView (FIDString name)
{
    if (name && std::strcmp (name, ViewType::kEditor) == 0)
        return new Editor (this);
    return nullptr;
}

tresult PLUGIN_API Controller::notify (IMessage* message)
{
    if (message && std::strcmp (message->getMessageID (), kSharedMessageId) == 0)
    {
        int64 ptr = 0;
        if (message->getAttributes ()->getInt (kSharedAttr, ptr) == kResultOk && ptr != 0)
        {
            auto* m = reinterpret_cast<SharedMeters*> ((intptr_t)ptr);
            if (m != shared)
            {
                m->retain ();
                unwatchLatency ();
                if (shared)
                    shared->release ();
                shared = m;
                watchLatency (&shared->tailMeters.latency); // (the end saturator's moves with its Oversampling)
                watchLatency (&shared->meters.labLatency);  // (the LAB's with the kinds in its slots)
            }
        }
        return kResultOk;
    }
    return pk::ControllerBase::notify (message);
}

} // namespace moistr

#include "Controller.h"

#include "Cids.h"
#include "Session.h"
#include "State.h"
#include "ui/Editor.h"

#include "pluginterfaces/vst/ivstmessage.h"

#include <cstdio>
#include <cstring>

namespace probr {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
    labelText = kDefaultLabel;
    const tresult r = pk::ControllerBase::initialize (context);
    if (r != kResultOk)
        return r;
    // MIDI pitch bend arrives as these (IMidiMapping), one per channel
    for (int ch = 0; ch < 16; ++ch)
    {
        char name[32];
        std::snprintf (name, sizeof (name), "Pitch Bend %d", ch + 1);
        String128 title;
        for (int i = 0; i < 32; ++i)
            title[i] = (char16)name[i];
        parameters.addParameter (title, nullptr, 0, 0.5, ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kMidiPitchBend + ch);
    }
    return kResultOk;
}

tresult PLUGIN_API Controller::terminate ()
{
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
        if (id != kRecord) // (not saved: stays as it is)
            setParamNormalized (id, st.norm[id]);
    labelText = st.label;
    folderText = st.folder;
    refreshEditor ();
    return kResultOk;
}

void Controller::sendText ()
{
    if (IMessage* msg = allocateMessage ())
    {
        msg->setMessageID (kTextMessageId);
        msg->getAttributes ()->setBinary (kLabelAttr, labelText.data (), (uint32)labelText.size ());
        msg->getAttributes ()->setBinary (kFolderAttr, folderText.data (), (uint32)folderText.size ());
        sendMessage (msg);
        msg->release ();
    }
    markDirty ();
}

void Controller::setLabel (const std::string& l)
{
    if (l == labelText)
        return;
    labelText = l;
    sendText ();
}

void Controller::setFolder (const std::string& f)
{
    if (f == folderText)
        return;
    folderText = f;
    sendText ();
}

std::string Controller::folderShown () const { return folderText.empty () ? defaultFolder () : folderText; }

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
            auto* m = reinterpret_cast<Shared*> ((intptr_t)ptr);
            if (m != shared)
            {
                m->retain ();
                if (shared)
                    shared->release ();
                shared = m;
            }
        }
        return kResultOk;
    }
    return pk::ControllerBase::notify (message);
}

tresult PLUGIN_API Controller::getMidiControllerAssignment (int32 busIndex, int16 channel, CtrlNumber ctrl, ParamID& id)
{
    if (busIndex == 0 && ctrl == kPitchBend && channel >= 0 && channel < 16)
    {
        id = kMidiPitchBend + (ParamID)channel;
        return kResultTrue;
    }
    return kResultFalse;
}

} // namespace probr

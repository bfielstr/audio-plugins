#include "Controller.h"

#include "State.h"
#include "ui/Editor.h"

#include "pluginterfaces/vst/ivstmessage.h"

#include <cstring>

namespace detonatr {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API Controller::terminate ()
{
    if (bridge)
    {
        bridge->release ();
        bridge = nullptr;
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
    return kResultOk;
}

IPlugView* PLUGIN_API Controller::createView (FIDString name)
{
    if (name && std::strcmp (name, ViewType::kEditor) == 0)
        return new Editor (this);
    return nullptr;
}

tresult PLUGIN_API Controller::notify (IMessage* message)
{
    if (message && std::strcmp (message->getMessageID (), kBridgeMessageId) == 0)
    {
        int64 ptr = 0;
        if (message->getAttributes ()->getInt (kBridgeAttr, ptr) == kResultOk && ptr != 0)
        {
            auto* b = reinterpret_cast<Bridge*> ((intptr_t)ptr);
            if (b != bridge)
            {
                b->retain ();
                if (bridge)
                    bridge->release ();
                bridge = b;
            }
        }
        return kResultOk;
    }
    return pk::ControllerBase::notify (message);
}

} // namespace detonatr

#include "Controller.h"

#include "Cids.h"
#include "State.h"
#include "ui/Editor.h"

#include "pluginterfaces/vst/ivstmessage.h"

#include <cstring>

namespace stretchr {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API Controller::terminate ()
{
    if (session)
    {
        session->release ();
        session = nullptr;
    }
    return pk::ControllerBase::terminate ();
}

tresult PLUGIN_API Controller::setComponentState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    State st;
    if (!readState (stream, st, false))
        return kResultFalse;
    for (uint32_t id = 0; id < kNumParams; ++id)
        setParamNormalized (id, st.norm[id]);
    return kResultOk;
}

tresult PLUGIN_API Controller::setParamNormalized (ParamID tag, ParamValue value)
{
    const tresult r = pk::ControllerBase::setParamNormalized (tag, value);
    // Also hand the value straight to the render worker, so edits re-render even while the
    // host isn't processing audio.
    if (session && tag < kNumParams)
        session->setParam (tag, toPlain (tag, value));
    return r;
}

IPlugView* PLUGIN_API Controller::createView (FIDString name)
{
    if (name && std::strcmp (name, ViewType::kEditor) == 0)
        return new Editor (this);
    return nullptr;
}

tresult PLUGIN_API Controller::notify (IMessage* message)
{
    if (message && std::strcmp (message->getMessageID (), kSessionMessageId) == 0)
    {
        int64 ptr = 0;
        if (message->getAttributes ()->getInt (kSessionAttr, ptr) == kResultOk && ptr != 0)
        {
            auto* s = reinterpret_cast<Session*> ((intptr_t)ptr);
            if (s != session)
            {
                s->retain ();
                if (session)
                    session->release ();
                session = s;
            }
        }
        return kResultOk;
    }
    return pk::ControllerBase::notify (message);
}

} // namespace stretchr

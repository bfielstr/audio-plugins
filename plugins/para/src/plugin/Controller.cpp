#include "Controller.h"

#include "State.h"
#include "ui/Editor.h"

#include "pluginterfaces/vst/ivstmessage.h"

#include <cstring>

namespace para {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
    const tresult r = pk::ControllerBase::initialize (context);
    if (r != kResultOk)
        return r;
    parameters.addParameter (STR16 ("Pitch Bend"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden,
                             kMidiPitchBend);
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
    if (message && std::strcmp (message->getMessageID (), kMetersMessageId) == 0)
    {
        int64 ptr = 0;
        if (message->getAttributes ()->getInt (kMetersAttr, ptr) == kResultOk && ptr != 0)
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
            }
        }
        return kResultOk;
    }
    return pk::ControllerBase::notify (message);
}

tresult PLUGIN_API Controller::getMidiControllerAssignment (int32 busIndex, int16, CtrlNumber ctrl, ParamID& id)
{
    if (busIndex == 0 && ctrl == kPitchBend)
    {
        id = kMidiPitchBend;
        return kResultTrue;
    }
    return kResultFalse;
}

} // namespace para

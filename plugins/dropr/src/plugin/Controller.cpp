#include "Controller.h"

#include "Cids.h"
#include "State.h"
#include "ui/Editor.h"

#include "pluginterfaces/vst/ivstmessage.h"

#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

namespace dropr {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
// the Negative Ratio reads "1 : -x" (Params.h: negRatioText)
class NegRatioParameter : public pk::TableParameter
{
public:
    using pk::TableParameter::TableParameter;
    void toString (ParamValue n, String128 string) const override
    {
        Steinberg::Vst::StringConvert::convert (negRatioText (toPlain (n)), string);
    }
    bool fromString (const TChar* string, ParamValue& n) const override
    {
        std::string s = Steinberg::Vst::StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (string)));
        if (const auto colon = s.find (':'); colon != std::string::npos)
            s = s.substr (colon + 1);
        s.erase (std::remove (s.begin (), s.end (), '-'), s.end ());
        double v = kRatioInf;
        if (s.find ("inf") == std::string::npos)
        {
            char* end = nullptr;
            v = std::strtod (s.c_str (), &end);
            if (end == s.c_str ())
                return false;
        }
        n = toNormalized (v);
        return true;
    }
};
} // namespace

Parameter* Controller::makeParameter (uint32_t id)
{
    if (id == kNegRatio)
        return new NegRatioParameter (paramTable (), id);
    return pk::ControllerBase::makeParameter (id);
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
            }
        }
        return kResultOk;
    }
    return pk::ControllerBase::notify (message);
}

} // namespace dropr

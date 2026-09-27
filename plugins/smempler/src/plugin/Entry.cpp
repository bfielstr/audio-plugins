#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: processor and controller share memory (see Bridge.h).
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (smempler::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass, "Smempler",
            0, PlugType::kInstrumentSampler, FULL_VERSION_STR, kVstVersionString, smempler::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (smempler::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "Smempler Controller", 0, "", FULL_VERSION_STR, kVstVersionString, smempler::Controller::createInstance)

END_FACTORY

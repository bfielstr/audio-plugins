#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: processor and controller share memory (see Bridge.h).
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (smemplr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass, "smemplr",
            0, PlugType::kInstrumentSampler, FULL_VERSION_STR, kVstVersionString, smemplr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (smemplr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "smemplr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, smemplr::Controller::createInstance)

END_FACTORY

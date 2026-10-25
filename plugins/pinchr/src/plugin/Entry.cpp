#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the meters from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (pinchr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "pinchr", 0, PlugType::kFxDynamics, FULL_VERSION_STR, kVstVersionString,
            pinchr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (pinchr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "pinchr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, pinchr::Controller::createInstance)

END_FACTORY

#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the meters from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (moistr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "moistr", 0, PlugType::kFxFilter, FULL_VERSION_STR, kVstVersionString,
            moistr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (moistr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "moistr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, moistr::Controller::createInstance)

END_FACTORY

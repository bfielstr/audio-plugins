#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the levels from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (wubr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "wubr", 0, PlugType::kFxFilter, FULL_VERSION_STR, kVstVersionString, wubr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (wubr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "wubr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, wubr::Controller::createInstance)

END_FACTORY

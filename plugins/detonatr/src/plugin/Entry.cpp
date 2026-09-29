#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor shares the recordings and levels with the processor in memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (detonatr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "detonatr", 0, PlugType::kFxDistortion, FULL_VERSION_STR, kVstVersionString, detonatr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (detonatr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "detonatr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, detonatr::Controller::createInstance)

END_FACTORY

#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the levels from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (smatcheratr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "Smatcheratr", 0, PlugType::kFxDistortion, FULL_VERSION_STR, kVstVersionString,
            smatcheratr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (smatcheratr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "Smatcheratr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, smatcheratr::Controller::createInstance)

END_FACTORY

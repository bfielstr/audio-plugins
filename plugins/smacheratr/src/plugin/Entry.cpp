#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the levels from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (smacheratr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "Smacheratr", 0, PlugType::kFxDistortion, FULL_VERSION_STR, kVstVersionString,
            smacheratr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (smacheratr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "Smacheratr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, smacheratr::Controller::createInstance)

END_FACTORY

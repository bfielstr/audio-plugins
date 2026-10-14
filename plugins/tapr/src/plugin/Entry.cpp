#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the meters from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (tapr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "tapr", 0, PlugType::kFxDistortion, FULL_VERSION_STR, kVstVersionString,
            tapr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (tapr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "tapr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, tapr::Controller::createInstance)

END_FACTORY

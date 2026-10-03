#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the meters from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (orbitr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "orbitr", 0, PlugType::kFxSpatial, FULL_VERSION_STR, kVstVersionString,
            orbitr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (orbitr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "orbitr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, orbitr::Controller::createInstance)

END_FACTORY

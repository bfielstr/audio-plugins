#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads meters from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (multidyn::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "multidyn", 0, PlugType::kFxDynamics, FULL_VERSION_STR, kVstVersionString,
            multidyn::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (multidyn::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "multidyn Controller", 0, "", FULL_VERSION_STR, kVstVersionString, multidyn::Controller::createInstance)

END_FACTORY

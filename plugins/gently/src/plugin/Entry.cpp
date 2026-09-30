#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the analyser from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (gently::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "gently", 0, PlugType::kFxDynamics, FULL_VERSION_STR, kVstVersionString, gently::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (gently::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "gently Controller", 0, "", FULL_VERSION_STR, kVstVersionString, gently::Controller::createInstance)

END_FACTORY

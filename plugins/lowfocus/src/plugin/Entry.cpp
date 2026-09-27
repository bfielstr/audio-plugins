#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the spectrum from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (lowfocus::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "Lowfocus", 0, PlugType::kFxDynamics, FULL_VERSION_STR, kVstVersionString,
            lowfocus::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (lowfocus::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "Lowfocus Controller", 0, "", FULL_VERSION_STR, kVstVersionString, lowfocus::Controller::createInstance)

END_FACTORY

#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the spectrum from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (locus::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "Locus", 0, PlugType::kFxDynamics, FULL_VERSION_STR, kVstVersionString,
            locus::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (locus::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "Locus Controller", 0, "", FULL_VERSION_STR, kVstVersionString, locus::Controller::createInstance)

END_FACTORY

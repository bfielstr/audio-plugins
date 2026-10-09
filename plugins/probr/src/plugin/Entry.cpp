#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the probe's status from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (probr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "probr", 0, PlugType::kFxAnalyzer, FULL_VERSION_STR, kVstVersionString,
            probr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (probr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "probr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, probr::Controller::createInstance)

END_FACTORY

#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the meters from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (smoothr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "smoothr", 0, PlugType::kFxDynamics, FULL_VERSION_STR, kVstVersionString, smoothr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (smoothr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "smoothr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, smoothr::Controller::createInstance)

END_FACTORY

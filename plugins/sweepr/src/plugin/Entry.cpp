#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the meters from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (sweepr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "sweepr", 0, PlugType::kFxEQ, FULL_VERSION_STR, kVstVersionString,
            sweepr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (sweepr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "sweepr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, sweepr::Controller::createInstance)

END_FACTORY

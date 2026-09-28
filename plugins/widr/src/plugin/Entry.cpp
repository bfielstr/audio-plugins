#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the meters from the processor's memory, and the instances
// find each other in this module's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (widr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "Widr", 0, PlugType::kFxSpatial, FULL_VERSION_STR, kVstVersionString, widr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (widr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "Widr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, widr::Controller::createInstance)

END_FACTORY

#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the levels from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (perrera::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "Perrera", 0, PlugType::kFxFilter, FULL_VERSION_STR, kVstVersionString, perrera::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (perrera::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "Perrera Controller", 0, "", FULL_VERSION_STR, kVstVersionString, perrera::Controller::createInstance)

END_FACTORY

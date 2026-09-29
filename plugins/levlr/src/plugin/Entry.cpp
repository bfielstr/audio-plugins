#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the analyser from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (levlr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "levlr", 0, PlugType::kFxEQ, FULL_VERSION_STR, kVstVersionString, levlr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (levlr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "levlr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, levlr::Controller::createInstance)

END_FACTORY

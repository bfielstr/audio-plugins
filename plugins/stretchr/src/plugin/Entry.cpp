#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor edits the clip in the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (stretchr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
            "Stretchr", 0, PlugType::kFxPitchShift, FULL_VERSION_STR, kVstVersionString,
            stretchr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (stretchr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "Stretchr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, stretchr::Controller::createInstance)

END_FACTORY

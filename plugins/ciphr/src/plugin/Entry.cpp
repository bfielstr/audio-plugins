#include "Cids.h"
#include "Controller.h"
#include "Processor.h"
#include "Version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;

// Not kDistributable: the editor reads the meters from the processor's memory.
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (ciphr::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass, "ciphr", 0,
            PlugType::kInstrumentSynth, FULL_VERSION_STR, kVstVersionString, ciphr::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (ciphr::kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
            "ciphr Controller", 0, "", FULL_VERSION_STR, kVstVersionString, ciphr::Controller::createInstance)

END_FACTORY

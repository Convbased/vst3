#include "controller.hpp"
#include "plugin_ids.hpp"
#include "processor.hpp"
#include "version.hpp"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "Convbased Bridge"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(convbased::vst3::kProcessorUid), PClassInfo::kManyInstances,
           kVstAudioEffectClass, stringPluginName, Vst::kDistributable,
           convbased::vst3::kPluginCategory, FULL_VERSION_STR, kVstVersionString,
           convbased::vst3::Processor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(convbased::vst3::kControllerUid), PClassInfo::kManyInstances,
           kVstComponentControllerClass, stringPluginName " Controller", 0, "",
           FULL_VERSION_STR, kVstVersionString, convbased::vst3::Controller::createInstance)

END_FACTORY

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace convbased::vst3 {

enum ParameterIds : Steinberg::Vst::ParamID {
    kMixId = 100,
    kBypassId = 101,
};

inline const Steinberg::FUID kProcessorUid(0xB50E8E9A, 0x5E2B4F9A, 0x8F97C1D4,
                                           0xC87B2A61);
inline const Steinberg::FUID kControllerUid(0xDA76E463, 0xA20D4CCF, 0x9341B7F8,
                                            0x23D9E605);

inline constexpr auto kPluginCategory = "Fx|Vocals";

} // namespace convbased::vst3

#pragma once

#include "loopback_bridge.hpp"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <cstdint>
#include <vector>

namespace convbased::vst3 {

class Processor final : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numInputs,
        Steinberg::Vst::SpeakerArrangement* outputs,
        Steinberg::int32 numOutputs) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(
        Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

private:
    enum class OutputRoute : std::uint8_t {
        silent,
        original,
        converted,
    };

    void applyParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void prepareScratch();

    bridge::LoopbackBridge bridge_ {};
    std::vector<float> inputScratch_ {};
    std::vector<float> outputScratch_ {};
    std::array<float, bridge::LoopbackBridge::kMaxChannels> lastOutput_ {};
    std::array<float, bridge::LoopbackBridge::kMaxChannels> transitionStart_ {};
    Steinberg::Vst::ParamValue convertedLevel_ {1.0};
    std::uint32_t declickFrames_ {1};
    std::uint32_t transitionFramesRemaining_ {0};
    OutputRoute outputRoute_ {OutputRoute::silent};
    Steinberg::int32 channelCount_ {2};
    bool bypass_ {false};
};

} // namespace convbased::vst3

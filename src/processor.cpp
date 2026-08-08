#include "processor.hpp"

#include "audio_routing.hpp"
#include "bridge_protocol.hpp"
#include "plugin_ids.hpp"
#include "plugin_messages.hpp"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>

namespace convbased::vst3 {

Processor::Processor() {
    setControllerClass(kControllerUid);
}

Processor::~Processor() {
    bridge_.stop();
}

Steinberg::tresult PLUGIN_API Processor::initialize(Steinberg::FUnknown* context) {
    const auto result = AudioEffect::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    addAudioInput(STR16("Audio Input"), Steinberg::Vst::SpeakerArr::kStereo);
    addAudioOutput(STR16("Audio Output"), Steinberg::Vst::SpeakerArr::kStereo);
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numInputs,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOutputs) {
    if (numInputs != 1 || numOutputs != 1 || inputs[0] != outputs[0]) {
        return Steinberg::kResultFalse;
    }

    const auto channels = Steinberg::Vst::SpeakerArr::getChannelCount(inputs[0]);
    if (channels != 1 && channels != 2) {
        return Steinberg::kResultFalse;
    }

    const auto result = AudioEffect::setBusArrangements(inputs, numInputs, outputs, numOutputs);
    if (result == Steinberg::kResultTrue) {
        channelCount_ = channels;
        prepareScratch();
    }
    return result;
}

Steinberg::tresult PLUGIN_API Processor::setupProcessing(Steinberg::Vst::ProcessSetup& setup) {
    const auto result = AudioEffect::setupProcessing(setup);
    if (result == Steinberg::kResultTrue) {
        prepareScratch();
    }
    return result;
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
    if (state) {
        prepareScratch();
        const auto sampleRate = static_cast<std::uint32_t>(
            std::clamp(std::llround(processSetup.sampleRate), 8'000LL, 384'000LL));
        const bridge::BridgeConfig config {
            sampleRate,
            static_cast<std::uint32_t>(std::max<Steinberg::int32>(1,
                                                                  processSetup.maxSamplesPerBlock)),
            static_cast<std::uint16_t>(channelCount_),
            bridge::LoopbackBridge::portFromEnvironment(),
        };
        if (!bridge_.start(config)) {
            return Steinberg::kResultFalse;
        }
    } else {
        bridge_.stop();
    }

    const auto result = AudioEffect::setActive(state);
    if (result != Steinberg::kResultTrue && state) {
        bridge_.stop();
    }
    return result;
}

Steinberg::tresult PLUGIN_API Processor::setProcessing(Steinberg::TBool state) {
    return AudioEffect::setProcessing(state);
}

Steinberg::tresult PLUGIN_API Processor::canProcessSampleSize(
    Steinberg::int32 symbolicSampleSize) {
    return symbolicSampleSize == Steinberg::Vst::kSample32 ? Steinberg::kResultTrue
                                                           : Steinberg::kResultFalse;
}

Steinberg::uint32 PLUGIN_API Processor::getLatencySamples() {
    // The local plug-in path adds no fixed look-ahead or block delay. The variable
    // WebRTC/model delay cannot be represented accurately as VST delay compensation.
    return 0;
}

Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data) {
    applyParameterChanges(data.inputParameterChanges);

    if (data.numSamples <= 0) {
        return Steinberg::kResultTrue;
    }
    if (data.symbolicSampleSize != Steinberg::Vst::kSample32 || data.numInputs < 1 ||
        data.numOutputs < 1) {
        return Steinberg::kResultFalse;
    }

    const auto channels = std::min<Steinberg::int32>(
        channelCount_, std::min(data.inputs[0].numChannels, data.outputs[0].numChannels));
    if (channels <= 0) {
        return Steinberg::kResultFalse;
    }
    const auto sampleCount = static_cast<std::size_t>(data.numSamples) *
                             static_cast<std::size_t>(channels);
    if (sampleCount > inputScratch_.size() || sampleCount > outputScratch_.size()) {
        return Steinberg::kResultFalse;
    }

    for (Steinberg::int32 frame = 0; frame < data.numSamples; ++frame) {
        for (Steinberg::int32 channel = 0; channel < channels; ++channel) {
            const auto* input = data.inputs[0].channelBuffers32[channel];
            inputScratch_[static_cast<std::size_t>(frame) * channels + channel] =
                input == nullptr ? 0.0F : input[frame];
        }
    }

    static_cast<void>(bridge_.pushInput(
        std::span<const float>(inputScratch_.data(), sampleCount)));
    const auto hasConverted = bridge_.tryPopOutput(
        std::span<float>(outputScratch_.data(), sampleCount));
    const auto gains = selectRoutingGains(bypass_, hasConverted, convertedLevel_);
    const auto targetRoute = gains.original > 0.0F
                                 ? OutputRoute::original
                                 : (gains.converted > 0.0F ? OutputRoute::converted
                                                           : OutputRoute::silent);
    if (targetRoute != outputRoute_) {
        transitionStart_ = lastOutput_;
        if (outputRoute_ == OutputRoute::original && targetRoute != OutputRoute::original) {
            transitionStart_.fill(0.0F);
        }
        outputRoute_ = targetRoute;
        transitionFramesRemaining_ = declickFrames_;
    }

    bool hasNonSilentOutput = false;
    for (Steinberg::int32 frame = 0; frame < data.numSamples; ++frame) {
        const auto completedTransitionFrames =
            declickFrames_ - std::min(declickFrames_, transitionFramesRemaining_) + 1U;
        for (Steinberg::int32 channel = 0; channel < channels; ++channel) {
            auto* output = data.outputs[0].channelBuffers32[channel];
            if (output == nullptr) {
                continue;
            }
            const auto index = static_cast<std::size_t>(frame) * channels + channel;
            float target = 0.0F;
            if (gains.original > 0.0F) {
                target = inputScratch_[index];
            } else if (gains.converted > 0.0F) {
                target = outputScratch_[index] * gains.converted;
            }
            output[frame] = transitionFramesRemaining_ > 0
                                ? interpolateTransition(
                                      transitionStart_[static_cast<std::size_t>(channel)],
                                      target, completedTransitionFrames, declickFrames_)
                                : target;
            lastOutput_[static_cast<std::size_t>(channel)] = output[frame];
            hasNonSilentOutput = hasNonSilentOutput || output[frame] != 0.0F;
        }
        if (transitionFramesRemaining_ > 0) {
            --transitionFramesRemaining_;
        }
    }

    if (!hasNonSilentOutput) {
        data.outputs[0].silenceFlags = (Steinberg::uint64 {1} << channels) - 1;
    } else {
        data.outputs[0].silenceFlags = 0;
    }
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
    if (state == nullptr) {
        return Steinberg::kResultFalse;
    }
    Steinberg::IBStreamer streamer(state, kLittleEndian);
    Steinberg::int32 version = 0;
    float savedConvertedLevel = 1.0F;
    Steinberg::int32 savedBypass = 0;
    if (!streamer.readInt32(version) || version != 1 ||
        !streamer.readFloat(savedConvertedLevel) ||
        !streamer.readInt32(savedBypass)) {
        return Steinberg::kResultFalse;
    }
    convertedLevel_ = std::clamp(
        static_cast<Steinberg::Vst::ParamValue>(savedConvertedLevel), 0.0, 1.0);
    bypass_ = savedBypass != 0;
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
    if (state == nullptr) {
        return Steinberg::kResultFalse;
    }
    Steinberg::IBStreamer streamer(state, kLittleEndian);
    const auto ok = streamer.writeInt32(1) &&
                    streamer.writeFloat(static_cast<float>(convertedLevel_)) &&
                    streamer.writeInt32(bypass_ ? 1 : 0);
    return ok ? Steinberg::kResultTrue : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message) {
    if (message == nullptr) {
        return Steinberg::kInvalidArgument;
    }
    const auto* messageId = message->getMessageID();
    if (messageId == nullptr) {
        return AudioEffect::notify(message);
    }
    if (std::strcmp(messageId, messages::kClearApiKeyMessageId) == 0) {
        bridge_.clearApiKey();
        return Steinberg::kResultTrue;
    }
    if (std::strcmp(messageId, messages::kApiKeyMessageId) != 0) {
        return AudioEffect::notify(message);
    }

    const void* data = nullptr;
    Steinberg::uint32 size = 0;
    auto* attributes = message->getAttributes();
    if (attributes == nullptr ||
        attributes->getBinary(messages::kApiKeyAttribute, data, size) !=
            Steinberg::kResultTrue ||
        data == nullptr || size == 0 || size > bridge::protocol::kMaxApiKeyBytes) {
        return Steinberg::kInvalidArgument;
    }
    const auto* characters = static_cast<const char*>(data);
    if (std::find(characters, characters + size, '\0') != characters + size) {
        return Steinberg::kInvalidArgument;
    }
    return bridge_.setApiKey(std::string(characters, size)) ? Steinberg::kResultTrue
                                                            : Steinberg::kResultFalse;
}

void Processor::applyParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept {
    if (changes == nullptr) {
        return;
    }
    const auto parameterCount = changes->getParameterCount();
    for (Steinberg::int32 parameterIndex = 0; parameterIndex < parameterCount;
         ++parameterIndex) {
        auto* queue = changes->getParameterData(parameterIndex);
        if (queue == nullptr || queue->getPointCount() == 0) {
            continue;
        }
        Steinberg::int32 sampleOffset = 0;
        Steinberg::Vst::ParamValue value = 0;
        if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, value) !=
            Steinberg::kResultTrue) {
            continue;
        }
        if (queue->getParameterId() == kConvertedLevelId) {
            convertedLevel_ = std::clamp(value, 0.0, 1.0);
        } else if (queue->getParameterId() == kBypassId) {
            bypass_ = value > 0.5;
        }
    }
}

void Processor::prepareScratch() {
    const auto frames = std::max<Steinberg::int32>(1, processSetup.maxSamplesPerBlock);
    const auto samples = static_cast<std::size_t>(frames) *
                         static_cast<std::size_t>(std::max<Steinberg::int32>(1, channelCount_));
    inputScratch_.assign(samples, 0.0F);
    outputScratch_.assign(samples, 0.0F);
    const auto sampleRate = std::clamp(processSetup.sampleRate, 8'000.0, 384'000.0);
    declickFrames_ = static_cast<std::uint32_t>(
        std::max(1LL, std::llround(sampleRate * 0.003)));
    lastOutput_.fill(0.0F);
    transitionStart_.fill(0.0F);
    transitionFramesRemaining_ = 0;
    outputRoute_ = OutputRoute::silent;
}

} // namespace convbased::vst3

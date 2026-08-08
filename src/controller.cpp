#include "controller.hpp"

#include "bridge_protocol.hpp"
#include "plugin_ids.hpp"
#include "plugin_messages.hpp"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/platform/iplatformtextedit.h"
#include "vstgui/uidescription/icontroller.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace convbased::vst3 {
namespace {

constexpr std::int32_t kApplyApiKeyTag = 1'000;
constexpr std::int32_t kApiKeyInputTag = 1'001;
constexpr std::int32_t kApiKeyStatusTag = 1'002;
constexpr std::string_view kApiKeyControllerName = "ApiKeyController";

[[nodiscard]] std::string_view trimAsciiWhitespace(std::string_view value) noexcept {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    return value.substr(first, value.find_last_not_of(whitespace) - first + 1);
}

[[nodiscard]] bool isPrintableAscii(std::string_view value) noexcept {
    return std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 0x21U && byte <= 0x7eU;
    });
}

class ApiKeyUiController final : public VSTGUI::IController {
public:
    explicit ApiKeyUiController(Controller& owner) : owner_(owner) {}

    void valueChanged(VSTGUI::CControl* control) override {
        if (control == nullptr || control->getTag() != kApplyApiKeyTag ||
            control->getValueNormalized() <= 0.5F || apiKeyInput_ == nullptr) {
            return;
        }

        std::string apiKey;
        if (const auto platformTextEdit = apiKeyInput_->getPlatformTextEdit()) {
            apiKey = platformTextEdit->getText().data();
        } else {
            apiKey = apiKeyInput_->getText().data();
        }
        const auto applied = owner_.submitApiKey(apiKey);
        std::fill(apiKey.begin(), apiKey.end(), '\0');
        if (applied) {
            apiKeyInput_->setText("");
            setStatus("密钥已应用，正在连接实时变声服务。");
        } else {
            setStatus("密钥格式无效，或音频处理器暂不可用。");
        }
    }

    VSTGUI::CView* verifyView(
        VSTGUI::CView* view, const VSTGUI::UIAttributes&,
        const VSTGUI::IUIDescription*) override {
        auto* control = dynamic_cast<VSTGUI::CControl*>(view);
        if (control == nullptr) {
            return view;
        }
        if (control->getTag() == kApiKeyInputTag) {
            apiKeyInput_ = dynamic_cast<VSTGUI::CTextEdit*>(control);
            if (apiKeyInput_ != nullptr) {
                apiKeyInput_->setSecureStyle(true);
                apiKeyInput_->setPlaceholderString("粘贴 CONVBASED_API_KEY");
            }
        } else if (control->getTag() == kApiKeyStatusTag) {
            status_ = dynamic_cast<VSTGUI::CTextLabel*>(control);
            setStatus(owner_.apiKeyApplied()
                          ? "密钥已应用，正在连接实时变声服务。"
                          : "尚未应用密钥，转换音频当前为静音。");
        }
        return view;
    }

private:
    void setStatus(VSTGUI::UTF8StringPtr text) {
        if (status_ != nullptr) {
            status_->setText(text);
            status_->invalid();
        }
    }

    Controller& owner_;
    VSTGUI::CTextEdit* apiKeyInput_ {nullptr};
    VSTGUI::CTextLabel* status_ {nullptr};
};

} // namespace

Steinberg::tresult PLUGIN_API Controller::initialize(Steinberg::FUnknown* context) {
    const auto result = EditController::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    parameters.addParameter(STR16("Converted Level"), STR16("%"), 0, 1.0,
                            Steinberg::Vst::ParameterInfo::kCanAutomate |
                                Steinberg::Vst::ParameterInfo::kIsHidden,
                            kConvertedLevelId);
    parameters.addParameter(STR16("Bypass"), nullptr, 1, 0.0,
                            Steinberg::Vst::ParameterInfo::kCanAutomate |
                                Steinberg::Vst::ParameterInfo::kIsBypass |
                                Steinberg::Vst::ParameterInfo::kIsHidden,
                            kBypassId);
    return Steinberg::kResultTrue;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(const char* name) {
    if (name != nullptr &&
        Steinberg::FIDStringsEqual(name, Steinberg::Vst::ViewType::kEditor)) {
        return new VSTGUI::VST3Editor(this, "view", "editor.uidesc");
    }
    return nullptr;
}

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
    if (state == nullptr) {
        return Steinberg::kResultFalse;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);
    Steinberg::int32 version = 0;
    float convertedLevel = 1.0F;
    Steinberg::int32 bypass = 0;
    if (!streamer.readInt32(version) || version != 1 ||
        !streamer.readFloat(convertedLevel) ||
        !streamer.readInt32(bypass)) {
        return Steinberg::kResultFalse;
    }
    setParamNormalized(kConvertedLevelId,
                       std::clamp(static_cast<double>(convertedLevel), 0.0, 1.0));
    setParamNormalized(kBypassId, bypass == 0 ? 0.0 : 1.0);
    return Steinberg::kResultTrue;
}

VSTGUI::IController* Controller::createSubController(
    VSTGUI::UTF8StringPtr name, const VSTGUI::IUIDescription*, VSTGUI::VST3Editor*) {
    if (name != nullptr && kApiKeyControllerName == name) {
        return new ApiKeyUiController(*this);
    }
    return nullptr;
}

bool Controller::submitApiKey(std::string_view apiKey) {
    apiKey = trimAsciiWhitespace(apiKey);
    if (apiKey.empty() || apiKey.size() > bridge::protocol::kMaxApiKeyBytes ||
        !isPrintableAscii(apiKey)) {
        return false;
    }

    Steinberg::IPtr<Steinberg::Vst::IMessage> message =
        Steinberg::owned(allocateMessage());
    if (message == nullptr) {
        message = Steinberg::owned(new Steinberg::Vst::HostMessage());
    }
    message->setMessageID(messages::kApiKeyMessageId);
    auto* attributes = message->getAttributes();
    if (attributes == nullptr ||
        attributes->setBinary(messages::kApiKeyAttribute, apiKey.data(),
                              static_cast<Steinberg::uint32>(apiKey.size())) !=
            Steinberg::kResultTrue ||
        sendMessage(message) != Steinberg::kResultTrue) {
        return false;
    }

    apiKeyApplied_ = true;
    return true;
}

bool Controller::apiKeyApplied() const noexcept {
    return apiKeyApplied_;
}

} // namespace convbased::vst3

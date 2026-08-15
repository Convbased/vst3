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
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/platform/iplatformtextedit.h"
#include "vstgui/uidescription/icontroller.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace convbased::vst3 {
namespace {

constexpr std::int32_t kApplyApiKeyTag = 1'000;
constexpr std::int32_t kApiKeyInputTag = 1'001;
constexpr std::int32_t kApiKeyStatusTag = 1'002;
constexpr std::int32_t kClearApiKeyTag = 1'003;
constexpr std::int32_t kBalanceStatusTag = 1'004;
constexpr std::int32_t kProfileStatusTag = 1'005;
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

[[nodiscard]] bool isValidApiKey(std::string_view value) noexcept {
    return !value.empty() && value.size() <= bridge::protocol::kMaxApiKeyBytes &&
           isPrintableAscii(value);
}

class ApiKeyUiController final : public VSTGUI::IController {
public:
    explicit ApiKeyUiController(Controller& owner)
        : owner_(owner),
          timer_(VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
              [this](VSTGUI::CVSTGUITimer*) { refreshAccountStatus(); }, 500)) {}

    void valueChanged(VSTGUI::CControl* control) override {
        if (control == nullptr || control->getValueNormalized() <= 0.5F) {
            return;
        }
        if (control->getTag() == kClearApiKeyTag) {
            const auto cleared = owner_.clearApiKey();
            if (apiKeyInput_ != nullptr) {
                apiKeyInput_->setText("");
            }
            setStatus(cleared ? "会话密钥已清除，实时连接已断开。"
                              : "会话密钥已清除，但音频处理器暂不可用。");
            refreshAccountStatus();
            return;
        }
        if (control->getTag() != kApplyApiKeyTag || apiKeyInput_ == nullptr) {
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
            setStatus("会话密钥已应用，正在连接实时变声服务。");
            refreshAccountStatus();
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
                          ? "会话密钥已应用，正在连接实时变声服务。"
                          : "尚未输入会话密钥，转换音频当前为静音。");
        } else if (control->getTag() == kBalanceStatusTag) {
            balanceStatus_ = dynamic_cast<VSTGUI::CTextLabel*>(control);
            refreshAccountStatus();
        } else if (control->getTag() == kProfileStatusTag) {
            profileStatus_ = dynamic_cast<VSTGUI::CTextLabel*>(control);
            refreshAccountStatus();
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

    static void setLabel(VSTGUI::CTextLabel* label, const std::string& text) {
        if (label != nullptr) {
            label->setText(text.c_str());
            label->invalid();
        }
    }

    void refreshAccountStatus() {
        if (!owner_.apiKeyApplied()) {
            setLabel(balanceStatus_, "可用余额：--");
            setLabel(profileStatus_, "设备配置：未连接");
            return;
        }

        static_cast<void>(owner_.requestAccountStatus());
        const auto& account = owner_.accountStatus();
        if (!account.accountChecked) {
            setLabel(balanceStatus_, "可用余额：查询中…");
            setLabel(profileStatus_, "设备配置：等待桥接服务验证");
            return;
        }
        if (!account.authenticated) {
            setStatus("API Key 验证未通过，请检查密钥、网络与桥接服务日志。");
            setLabel(balanceStatus_, "可用余额：验证未通过");
            setLabel(profileStatus_, "设备配置：无法读取");
            return;
        }

        setStatus("API Key 验证成功，设备配置已同步。");
        if (!account.balanceChecked) {
            setLabel(balanceStatus_, "可用余额：查询中…");
        } else if (!account.balanceAvailable) {
            setLabel(balanceStatus_, "可用余额：当前服务未开放 API Key 查询");
        } else {
            std::array<char, 96> text {};
            std::snprintf(text.data(), text.size(), "可用余额：%.2f", account.balance);
            setLabel(balanceStatus_, text.data());
        }

        std::string profile = "设备配置：";
        profile += account.label.empty() ? "未命名设备" : account.label;
        if (!account.realtimeEnabled) {
            profile += "，实时变声已停用";
        } else if (!account.modelConfigured) {
            profile += "，尚未绑定模型";
        } else {
            profile += "，实时模型已配置";
        }
        setLabel(profileStatus_, profile);
    }

    Controller& owner_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    VSTGUI::CTextEdit* apiKeyInput_ {nullptr};
    VSTGUI::CTextLabel* status_ {nullptr};
    VSTGUI::CTextLabel* balanceStatus_ {nullptr};
    VSTGUI::CTextLabel* profileStatus_ {nullptr};
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

Steinberg::tresult PLUGIN_API Controller::notify(Steinberg::Vst::IMessage* message) {
    if (message == nullptr) {
        return Steinberg::kInvalidArgument;
    }
    const auto* messageId = message->getMessageID();
    if (messageId == nullptr ||
        std::strcmp(messageId, messages::kAccountStatusMessageId) != 0) {
        return EditController::notify(message);
    }

    const void* data = nullptr;
    Steinberg::uint32 size = 0;
    Steinberg::int64 generation = 0;
    auto* attributes = message->getAttributes();
    if (attributes == nullptr ||
        attributes->getBinary(messages::kAccountStatusAttribute, data, size) !=
            Steinberg::kResultTrue ||
        attributes->getInt(messages::kAccountStatusGenerationAttribute, generation) !=
            Steinberg::kResultTrue ||
        data == nullptr || size == 0) {
        return Steinberg::kInvalidArgument;
    }

    bridge::protocol::AccountStatus status;
    const auto bytes = std::span<const std::byte>(static_cast<const std::byte*>(data), size);
    if (!bridge::protocol::decodeAccountStatus(bytes, status)) {
        return Steinberg::kInvalidArgument;
    }
    if (generation < 0) {
        return Steinberg::kInvalidArgument;
    }
    const auto incomingGeneration = static_cast<std::uint64_t>(generation);
    if (incomingGeneration >= accountStatusGeneration_) {
        accountStatus_ = std::move(status);
        accountStatusGeneration_ = incomingGeneration;
    }
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
    if (!isValidApiKey(apiKey)) {
        return false;
    }

    if (!sendApiKey(apiKey)) {
        return false;
    }

    apiKeyApplied_ = true;
    accountStatus_ = {};
    return true;
}

bool Controller::clearApiKey() {
    Steinberg::IPtr<Steinberg::Vst::IMessage> message =
        Steinberg::owned(allocateMessage());
    if (message == nullptr) {
        message = Steinberg::owned(new Steinberg::Vst::HostMessage());
    }
    message->setMessageID(messages::kClearApiKeyMessageId);
    const auto processorCleared = sendMessage(message) == Steinberg::kResultTrue;
    apiKeyApplied_ = false;
    accountStatus_ = {};
    return processorCleared;
}

bool Controller::sendApiKey(std::string_view apiKey) {
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

    return true;
}

bool Controller::apiKeyApplied() const noexcept {
    return apiKeyApplied_;
}

bool Controller::requestAccountStatus() {
    Steinberg::IPtr<Steinberg::Vst::IMessage> message =
        Steinberg::owned(allocateMessage());
    if (message == nullptr) {
        message = Steinberg::owned(new Steinberg::Vst::HostMessage());
    }
    message->setMessageID(messages::kAccountStatusRequestMessageId);
    return sendMessage(message) == Steinberg::kResultTrue;
}

const bridge::protocol::AccountStatus& Controller::accountStatus() const noexcept {
    return accountStatus_;
}

} // namespace convbased::vst3

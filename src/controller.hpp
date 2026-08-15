#pragma once

#include "bridge_protocol.hpp"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <string_view>

namespace convbased::vst3 {

class Controller final : public Steinberg::Vst::EditController,
                         public VSTGUI::VST3EditorDelegate {
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(const char* name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(
        Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    VSTGUI::IController* createSubController(
        VSTGUI::UTF8StringPtr name, const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;

    [[nodiscard]] bool submitApiKey(std::string_view apiKey);
    [[nodiscard]] bool clearApiKey();
    [[nodiscard]] bool apiKeyApplied() const noexcept;
    [[nodiscard]] bool requestAccountStatus();
    [[nodiscard]] const bridge::protocol::AccountStatus& accountStatus() const noexcept;

private:
    [[nodiscard]] bool sendApiKey(std::string_view apiKey);

    bool apiKeyApplied_ {false};
    bridge::protocol::AccountStatus accountStatus_ {};
    std::uint64_t accountStatusGeneration_ {0};
};

} // namespace convbased::vst3

#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/cgradientview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/vstguibase.h"
#include "vstgui/lib/vstguiinit.h"
#include "vstgui/uidescription/icontroller.h"
#include "vstgui/uidescription/uicontentprovider.h"
#include "vstgui/uidescription/uidescription.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Windows.h>

namespace {

constexpr std::int32_t kConvertedLevelTag = 100;
constexpr std::int32_t kBypassTag = 101;
constexpr std::int32_t kApplyApiKeyTag = 1'000;
constexpr std::int32_t kApiKeyInputTag = 1'001;
constexpr std::int32_t kApiKeyStatusTag = 1'002;
constexpr std::int32_t kClearApiKeyTag = 1'003;
constexpr std::int32_t kBalanceStatusTag = 1'004;
constexpr std::int32_t kProfileStatusTag = 1'005;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class PassiveController final : public VSTGUI::IController {
public:
    void valueChanged(VSTGUI::CControl*) override {}
};

class RootController final : public VSTGUI::IController {
public:
    void valueChanged(VSTGUI::CControl*) override {}

    VSTGUI::IController* createSubController(
        VSTGUI::UTF8StringPtr name, const VSTGUI::IUIDescription*) override {
        if (name != nullptr && std::string_view(name) == "ApiKeyController") {
            return new PassiveController();
        }
        return nullptr;
    }
};

struct EditorControls final {
    std::uint32_t convertedLevelCount {0};
    std::uint32_t panelSurfaceCount {0};
    bool panelSurfacesRounded {true};
    bool bypass {false};
    bool applyApiKey {false};
    bool applyApiKeyBranded {false};
    bool apiKeyInput {false};
    bool apiKeyInputImmediate {false};
    bool apiKeyInputRounded {false};
    bool apiKeyInputSecure {false};
    bool apiKeyStatus {false};
    bool clearApiKey {false};
    bool clearApiKeyStyled {false};
    bool balanceStatus {false};
    bool profileStatus {false};
};

void inspectView(VSTGUI::CView* view, EditorControls& controls) {
    if (const auto* surface = dynamic_cast<VSTGUI::CGradientView*>(view);
        surface != nullptr && surface->getWidth() >= 500.) {
        ++controls.panelSurfaceCount;
        controls.panelSurfacesRounded =
            controls.panelSurfacesRounded && surface->getRoundRectRadius() >= 8.;
    }

    if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
        switch (control->getTag()) {
            case kConvertedLevelTag:
                ++controls.convertedLevelCount;
                break;
            case kBypassTag:
                controls.bypass = true;
                break;
            case kApplyApiKeyTag:
                if (const auto* button = dynamic_cast<VSTGUI::CTextButton*>(control)) {
                    controls.applyApiKey = true;
                    controls.applyApiKeyBranded =
                        button->getGradient() != nullptr &&
                        button->getGradientHighlighted() != nullptr &&
                        button->getRoundRadius() >= 8. &&
                        button->getTextColor() == VSTGUI::CColor(255, 255, 255, 255);
                }
                break;
            case kApiKeyInputTag:
                controls.apiKeyInput = true;
                if (const auto* edit = dynamic_cast<VSTGUI::CTextEdit*>(control)) {
                    controls.apiKeyInputImmediate = edit->getImmediateTextChange();
                    controls.apiKeyInputRounded = edit->getRoundRectRadius() >= 8.;
                    controls.apiKeyInputSecure = edit->getSecureStyle();
                }
                break;
            case kApiKeyStatusTag:
                controls.apiKeyStatus = true;
                break;
            case kClearApiKeyTag:
                if (const auto* button = dynamic_cast<VSTGUI::CTextButton*>(control)) {
                    controls.clearApiKey = true;
                    controls.clearApiKeyStyled =
                        button->getGradient() != nullptr &&
                        button->getGradientHighlighted() != nullptr &&
                        button->getRoundRadius() >= 8.;
                }
                break;
            case kBalanceStatusTag:
                controls.balanceStatus = dynamic_cast<VSTGUI::CTextLabel*>(control) != nullptr;
                break;
            case kProfileStatusTag:
                controls.profileStatus = dynamic_cast<VSTGUI::CTextLabel*>(control) != nullptr;
                break;
            default:
                break;
        }
    }

    if (auto* container = view->asViewContainer()) {
        for (std::uint32_t index = 0; index < container->getNbViews(); ++index) {
            inspectView(container->getView(index), controls);
        }
    }
}

void testEditorDescription() {
    std::ifstream input(CONVBASED_EDITOR_UIDESC, std::ios::binary);
    require(input.good(), "editor.uidesc could not be opened");
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    require(!contents.empty(), "editor.uidesc is empty");

    VSTGUI::MemoryContentProvider provider(
        contents.data(), static_cast<std::uint32_t>(contents.size()));
    VSTGUI::UIDescription description(&provider);
    require(description.parse(), "editor.uidesc did not parse");

    RootController controller;
    const auto view = VSTGUI::owned(description.createView("view", &controller));
    require(view != nullptr, "editor.uidesc did not create its view template");
    require(view->getWidth() == 560. && view->getHeight() == 344.,
            "editor must preserve its fixed host-compatible size");

    VSTGUI::CColor color;
    require(description.getColor("Canvas", color) &&
                color == VSTGUI::CColor(29, 31, 33, 255),
            "editor canvas does not match the Convbased Web background token");
    require(description.getColor("Panel", color) &&
                color == VSTGUI::CColor(44, 46, 48, 255),
            "editor panels do not match the Convbased Web surface token");
    require(description.getColor("Border", color) &&
                color == VSTGUI::CColor(68, 70, 72, 255),
            "editor borders do not match the Convbased Web border token");
    require(description.getColor("Accent", color) &&
                color == VSTGUI::CColor(255, 102, 0, 255),
            "editor accent does not match the Convbased brand token");

    EditorControls controls;
    inspectView(view, controls);
    require(controls.convertedLevelCount == 0,
            "editor must hide converted-level debugging controls");
    require(!controls.bypass, "editor must hide the Bypass debugging control");
    require(controls.panelSurfaceCount == 2 && controls.panelSurfacesRounded,
            "editor must use two rounded Convbased surface panels");
    require(controls.applyApiKey, "editor is missing the Apply key button");
    require(controls.applyApiKeyBranded,
            "Apply key button must use the branded rounded treatment");
    require(controls.apiKeyInput, "editor is missing the API key field");
    require(controls.apiKeyInputImmediate,
            "API key field must synchronize text before Apply key is pressed");
    require(controls.apiKeyInputRounded,
            "API key field must match the rounded Convbased form treatment");
    require(controls.apiKeyInputSecure, "API key field is not masked");
    require(controls.apiKeyStatus, "editor is missing API key status text");
    require(controls.clearApiKey, "editor is missing the clear-key action");
    require(controls.clearApiKeyStyled,
            "clear-key action must not fall back to the native button style");
    require(controls.balanceStatus, "editor is missing the account-balance status");
    require(controls.profileStatus, "editor is missing the device-profile status");
    require(contents.find("title=\"Convbased\"") != std::string::npos &&
                contents.find("title=\"实时变声\"") != std::string::npos,
            "editor title is not localized to Chinese");
    require(contents.find("title=\"连接\"") != std::string::npos,
            "editor actions are not localized to Chinese");
    require(contents.find("不写入磁盘或工程文件") != std::string::npos,
            "editor does not explain the session-only credential boundary");
    require(contents.find("#36c58d") == std::string::npos,
            "editor still contains the legacy non-brand green accent");
    require(contents.find("Audio remains dry") == std::string::npos,
            "editor must not promise an automatic original-audio fallback");
}

} // namespace

int main() {
    VSTGUI::init(::GetModuleHandleW(nullptr));
    try {
        testEditorDescription();
        VSTGUI::exit();
        std::cout << "Editor description test passed.\n";
        return 0;
    } catch (const std::exception& error) {
        VSTGUI::exit();
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}

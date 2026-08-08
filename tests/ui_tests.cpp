#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/ctextedit.h"
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
    bool bypass {false};
    bool applyApiKey {false};
    bool apiKeyInput {false};
    bool apiKeyInputImmediate {false};
    bool apiKeyInputSecure {false};
    bool apiKeyStatus {false};
    bool clearApiKey {false};
};

void inspectView(VSTGUI::CView* view, EditorControls& controls) {
    if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
        switch (control->getTag()) {
            case kConvertedLevelTag:
                ++controls.convertedLevelCount;
                break;
            case kBypassTag:
                controls.bypass = true;
                break;
            case kApplyApiKeyTag:
                controls.applyApiKey = dynamic_cast<VSTGUI::CTextButton*>(control) != nullptr;
                break;
            case kApiKeyInputTag:
                controls.apiKeyInput = true;
                if (const auto* edit = dynamic_cast<VSTGUI::CTextEdit*>(control)) {
                    controls.apiKeyInputImmediate = edit->getImmediateTextChange();
                    controls.apiKeyInputSecure = edit->getSecureStyle();
                }
                break;
            case kApiKeyStatusTag:
                controls.apiKeyStatus = true;
                break;
            case kClearApiKeyTag:
                controls.clearApiKey = dynamic_cast<VSTGUI::CTextButton*>(control) != nullptr;
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

    EditorControls controls;
    inspectView(view, controls);
    require(controls.convertedLevelCount == 0,
            "editor must hide converted-level debugging controls");
    require(!controls.bypass, "editor must hide the Bypass debugging control");
    require(controls.applyApiKey, "editor is missing the Apply key button");
    require(controls.apiKeyInput, "editor is missing the API key field");
    require(controls.apiKeyInputImmediate,
            "API key field must synchronize text before Apply key is pressed");
    require(controls.apiKeyInputSecure, "API key field is not masked");
    require(controls.apiKeyStatus, "editor is missing API key status text");
    require(controls.clearApiKey, "editor is missing the clear-key action");
    require(contents.find("Convbased 实时变声") != std::string::npos,
            "editor title is not localized to Chinese");
    require(contents.find("保存并连接") != std::string::npos,
            "editor actions are not localized to Chinese");
    require(contents.find("Windows 加密保存在本机") != std::string::npos,
            "editor does not explain the encrypted local cache");
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

#include "api_key_cache.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryCache final {
public:
    TemporaryCache()
        : path(std::filesystem::temp_directory_path() /
               (L"convbased-vst-cache-test-" + std::to_wstring(::GetCurrentProcessId()) +
                L".bin")) {
        static_cast<void>(convbased::credentials::deleteApiKeyFrom(path));
    }

    ~TemporaryCache() {
        static_cast<void>(convbased::credentials::deleteApiKeyFrom(path));
    }

    std::filesystem::path path;
};

void testEncryptedRoundTripAndRemoval() {
    TemporaryCache cache;
    constexpr std::string_view firstKey = "sk_local-cache-test-123";
    constexpr std::string_view replacementKey = "sk_replacement-test-456";

    require(convbased::credentials::saveApiKeyTo(firstKey, cache.path),
            "could not save the API key cache");
    const auto loaded = convbased::credentials::loadApiKeyFrom(cache.path);
    require(loaded.has_value() && *loaded == firstKey,
            "the API key cache did not round trip");

    std::ifstream input(cache.path, std::ios::binary);
    const std::string stored((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    input.close();
    require(stored.find(firstKey) == std::string::npos,
            "the API key cache contains plaintext credentials");

    require(convbased::credentials::saveApiKeyTo(replacementKey, cache.path),
            "could not replace the API key cache");
    const auto replaced = convbased::credentials::loadApiKeyFrom(cache.path);
    require(replaced.has_value() && *replaced == replacementKey,
            "the API key cache did not replace its previous value");

    require(convbased::credentials::deleteApiKeyFrom(cache.path),
            "could not remove the API key cache");
    require(!convbased::credentials::loadApiKeyFrom(cache.path).has_value(),
            "the API key cache remained readable after removal");
}

} // namespace

int main() {
    try {
        testEncryptedRoundTripAndRemoval();
        std::cout << "Credential cache tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}

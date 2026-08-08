#include "api_key_cache.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <WinCrypt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace convbased::credentials {
namespace {

constexpr std::array<char, 4> kCacheMagic {'C', 'B', 'K', '1'};
constexpr std::array<std::byte, 25> kEntropy {
    std::byte {'C'}, std::byte {'o'}, std::byte {'n'}, std::byte {'v'},
    std::byte {'b'}, std::byte {'a'}, std::byte {'s'}, std::byte {'e'},
    std::byte {'d'}, std::byte {'V'}, std::byte {'S'}, std::byte {'T'},
    std::byte {'3'}, std::byte {'.'}, std::byte {'A'}, std::byte {'p'},
    std::byte {'i'}, std::byte {'K'}, std::byte {'e'}, std::byte {'y'},
    std::byte {'.'}, std::byte {'v'}, std::byte {'1'}, std::byte {0x43},
    std::byte {0x42}};
constexpr std::size_t kMaxEncryptedBytes = 64U * 1024U;

[[nodiscard]] DATA_BLOB entropyBlob() noexcept {
    return {static_cast<DWORD>(kEntropy.size()),
            reinterpret_cast<BYTE*>(const_cast<std::byte*>(kEntropy.data()))};
}

[[nodiscard]] bool replaceFile(const std::filesystem::path& temporary,
                               const std::filesystem::path& destination) noexcept {
    return ::MoveFileExW(temporary.c_str(), destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

} // namespace

std::filesystem::path defaultApiKeyCachePath() {
    PWSTR rawPath = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &rawPath)) ||
        rawPath == nullptr) {
        return {};
    }
    std::filesystem::path path(rawPath);
    ::CoTaskMemFree(rawPath);
    return path / L"Convbased" / L"VST3" / L"api-key.bin";
}

bool saveApiKey(std::string_view apiKey) {
    return saveApiKeyTo(apiKey, defaultApiKeyCachePath());
}

std::optional<std::string> loadApiKey() {
    return loadApiKeyFrom(defaultApiKeyCachePath());
}

bool deleteApiKey() {
    return deleteApiKeyFrom(defaultApiKeyCachePath());
}

bool saveApiKeyTo(std::string_view apiKey, const std::filesystem::path& path) {
    if (apiKey.empty() || path.empty() || apiKey.size() > MAXDWORD) {
        return false;
    }

    DATA_BLOB plain {
        static_cast<DWORD>(apiKey.size()),
        reinterpret_cast<BYTE*>(const_cast<char*>(apiKey.data())),
    };
    auto entropy = entropyBlob();
    DATA_BLOB encrypted {};
    if (::CryptProtectData(&plain, L"Convbased VST3 API Key", &entropy, nullptr, nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN, &encrypted) == FALSE) {
        return false;
    }

    bool saved = false;
    const auto temporary = std::filesystem::path(path).concat(L".tmp");
    try {
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(kCacheMagic.data(), static_cast<std::streamsize>(kCacheMagic.size()));
        output.write(reinterpret_cast<const char*>(encrypted.pbData),
                     static_cast<std::streamsize>(encrypted.cbData));
        output.close();
        saved = output.good() && replaceFile(temporary, path);
    } catch (const std::filesystem::filesystem_error&) {
        saved = false;
    }

    if (!saved) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    ::LocalFree(encrypted.pbData);
    return saved;
}

std::optional<std::string> loadApiKeyFrom(const std::filesystem::path& path) {
    if (path.empty()) {
        return std::nullopt;
    }

    std::vector<char> bytes;
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input.good()) {
            return std::nullopt;
        }
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    } catch (const std::ios_base::failure&) {
        return std::nullopt;
    }
    if (bytes.size() <= kCacheMagic.size() ||
        bytes.size() > kCacheMagic.size() + kMaxEncryptedBytes ||
        !std::equal(kCacheMagic.begin(), kCacheMagic.end(), bytes.begin())) {
        return std::nullopt;
    }

    DATA_BLOB encrypted {
        static_cast<DWORD>(bytes.size() - kCacheMagic.size()),
        reinterpret_cast<BYTE*>(bytes.data() + kCacheMagic.size()),
    };
    auto entropy = entropyBlob();
    DATA_BLOB plain {};
    if (::CryptUnprotectData(&encrypted, nullptr, &entropy, nullptr, nullptr,
                             CRYPTPROTECT_UI_FORBIDDEN, &plain) == FALSE ||
        plain.pbData == nullptr || plain.cbData == 0) {
        return std::nullopt;
    }

    std::string apiKey(reinterpret_cast<const char*>(plain.pbData), plain.cbData);
    ::SecureZeroMemory(plain.pbData, plain.cbData);
    ::LocalFree(plain.pbData);
    return apiKey;
}

bool deleteApiKeyFrom(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    static_cast<void>(std::filesystem::remove(path, error));
    return !error;
}

} // namespace convbased::credentials

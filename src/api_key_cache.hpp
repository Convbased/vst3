#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace convbased::credentials {

[[nodiscard]] std::filesystem::path defaultApiKeyCachePath();
[[nodiscard]] bool saveApiKey(std::string_view apiKey);
[[nodiscard]] std::optional<std::string> loadApiKey();
[[nodiscard]] bool deleteApiKey();

[[nodiscard]] bool saveApiKeyTo(std::string_view apiKey,
                                const std::filesystem::path& path);
[[nodiscard]] std::optional<std::string> loadApiKeyFrom(
    const std::filesystem::path& path);
[[nodiscard]] bool deleteApiKeyFrom(const std::filesystem::path& path);

} // namespace convbased::credentials

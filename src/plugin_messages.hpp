#pragma once

namespace convbased::vst3::messages {

inline constexpr char kApiKeyMessageId[] = "Convbased.ApiKey";
inline constexpr char kApiKeyAttribute[] = "value";
inline constexpr char kClearApiKeyMessageId[] = "Convbased.ApiKey.Clear";
inline constexpr char kAccountStatusRequestMessageId[] = "Convbased.AccountStatus.Request";
inline constexpr char kAccountStatusMessageId[] = "Convbased.AccountStatus";
inline constexpr char kAccountStatusAttribute[] = "value";
inline constexpr char kAccountStatusGenerationAttribute[] = "generation";

} // namespace convbased::vst3::messages

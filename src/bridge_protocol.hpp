#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace convbased::bridge::protocol {

inline constexpr std::array<std::byte, 4> kMagic {
    std::byte {'C'}, std::byte {'B'}, std::byte {'V'}, std::byte {'3'}};
inline constexpr std::uint16_t kVersion = 2;
inline constexpr std::size_t kHeaderBytes = 16;
inline constexpr std::size_t kHelloBytes = 12;
inline constexpr std::size_t kAccountStatusFixedBytes = 16;
inline constexpr std::size_t kMaxApiKeyBytes = 512;
inline constexpr std::size_t kMaxAccountTextBytes = 512;
inline constexpr std::uint32_t kMaxPayloadBytes = 256U * 1024U;

enum class MessageType : std::uint16_t {
    hello = 1,
    audioInput = 2,
    audioOutput = 3,
    status = 4,
    authenticate = 5,
    accountStatus = 6,
};

enum class AccountStatusFlag : std::uint16_t {
    accountChecked = 1U << 0U,
    authenticated = 1U << 1U,
    balanceChecked = 1U << 2U,
    balanceAvailable = 1U << 3U,
    realtimeEnabled = 1U << 4U,
    modelConfigured = 1U << 5U,
};

struct FrameHeader final {
    MessageType type {MessageType::status};
    std::uint32_t payloadBytes {0};
    std::uint32_t sequence {0};
};

struct Hello final {
    std::uint32_t sampleRate {48'000};
    std::uint16_t channels {2};
    std::uint32_t maxBlockFrames {0};
};

struct AccountStatus final {
    bool accountChecked {false};
    bool authenticated {false};
    bool balanceChecked {false};
    bool balanceAvailable {false};
    bool realtimeEnabled {false};
    bool modelConfigured {false};
    double balance {0.0};
    std::string label {};
    std::string modelId {};
};

[[nodiscard]] std::array<std::byte, kHeaderBytes> encodeHeader(const FrameHeader& header) noexcept;
[[nodiscard]] bool decodeHeader(std::span<const std::byte> bytes, FrameHeader& header) noexcept;
[[nodiscard]] std::array<std::byte, kHelloBytes> encodeHello(const Hello& hello) noexcept;
[[nodiscard]] bool decodeHello(std::span<const std::byte> bytes, Hello& hello) noexcept;
[[nodiscard]] std::vector<std::byte> encodeAccountStatus(const AccountStatus& status);
[[nodiscard]] bool decodeAccountStatus(std::span<const std::byte> bytes,
                                       AccountStatus& status);

} // namespace convbased::bridge::protocol

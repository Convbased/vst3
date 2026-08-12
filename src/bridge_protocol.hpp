#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace convbased::bridge::protocol {

inline constexpr std::array<std::byte, 4> kMagic {
    std::byte {'C'}, std::byte {'B'}, std::byte {'V'}, std::byte {'3'}};
inline constexpr std::uint16_t kVersion = 2;
inline constexpr std::size_t kHeaderBytes = 16;
inline constexpr std::size_t kHelloBytes = 12;
inline constexpr std::size_t kMaxApiKeyBytes = 512;
inline constexpr std::uint32_t kMaxPayloadBytes = 256U * 1024U;

enum class MessageType : std::uint16_t {
    hello = 1,
    audioInput = 2,
    audioOutput = 3,
    status = 4,
    authenticate = 5,
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

[[nodiscard]] std::array<std::byte, kHeaderBytes> encodeHeader(const FrameHeader& header) noexcept;
[[nodiscard]] bool decodeHeader(std::span<const std::byte> bytes, FrameHeader& header) noexcept;
[[nodiscard]] std::array<std::byte, kHelloBytes> encodeHello(const Hello& hello) noexcept;
[[nodiscard]] bool decodeHello(std::span<const std::byte> bytes, Hello& hello) noexcept;

} // namespace convbased::bridge::protocol

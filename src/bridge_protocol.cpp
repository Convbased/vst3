#include "bridge_protocol.hpp"

#include <algorithm>

namespace convbased::bridge::protocol {
namespace {

void writeU16(std::span<std::byte> destination, std::uint16_t value) noexcept {
    destination[0] = static_cast<std::byte>(value & 0xffU);
    destination[1] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void writeU32(std::span<std::byte> destination, std::uint32_t value) noexcept {
    destination[0] = static_cast<std::byte>(value & 0xffU);
    destination[1] = static_cast<std::byte>((value >> 8U) & 0xffU);
    destination[2] = static_cast<std::byte>((value >> 16U) & 0xffU);
    destination[3] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

[[nodiscard]] std::uint16_t readU16(std::span<const std::byte> source) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(source[0])) |
           static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(source[1]) << 8U);
}

[[nodiscard]] std::uint32_t readU32(std::span<const std::byte> source) noexcept {
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(source[0])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(source[1])) << 8U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(source[2])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(source[3])) << 24U);
}

[[nodiscard]] bool validMessageType(std::uint16_t raw) noexcept {
    return raw >= static_cast<std::uint16_t>(MessageType::hello) &&
           raw <= static_cast<std::uint16_t>(MessageType::authenticate);
}

} // namespace

std::array<std::byte, kHeaderBytes> encodeHeader(const FrameHeader& header) noexcept {
    std::array<std::byte, kHeaderBytes> bytes {};
    std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
    writeU16(std::span<std::byte>(bytes).subspan(4, 2), kVersion);
    writeU16(std::span<std::byte>(bytes).subspan(6, 2), static_cast<std::uint16_t>(header.type));
    writeU32(std::span<std::byte>(bytes).subspan(8, 4), header.payloadBytes);
    writeU32(std::span<std::byte>(bytes).subspan(12, 4), header.sequence);
    return bytes;
}

bool decodeHeader(std::span<const std::byte> bytes, FrameHeader& header) noexcept {
    if (bytes.size() < kHeaderBytes || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        return false;
    }

    const auto version = readU16(bytes.subspan(4, 2));
    const auto rawType = readU16(bytes.subspan(6, 2));
    const auto payloadBytes = readU32(bytes.subspan(8, 4));
    if (version != kVersion || !validMessageType(rawType) || payloadBytes > kMaxPayloadBytes) {
        return false;
    }

    header.type = static_cast<MessageType>(rawType);
    header.payloadBytes = payloadBytes;
    header.sequence = readU32(bytes.subspan(12, 4));
    return true;
}

std::array<std::byte, kHelloBytes> encodeHello(const Hello& hello) noexcept {
    std::array<std::byte, kHelloBytes> bytes {};
    writeU32(std::span<std::byte>(bytes).subspan(0, 4), hello.sampleRate);
    writeU16(std::span<std::byte>(bytes).subspan(4, 2), hello.channels);
    writeU16(std::span<std::byte>(bytes).subspan(6, 2), 0);
    writeU32(std::span<std::byte>(bytes).subspan(8, 4), hello.maxBlockFrames);
    return bytes;
}

bool decodeHello(std::span<const std::byte> bytes, Hello& hello) noexcept {
    if (bytes.size() != kHelloBytes) {
        return false;
    }

    hello.sampleRate = readU32(bytes.subspan(0, 4));
    hello.channels = readU16(bytes.subspan(4, 2));
    hello.maxBlockFrames = readU32(bytes.subspan(8, 4));
    return hello.sampleRate >= 8'000 && hello.sampleRate <= 384'000 &&
           hello.channels >= 1 && hello.channels <= 2 && hello.maxBlockFrames > 0;
}

} // namespace convbased::bridge::protocol

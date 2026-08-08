#include "bridge_protocol.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

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

void writeU64(std::span<std::byte> destination, std::uint64_t value) noexcept {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        destination[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
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

[[nodiscard]] std::uint64_t readU64(std::span<const std::byte> source) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(source[index]))
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] constexpr std::uint16_t flagValue(AccountStatusFlag flag) noexcept {
    return static_cast<std::uint16_t>(flag);
}

[[nodiscard]] bool validMessageType(std::uint16_t raw) noexcept {
    return raw >= static_cast<std::uint16_t>(MessageType::hello) &&
           raw <= static_cast<std::uint16_t>(MessageType::accountStatus);
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

std::vector<std::byte> encodeAccountStatus(const AccountStatus& status) {
    if (status.label.size() > kMaxAccountTextBytes ||
        status.modelId.size() > kMaxAccountTextBytes ||
        status.label.size() > std::numeric_limits<std::uint16_t>::max() ||
        status.modelId.size() > std::numeric_limits<std::uint16_t>::max() ||
        (status.balanceAvailable && !std::isfinite(status.balance))) {
        return {};
    }

    std::uint16_t flags = 0;
    const auto setFlag = [&flags](AccountStatusFlag flag, bool enabled) {
        if (enabled) {
            flags = static_cast<std::uint16_t>(flags | flagValue(flag));
        }
    };
    setFlag(AccountStatusFlag::accountChecked, status.accountChecked);
    setFlag(AccountStatusFlag::authenticated, status.authenticated);
    setFlag(AccountStatusFlag::balanceChecked, status.balanceChecked);
    setFlag(AccountStatusFlag::balanceAvailable, status.balanceAvailable);
    setFlag(AccountStatusFlag::realtimeEnabled, status.realtimeEnabled);
    setFlag(AccountStatusFlag::modelConfigured, status.modelConfigured);

    std::vector<std::byte> bytes(kAccountStatusFixedBytes + status.label.size() +
                                 status.modelId.size());
    writeU16(std::span<std::byte>(bytes).subspan(0, 2), 1);
    writeU16(std::span<std::byte>(bytes).subspan(2, 2), flags);
    writeU64(std::span<std::byte>(bytes).subspan(4, 8),
             std::bit_cast<std::uint64_t>(status.balance));
    writeU16(std::span<std::byte>(bytes).subspan(12, 2),
             static_cast<std::uint16_t>(status.label.size()));
    writeU16(std::span<std::byte>(bytes).subspan(14, 2),
             static_cast<std::uint16_t>(status.modelId.size()));
    std::memcpy(bytes.data() + kAccountStatusFixedBytes, status.label.data(),
                status.label.size());
    std::memcpy(bytes.data() + kAccountStatusFixedBytes + status.label.size(),
                status.modelId.data(), status.modelId.size());
    return bytes;
}

bool decodeAccountStatus(std::span<const std::byte> bytes, AccountStatus& status) {
    if (bytes.size() < kAccountStatusFixedBytes || readU16(bytes.subspan(0, 2)) != 1) {
        return false;
    }
    const auto flags = readU16(bytes.subspan(2, 2));
    const auto knownFlags = flagValue(AccountStatusFlag::accountChecked) |
                            flagValue(AccountStatusFlag::authenticated) |
                            flagValue(AccountStatusFlag::balanceChecked) |
                            flagValue(AccountStatusFlag::balanceAvailable) |
                            flagValue(AccountStatusFlag::realtimeEnabled) |
                            flagValue(AccountStatusFlag::modelConfigured);
    const auto labelBytes = readU16(bytes.subspan(12, 2));
    const auto modelBytes = readU16(bytes.subspan(14, 2));
    if ((flags & static_cast<std::uint16_t>(~knownFlags)) != 0 ||
        labelBytes > kMaxAccountTextBytes || modelBytes > kMaxAccountTextBytes ||
        bytes.size() != kAccountStatusFixedBytes + labelBytes + modelBytes) {
        return false;
    }

    AccountStatus decoded;
    const auto hasFlag = [flags](AccountStatusFlag flag) {
        return (flags & flagValue(flag)) != 0;
    };
    decoded.accountChecked = hasFlag(AccountStatusFlag::accountChecked);
    decoded.authenticated = hasFlag(AccountStatusFlag::authenticated);
    decoded.balanceChecked = hasFlag(AccountStatusFlag::balanceChecked);
    decoded.balanceAvailable = hasFlag(AccountStatusFlag::balanceAvailable);
    decoded.realtimeEnabled = hasFlag(AccountStatusFlag::realtimeEnabled);
    decoded.modelConfigured = hasFlag(AccountStatusFlag::modelConfigured);
    decoded.balance = std::bit_cast<double>(readU64(bytes.subspan(4, 8)));
    if (decoded.balanceAvailable && !std::isfinite(decoded.balance)) {
        return false;
    }
    decoded.label.assign(
        reinterpret_cast<const char*>(bytes.data() + kAccountStatusFixedBytes),
        labelBytes);
    decoded.modelId.assign(
        reinterpret_cast<const char*>(bytes.data() + kAccountStatusFixedBytes + labelBytes),
        modelBytes);
    status = std::move(decoded);
    return true;
}

} // namespace convbased::bridge::protocol

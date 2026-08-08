#include "loopback_bridge.hpp"

#include "bridge_protocol.hpp"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace convbased::bridge {
namespace {

using namespace std::chrono_literals;
using protocol::FrameHeader;
using protocol::MessageType;

[[nodiscard]] SOCKET connectToBridgeService(std::uint16_t port) noexcept {
    const auto socketHandle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    const DWORD receiveTimeoutMs = 2;
    const DWORD sendTimeoutMs = 100;
    const BOOL noDelay = TRUE;
    ::setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&receiveTimeoutMs), sizeof(receiveTimeoutMs));
    ::setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char*>(&sendTimeoutMs), sizeof(sendTimeoutMs));
    ::setsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
        SOCKET_ERROR) {
        ::closesocket(socketHandle);
        return INVALID_SOCKET;
    }
    return socketHandle;
}

[[nodiscard]] bool sendAll(SOCKET socketHandle, std::span<const std::byte> bytes) noexcept {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto remaining = bytes.size() - sent;
        const auto request = static_cast<int>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const auto result = ::send(socketHandle,
                                   reinterpret_cast<const char*>(bytes.data() + sent), request, 0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

[[nodiscard]] bool sendFrame(SOCKET socketHandle, MessageType type, std::uint32_t sequence,
                             std::span<const std::byte> payload) noexcept {
    if (payload.size() > protocol::kMaxPayloadBytes) {
        return false;
    }
    const auto header = protocol::encodeHeader(
        {type, static_cast<std::uint32_t>(payload.size()), sequence});
    return sendAll(socketHandle, header) && sendAll(socketHandle, payload);
}

[[nodiscard]] bool receiveFrames(
    SOCKET socketHandle, std::vector<std::byte>& receiveBuffer,
    SpscFloatRingBuffer<LoopbackBridge::kRingCapacitySamples>& output,
    std::atomic<std::uint64_t>& droppedOutputSamples, std::uint16_t channels) noexcept {
    std::array<std::byte, 32U * 1024U> chunk {};
    const auto received = ::recv(socketHandle, reinterpret_cast<char*>(chunk.data()),
                                 static_cast<int>(chunk.size()), 0);
    if (received == 0) {
        return false;
    }
    if (received == SOCKET_ERROR) {
        const auto error = ::WSAGetLastError();
        return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
    }

    receiveBuffer.insert(receiveBuffer.end(), chunk.begin(), chunk.begin() + received);
    std::size_t consumed = 0;
    while (receiveBuffer.size() - consumed >= protocol::kHeaderBytes) {
        FrameHeader header {};
        const auto headerBytes = std::span<const std::byte>(receiveBuffer).subspan(consumed);
        if (!protocol::decodeHeader(headerBytes, header)) {
            return false;
        }
        const auto frameBytes = protocol::kHeaderBytes + header.payloadBytes;
        if (receiveBuffer.size() - consumed < frameBytes) {
            break;
        }

        const auto payload = headerBytes.subspan(protocol::kHeaderBytes, header.payloadBytes);
        if (header.type == MessageType::audioOutput) {
            if ((payload.size() % sizeof(float)) != 0) {
                return false;
            }
            const auto sampleCount = payload.size() / sizeof(float);
            if (sampleCount == 0 || (sampleCount % channels) != 0 ||
                sampleCount > LoopbackBridge::kMaxNetworkChunkFrames *
                                  LoopbackBridge::kMaxChannels) {
                return false;
            }
            std::array<float, LoopbackBridge::kMaxNetworkChunkFrames *
                                  LoopbackBridge::kMaxChannels>
                samples {};
            std::memcpy(samples.data(), payload.data(), payload.size());
            if (!output.push(std::span<const float>(samples.data(), sampleCount))) {
                droppedOutputSamples.fetch_add(sampleCount, std::memory_order_relaxed);
            }
        }
        consumed += frameBytes;
    }

    if (consumed > 0) {
        receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() +
                                                       static_cast<std::ptrdiff_t>(consumed));
    }
    return receiveBuffer.size() <= protocol::kMaxPayloadBytes + protocol::kHeaderBytes;
}

void waitBeforeReconnect(const std::atomic<bool>& running) noexcept {
    for (int attempt = 0; attempt < 25 && running.load(std::memory_order_acquire); ++attempt) {
        std::this_thread::sleep_for(10ms);
    }
}

} // namespace

LoopbackBridge::~LoopbackBridge() {
    stop();
    std::scoped_lock lock(credentialMutex_);
    std::fill(apiKey_.begin(), apiKey_.end(), '\0');
    apiKey_.clear();
}

bool LoopbackBridge::start(const BridgeConfig& config) {
    stop();
    if (config.sampleRate < 8'000 || config.sampleRate > 384'000 || config.maxBlockFrames == 0 ||
        config.channels == 0 || config.channels > kMaxChannels || config.port == 0) {
        state_.store(BridgeState::faulted, std::memory_order_release);
        return false;
    }

    config_ = config;
    input_.reset();
    output_.reset();
    droppedInputSamples_.store(0, std::memory_order_relaxed);
    droppedOutputSamples_.store(0, std::memory_order_relaxed);
    outputPrimed_.store(false, std::memory_order_relaxed);
    state_.store(BridgeState::connecting, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    try {
        worker_ = std::thread(&LoopbackBridge::run, this);
    } catch (...) {
        running_.store(false, std::memory_order_release);
        state_.store(BridgeState::faulted, std::memory_order_release);
        return false;
    }
    return true;
}

void LoopbackBridge::stop() noexcept {
    running_.store(false, std::memory_order_release);
    credentialChanged_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    state_.store(BridgeState::stopped, std::memory_order_release);
}

bool LoopbackBridge::setApiKey(std::string apiKey) {
    if (apiKey.empty() || apiKey.size() > protocol::kMaxApiKeyBytes ||
        apiKey.find('\0') != std::string::npos) {
        return false;
    }

    {
        std::scoped_lock lock(credentialMutex_);
        std::fill(apiKey_.begin(), apiKey_.end(), '\0');
        apiKey_ = std::move(apiKey);
        credentialGeneration_.fetch_add(1, std::memory_order_release);
    }
    credentialChanged_.notify_all();
    return true;
}

void LoopbackBridge::clearApiKey() noexcept {
    {
        std::scoped_lock lock(credentialMutex_);
        std::fill(apiKey_.begin(), apiKey_.end(), '\0');
        apiKey_.clear();
        credentialGeneration_.fetch_add(1, std::memory_order_release);
    }
    input_.discardAvailable();
    discardOutput();
    credentialChanged_.notify_all();
}

bool LoopbackBridge::pushInput(std::span<const float> interleaved) noexcept {
    if (state() != BridgeState::connected) {
        return false;
    }
    if (!input_.push(interleaved)) {
        droppedInputSamples_.fetch_add(interleaved.size(), std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool LoopbackBridge::tryPopOutput(std::span<float> interleaved) noexcept {
    if (state() != BridgeState::connected) {
        outputPrimed_.store(false, std::memory_order_relaxed);
        output_.discardAvailable();
        return false;
    }
    const auto availableSamples = output_.availableToRead();
    const auto queueLimit = std::max(maxOutputQueuedSamples(), interleaved.size());
    if (availableSamples > queueLimit) {
        auto staleSamples = availableSamples - queueLimit;
        staleSamples -= staleSamples % config_.channels;
        const auto discarded = output_.discardOldest(staleSamples);
        droppedOutputSamples_.fetch_add(discarded, std::memory_order_relaxed);
        outputPrimed_.store(false, std::memory_order_relaxed);
        return false;
    }
    if (!outputPrimed_.load(std::memory_order_relaxed)) {
        const auto primeThreshold = std::max(outputPrimeSamples(), interleaved.size());
        if (availableSamples < primeThreshold) {
            return false;
        }
        outputPrimed_.store(true, std::memory_order_relaxed);
    }
    if (!output_.pop(interleaved)) {
        outputPrimed_.store(false, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void LoopbackBridge::discardOutput() noexcept {
    outputPrimed_.store(false, std::memory_order_relaxed);
    output_.discardAvailable();
}

BridgeState LoopbackBridge::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

std::uint64_t LoopbackBridge::droppedInputSamples() const noexcept {
    return droppedInputSamples_.load(std::memory_order_relaxed);
}

std::uint64_t LoopbackBridge::droppedOutputSamples() const noexcept {
    return droppedOutputSamples_.load(std::memory_order_relaxed);
}

std::size_t LoopbackBridge::queuedOutputSamples() const noexcept {
    return output_.availableToRead();
}

std::size_t LoopbackBridge::maxInputQueuedSamples() const noexcept {
    const auto frames = std::max<std::size_t>(
        1, static_cast<std::size_t>(config_.sampleRate) * kMaxInputQueuedMilliseconds /
               1'000U);
    return frames * config_.channels;
}

std::size_t LoopbackBridge::maxOutputQueuedSamples() const noexcept {
    const auto frames = std::max<std::size_t>(
        1, static_cast<std::size_t>(config_.sampleRate) * kMaxOutputQueuedMilliseconds /
               1'000U);
    return frames * config_.channels;
}

std::size_t LoopbackBridge::outputPrimeSamples() const noexcept {
    const auto frames = std::max<std::size_t>(
        1, static_cast<std::size_t>(config_.sampleRate) * kOutputPrimeMilliseconds / 1'000U);
    return frames * config_.channels;
}

std::uint16_t LoopbackBridge::portFromEnvironment() noexcept {
    wchar_t value[16] {};
    const auto length = ::GetEnvironmentVariableW(L"CONVBASED_VST_BRIDGE_PORT", value,
                                                   static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) {
        return 43'127;
    }
    wchar_t* end = nullptr;
    const auto parsed = std::wcstoul(value, &end, 10);
    if (end == value || *end != L'\0' || parsed == 0 || parsed > 65'535) {
        return 43'127;
    }
    return static_cast<std::uint16_t>(parsed);
}

void LoopbackBridge::run() noexcept {
    WSADATA winsockData {};
    if (::WSAStartup(MAKEWORD(2, 2), &winsockData) != 0) {
        state_.store(BridgeState::faulted, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        return;
    }

    std::array<float, kMaxNetworkChunkFrames * kMaxChannels> sendSamples {};
    std::vector<std::byte> receiveBuffer;
    receiveBuffer.reserve(64U * 1024U);
    std::uint32_t sequence = 0;

    while (running_.load(std::memory_order_acquire)) {
        std::string apiKey;
        std::uint64_t credentialGeneration = 0;
        {
            std::unique_lock lock(credentialMutex_);
            if (apiKey_.empty()) {
                state_.store(BridgeState::waitingForCredentials, std::memory_order_release);
                credentialChanged_.wait_for(lock, 250ms, [this] {
                    return !running_.load(std::memory_order_acquire) || !apiKey_.empty();
                });
            }
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            apiKey = apiKey_;
            credentialGeneration = credentialGeneration_.load(std::memory_order_relaxed);
        }

        if (apiKey.empty()) {
            continue;
        }
        state_.store(BridgeState::connecting, std::memory_order_release);
        const auto socketHandle = connectToBridgeService(config_.port);
        if (socketHandle == INVALID_SOCKET) {
            state_.store(BridgeState::disconnected, std::memory_order_release);
            input_.discardAvailable();
            waitBeforeReconnect(running_);
            continue;
        }

        const auto hello = protocol::encodeHello(
            {config_.sampleRate, config_.channels, config_.maxBlockFrames});
        if (!sendFrame(socketHandle, MessageType::hello, sequence++, hello)) {
            ::closesocket(socketHandle);
            state_.store(BridgeState::disconnected, std::memory_order_release);
            waitBeforeReconnect(running_);
            continue;
        }
        const auto apiKeyBytes = std::as_bytes(
            std::span<const char>(apiKey.data(), apiKey.size()));
        if (!sendFrame(socketHandle, MessageType::authenticate, sequence++, apiKeyBytes)) {
            ::closesocket(socketHandle);
            state_.store(BridgeState::disconnected, std::memory_order_release);
            waitBeforeReconnect(running_);
            continue;
        }

        receiveBuffer.clear();
        state_.store(BridgeState::connected, std::memory_order_release);
        bool connectionAlive = true;
        const auto chunkFrames = std::clamp<std::size_t>(config_.sampleRate / 100U, 1,
                                                        kMaxNetworkChunkFrames);
        while (connectionAlive && running_.load(std::memory_order_acquire) &&
               credentialGeneration_.load(std::memory_order_acquire) == credentialGeneration) {
            bool sentAudio = false;
            auto availableSamples = input_.availableToRead();
            const auto queueLimit = maxInputQueuedSamples();
            if (availableSamples > queueLimit) {
                auto staleSamples = availableSamples - queueLimit;
                staleSamples -= staleSamples % config_.channels;
                const auto discarded = input_.discardOldest(staleSamples);
                droppedInputSamples_.fetch_add(discarded, std::memory_order_relaxed);
                availableSamples -= discarded;
            }
            if (availableSamples >= config_.channels) {
                auto sampleCount = std::min<std::size_t>(availableSamples,
                                                        chunkFrames * config_.channels);
                sampleCount -= sampleCount % config_.channels;
                if (input_.pop(std::span<float>(sendSamples.data(), sampleCount))) {
                    const auto bytes = std::as_bytes(
                        std::span<const float>(sendSamples.data(), sampleCount));
                    connectionAlive =
                        sendFrame(socketHandle, MessageType::audioInput, sequence++, bytes);
                    sentAudio = true;
                }
            }

            if (connectionAlive) {
                connectionAlive = receiveFrames(socketHandle, receiveBuffer, output_,
                                                droppedOutputSamples_, config_.channels);
            }
            if (!sentAudio && connectionAlive) {
                std::this_thread::sleep_for(1ms);
            }
        }

        ::shutdown(socketHandle, SD_BOTH);
        ::closesocket(socketHandle);
        input_.discardAvailable();
        if (running_.load(std::memory_order_acquire)) {
            state_.store(BridgeState::disconnected, std::memory_order_release);
            waitBeforeReconnect(running_);
        }
    }

    ::WSACleanup();
}

} // namespace convbased::bridge

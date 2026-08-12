#pragma once

#include "audio_ring_buffer.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace convbased::bridge {

enum class BridgeState : std::uint8_t {
    stopped,
    waitingForCredentials,
    connecting,
    connected,
    disconnected,
    faulted,
};

struct BridgeConfig final {
    std::uint32_t sampleRate {48'000};
    std::uint32_t maxBlockFrames {1'024};
    std::uint16_t channels {2};
    std::uint16_t port {43'127};
};

class LoopbackBridge final {
public:
    static constexpr std::size_t kRingCapacitySamples = 1U << 18U;
    static constexpr std::size_t kMaxChannels = 2;
    static constexpr std::size_t kMaxNetworkChunkFrames = 4'096;

    LoopbackBridge() = default;
    ~LoopbackBridge();

    LoopbackBridge(const LoopbackBridge&) = delete;
    LoopbackBridge& operator=(const LoopbackBridge&) = delete;

    [[nodiscard]] bool start(const BridgeConfig& config);
    void stop() noexcept;
    [[nodiscard]] bool setApiKey(std::string apiKey);

    [[nodiscard]] bool pushInput(std::span<const float> interleaved) noexcept;
    [[nodiscard]] bool tryPopOutput(std::span<float> interleaved) noexcept;
    void discardOutput() noexcept;

    [[nodiscard]] BridgeState state() const noexcept;
    [[nodiscard]] std::uint64_t droppedInputSamples() const noexcept;
    [[nodiscard]] std::uint64_t droppedOutputSamples() const noexcept;
    [[nodiscard]] static std::uint16_t portFromEnvironment() noexcept;

private:
    void run() noexcept;

    BridgeConfig config_ {};
    SpscFloatRingBuffer<kRingCapacitySamples> input_ {};
    SpscFloatRingBuffer<kRingCapacitySamples> output_ {};
    std::atomic<BridgeState> state_ {BridgeState::stopped};
    std::atomic<bool> running_ {false};
    std::atomic<std::uint64_t> droppedInputSamples_ {0};
    std::atomic<std::uint64_t> droppedOutputSamples_ {0};
    mutable std::mutex credentialMutex_ {};
    std::condition_variable credentialChanged_ {};
    std::string apiKey_ {};
    std::atomic<std::uint64_t> credentialGeneration_ {0};
    std::thread worker_ {};
};

} // namespace convbased::bridge

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace convbased::bridge {

template <std::size_t CapacitySamples>
class SpscFloatRingBuffer final {
    static_assert(CapacitySamples > 1);
    static_assert((CapacitySamples & (CapacitySamples - 1)) == 0,
                  "ring-buffer capacity must be a power of two");

public:
    [[nodiscard]] bool push(std::span<const float> samples) noexcept {
        if (samples.empty()) {
            return true;
        }

        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto read = readIndex_.load(std::memory_order_acquire);
        if (samples.size() > CapacitySamples - static_cast<std::size_t>(write - read)) {
            return false;
        }

        copyInto(write, samples);
        writeIndex_.store(write + samples.size(), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(std::span<float> destination) noexcept {
        if (destination.empty()) {
            return true;
        }

        const auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        if (destination.size() > static_cast<std::size_t>(write - read)) {
            return false;
        }

        copyOut(read, destination);
        readIndex_.store(read + destination.size(), std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t availableToRead() const noexcept {
        const auto read = readIndex_.load(std::memory_order_acquire);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(write - read);
    }

    [[nodiscard]] std::size_t discardOldest(std::size_t sampleCount) noexcept {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        const auto discarded =
            std::min(sampleCount, static_cast<std::size_t>(write - read));
        readIndex_.store(read + discarded, std::memory_order_release);
        return discarded;
    }

    [[nodiscard]] constexpr std::size_t capacity() const noexcept {
        return CapacitySamples;
    }

    // Only call reset while producer and consumer are stopped.
    void reset() noexcept {
        readIndex_.store(0, std::memory_order_relaxed);
        writeIndex_.store(0, std::memory_order_relaxed);
    }

    // Consumer-only operation used to discard stale data after a disconnect.
    void discardAvailable() noexcept {
        static_cast<void>(discardOldest(availableToRead()));
    }

private:
    static constexpr std::size_t kMask = CapacitySamples - 1;

    void copyInto(std::uint64_t write, std::span<const float> samples) noexcept {
        const auto offset = static_cast<std::size_t>(write) & kMask;
        const auto first = std::min(samples.size(), CapacitySamples - offset);
        std::copy_n(samples.data(), first, storage_.data() + offset);
        std::copy_n(samples.data() + first, samples.size() - first, storage_.data());
    }

    void copyOut(std::uint64_t read, std::span<float> destination) noexcept {
        const auto offset = static_cast<std::size_t>(read) & kMask;
        const auto first = std::min(destination.size(), CapacitySamples - offset);
        std::copy_n(storage_.data() + offset, first, destination.data());
        std::copy_n(storage_.data(), destination.size() - first, destination.data() + first);
    }

    alignas(64) std::array<float, CapacitySamples> storage_ {};
    alignas(64) std::atomic<std::uint64_t> readIndex_ {0};
    alignas(64) std::atomic<std::uint64_t> writeIndex_ {0};
};

} // namespace convbased::bridge

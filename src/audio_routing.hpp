#pragma once

#include <algorithm>
#include <cstdint>

namespace convbased::vst3 {

struct RoutingGains final {
    float original {0.0F};
    float converted {0.0F};
};

[[nodiscard]] constexpr RoutingGains selectRoutingGains(
    bool bypass, bool hasConvertedAudio, double convertedLevel) noexcept {
    if (bypass) {
        return {1.0F, 0.0F};
    }
    if (!hasConvertedAudio) {
        return {0.0F, 0.0F};
    }
    return {0.0F, static_cast<float>(std::clamp(convertedLevel, 0.0, 1.0))};
}

[[nodiscard]] constexpr float interpolateTransition(
    float start, float target, std::uint32_t completedFrames,
    std::uint32_t totalFrames) noexcept {
    if (totalFrames == 0 || completedFrames >= totalFrames) {
        return target;
    }
    const auto progress = static_cast<float>(completedFrames) /
                          static_cast<float>(totalFrames);
    return start + (target - start) * progress;
}

} // namespace convbased::vst3

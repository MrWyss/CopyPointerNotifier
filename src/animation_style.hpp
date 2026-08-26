#pragma once

#include <algorithm>

enum class AnimationStyle {
    FadeAndShrink = 0,
    CameraShutter = 1,
    Pulse = 2,
    Wobble = 3,
};

inline constexpr int kAnimationStyleCount = 4;

constexpr AnimationStyle NormalizeAnimationStyle(int value) {
    return static_cast<AnimationStyle>(
        std::clamp(value, 0, kAnimationStyleCount - 1));
}

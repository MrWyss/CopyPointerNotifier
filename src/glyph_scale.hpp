#pragma once

#include <algorithm>

inline constexpr int kMinimumGlyphScale = 0;
inline constexpr int kGlyphScaleMidpoint = 50;
inline constexpr int kMaximumGlyphScale = 100;
inline constexpr int kMaximumEffectiveGlyphScale = 160;

constexpr float EffectiveGlyphScalePercent(int normalizedScale) {
    const int clamped = std::clamp(
        normalizedScale,
        kMinimumGlyphScale,
        kMaximumGlyphScale);
    if (clamped <= kGlyphScaleMidpoint) {
        return static_cast<float>(clamped);
    }
    return kGlyphScaleMidpoint +
           (clamped - kGlyphScaleMidpoint) *
               (kMaximumEffectiveGlyphScale - kGlyphScaleMidpoint) /
               static_cast<float>(kMaximumGlyphScale - kGlyphScaleMidpoint);
}

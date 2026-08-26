#pragma once

#include "animation_style.hpp"
#include "overlay_window.hpp"

bool IsStartWithWindowsEnabled();
bool SetStartWithWindowsEnabled(bool enabled);
bool LoadApplicationEnabled();
bool SaveApplicationEnabled(bool enabled);
GlyphPosition LoadGlyphPosition();
int LoadGlyphScalePercent();
AnimationStyle LoadAnimationStyle();
int LoadAnimationSpeed();
int LoadIndicatorVisibilityDuration();

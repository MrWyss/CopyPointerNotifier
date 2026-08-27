#pragma once

#include "animation_style.hpp"
#include "clipboard_rules.hpp"

#include <windows.h>

struct GlyphPosition {
    int xPercent = 30;
    int yPercent = 50;
};

class OverlayWindow {
public:
    ~OverlayWindow();

    bool Initialize(HINSTANCE instance);
    void Show(ClipboardIndicator indicator);
    void Hide();
    bool Tick();
    bool IsSettled() const;
    void MoveToCursor(const POINT& cursorPosition);
    void SetGlyphPosition(GlyphPosition position);
    void SetGlyphScalePercent(int scalePercent);
    void SetAnimationStyle(AnimationStyle animationStyle);
    void SetAnimationSpeed(int animationSpeed);
    void SetVisibilityDuration(int durationMilliseconds);
    void RefreshCursorSettings();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool CalculateIconRect(const CURSORINFO& cursorInfo, RECT& iconRect);
    bool PositionNearCursor(const CURSORINFO& cursorInfo);
    bool RenderAtCursor(const CURSORINFO& cursorInfo);
    void UpdateCursorMetrics(HCURSOR cursor);
    void UpdateCursorStyleColor(const POINT& cursorPosition);
    int Scale(int value) const;
    int BaseIconSize() const;
    float CurrentSizeFactor() const;
    ULONGLONG CurrentAnimationElapsed() const;
    float CurrentShutterRevealFactor() const;
    float CurrentWobbleAngle() const;

    HWND window_ = nullptr;
    ClipboardIndicator indicator_{ClipboardContentType::Text, {}};
    ULONGLONG animationStartedAt_ = 0;
    UINT dpi_ = 96;
    BYTE opacity_ = 0;
    bool settled_ = false;
    bool needsRender_ = false;
    ULONG_PTR gdiplusToken_ = 0;
    HCURSOR cachedCursor_ = nullptr;
    int cursorWidth_ = 32;
    int cursorHeight_ = 32;
    int cursorHotspotX_ = 0;
    int cursorHotspotY_ = 0;
    COLORREF cursorColor_ = RGB(255, 255, 255);
    DWORD cursorType_ = 0;
    int configuredCursorSize_ = 32;
    GlyphPosition glyphPosition_{};
    int glyphScalePercent_ = 50;
    AnimationStyle animationStyle_ = AnimationStyle::FadeAndShrink;
    int animationSpeed_ = 50;
    int visibilityDurationMs_ = 0;
    ULONGLONG cursorSettingsReadAt_ = 0;
};

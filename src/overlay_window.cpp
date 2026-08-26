#include "overlay_window.hpp"

#include "glyph_scale.hpp"

#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>

namespace {

constexpr DWORD kExcludeFromCapture = 0x00000011;

constexpr wchar_t kOverlayClassName[] = L"CopyPointerNotifier.Overlay";
constexpr ULONGLONG kColorFadeDurationMs = 650;
constexpr ULONGLONG kShrinkDurationMs = 250;
constexpr ULONGLONG kAnimationDurationMs = kColorFadeDurationMs + kShrinkDurationMs;
constexpr float kSettledSizeFactor = 0.65F;
constexpr int kDesignSize = 40;
constexpr float kGlyphScaleNormalization = 1.1F;
constexpr int kSupersamplingFactor = 4;
constexpr ULONGLONG kCursorSettingsRefreshMs = 200;

struct RgbColor {
    BYTE red;
    BYTE green;
    BYTE blue;
};

BYTE BlendChannel(BYTE from, BYTE to, float progress) {
    return static_cast<BYTE>(from + (to - from) * progress);
}

RgbColor BlendColor(RgbColor from, RgbColor to, float progress) {
    return {
        BlendChannel(from.red, to.red, progress),
        BlendChannel(from.green, to.green, progress),
        BlendChannel(from.blue, to.blue, progress),
    };
}

RgbColor Lighten(RgbColor color, float amount) {
    return BlendColor(color, {255, 255, 255}, amount);
}

RgbColor Darken(RgbColor color, float amount) {
    return BlendColor(color, {0, 0, 0}, amount);
}

RgbColor ContrastingColor(RgbColor top, RgbColor bottom) {
    const int red = (top.red + bottom.red) / 2;
    const int green = (top.green + bottom.green) / 2;
    const int blue = (top.blue + bottom.blue) / 2;
    const int luminance = (red * 299 + green * 587 + blue * 114) / 1000;
    return luminance >= 145 ? RgbColor{0, 0, 0} : RgbColor{255, 255, 255};
}

Gdiplus::Color ToGdiColor(BYTE alpha, RgbColor color) {
    return Gdiplus::Color(alpha, color.red, color.green, color.blue);
}

RgbColor GetWindowsAccentColor() {
    DWORD colorizationColor = 0;
    BOOL opaqueBlend = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&colorizationColor, &opaqueBlend))) {
        return {
            static_cast<BYTE>((colorizationColor >> 16) & 0xFF),
            static_cast<BYTE>((colorizationColor >> 8) & 0xFF),
            static_cast<BYTE>(colorizationColor & 0xFF),
        };
    }
    return {0, 120, 212};
}

RgbColor FromColorRef(COLORREF color) {
    return {
        GetRValue(color),
        GetGValue(color),
        GetBValue(color),
    };
}

DWORD ReadCursorSetting(
    const wchar_t* subKey,
    const wchar_t* valueName,
    DWORD fallback) {
    DWORD value = fallback;
    DWORD size = sizeof(value);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            subKey,
            valueName,
            RRF_RT_REG_DWORD,
            nullptr,
            &value,
            &size) != ERROR_SUCCESS) {
        return fallback;
    }
    return value;
}

bool SampleRenderedCursorColor(
    HCURSOR cursor,
    int cursorSize,
    COLORREF& sampledColor) {
    if (!cursor || cursorSize <= 0) {
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = cursorSize;
    bitmapInfo.bmiHeader.biHeight = -cursorSize;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixelData = nullptr;
    HDC screenContext = GetDC(nullptr);
    HDC memoryContext = CreateCompatibleDC(screenContext);
    HBITMAP bitmap = CreateDIBSection(
        screenContext,
        &bitmapInfo,
        DIB_RGB_COLORS,
        &pixelData,
        nullptr,
        0);
    ReleaseDC(nullptr, screenContext);
    if (!memoryContext || !bitmap || !pixelData) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (memoryContext) {
            DeleteDC(memoryContext);
        }
        return false;
    }

    constexpr DWORD kBackgroundPixel = 0x007F7F7F;
    std::fill_n(
        static_cast<DWORD*>(pixelData),
        static_cast<size_t>(cursorSize) * cursorSize,
        kBackgroundPixel);
    HGDIOBJ previousBitmap = SelectObject(memoryContext, bitmap);
    const BOOL drawn = DrawIconEx(
        memoryContext,
        0,
        0,
        cursor,
        cursorSize,
        cursorSize,
        0,
        nullptr,
        DI_NORMAL);

    struct ColorBucket {
        ULONGLONG red = 0;
        ULONGLONG green = 0;
        ULONGLONG blue = 0;
        ULONGLONG count = 0;
    };
    std::array<ColorBucket, 4096> buckets{};
    if (drawn) {
        const auto* pixels = static_cast<const DWORD*>(pixelData);
        for (size_t index = 0;
             index < static_cast<size_t>(cursorSize) * cursorSize;
             ++index) {
            const DWORD pixel = pixels[index];
            const BYTE red = static_cast<BYTE>((pixel >> 16) & 0xFF);
            const BYTE green = static_cast<BYTE>((pixel >> 8) & 0xFF);
            const BYTE blue = static_cast<BYTE>(pixel & 0xFF);
            const int backgroundDistance =
                std::abs(static_cast<int>(red) - 127) +
                std::abs(static_cast<int>(green) - 127) +
                std::abs(static_cast<int>(blue) - 127);
            if (backgroundDistance < 45) {
                continue;
            }

            const size_t bucketIndex =
                (static_cast<size_t>(red >> 4) << 8) |
                (static_cast<size_t>(green >> 4) << 4) |
                static_cast<size_t>(blue >> 4);
            ColorBucket& bucket = buckets[bucketIndex];
            bucket.red += red;
            bucket.green += green;
            bucket.blue += blue;
            ++bucket.count;
        }
    }

    SelectObject(memoryContext, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryContext);

    const auto dominant = std::max_element(
        buckets.begin(),
        buckets.end(),
        [](const ColorBucket& left, const ColorBucket& right) {
            return left.count < right.count;
        });
    if (!drawn || dominant == buckets.end() || dominant->count == 0) {
        return false;
    }

    sampledColor = RGB(
        static_cast<BYTE>(dominant->red / dominant->count),
        static_cast<BYTE>(dominant->green / dominant->count),
        static_cast<BYTE>(dominant->blue / dominant->count));
    return true;
}

}  // namespace

OverlayWindow::~OverlayWindow() {
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (gdiplusToken_ != 0) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

bool OverlayWindow::Initialize(HINSTANCE instance) {
    Gdiplus::GdiplusStartupInput startupInput;
    if (Gdiplus::GdiplusStartup(
            &gdiplusToken_,
            &startupInput,
            nullptr) != Gdiplus::Ok) {
        return false;
    }

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kOverlayClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kOverlayClassName,
        L"",
        WS_POPUP,
        0,
        0,
        kDesignSize,
        kDesignSize,
        nullptr,
        nullptr,
        instance,
        this);
    if (!window_) {
        return false;
    }
    if (!SetWindowDisplayAffinity(window_, kExcludeFromCapture)) {
        DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }
    return true;
}

void OverlayWindow::Show(ClipboardContentType contentType) {
    contentType_ = contentType;
    animationStartedAt_ = GetTickCount64();
    opacity_ = 255;
    settled_ = false;
    needsRender_ = true;
}

void OverlayWindow::SetAnimationStyle(AnimationStyle animationStyle) {
    animationStyle_ = animationStyle;
    needsRender_ = true;
}

void OverlayWindow::SetAnimationSpeed(int animationSpeed) {
    animationSpeed_ = std::clamp(animationSpeed, 0, 100);
}

void OverlayWindow::SetVisibilityDuration(int durationMilliseconds) {
    visibilityDurationMs_ = std::max(0, durationMilliseconds);
}

void OverlayWindow::Hide() {
    animationStartedAt_ = 0;
    opacity_ = 0;
    settled_ = false;
    needsRender_ = false;
    if (window_) {
        ShowWindow(window_, SW_HIDE);
    }
}

bool OverlayWindow::Tick() {
    if (!window_ || animationStartedAt_ == 0) {
        return false;
    }

    CURSORINFO cursorInfo{sizeof(cursorInfo)};
    if (!GetCursorInfo(&cursorInfo) || (cursorInfo.flags & CURSOR_SHOWING) == 0) {
        ShowWindow(window_, SW_HIDE);
        return true;
    }

    const ULONGLONG elapsed = CurrentAnimationElapsed();
    if (visibilityDurationMs_ > 0 &&
        GetTickCount64() - animationStartedAt_ >=
            static_cast<ULONGLONG>(visibilityDurationMs_)) {
        Hide();
        return false;
    }
    const bool wasSettled = settled_;
    const float fadeProgress = std::clamp(
        static_cast<float>(elapsed) / static_cast<float>(kColorFadeDurationMs),
        0.0F,
        1.0F);
    opacity_ = static_cast<BYTE>(255.0F - (35.0F * fadeProgress));
    settled_ = elapsed >= kAnimationDurationMs;

    if (!settled_ || !wasSettled || needsRender_) {
        needsRender_ = !RenderAtCursor(cursorInfo);
    } else {
        PositionNearCursor(cursorInfo);
    }
    return true;
}

bool OverlayWindow::IsSettled() const {
    return settled_;
}

void OverlayWindow::MoveToCursor(const POINT& cursorPosition) {
    if (!window_ || animationStartedAt_ == 0) {
        return;
    }

    CURSORINFO cursorInfo{sizeof(cursorInfo)};
    if (!GetCursorInfo(&cursorInfo) || (cursorInfo.flags & CURSOR_SHOWING) == 0) {
        ShowWindow(window_, SW_HIDE);
        return;
    }
    cursorInfo.ptScreenPos = cursorPosition;

    if (needsRender_) {
        needsRender_ = !RenderAtCursor(cursorInfo);
    } else {
        PositionNearCursor(cursorInfo);
    }
}

void OverlayWindow::SetGlyphPosition(GlyphPosition position) {
    glyphPosition_ = position;
    needsRender_ = true;
}

void OverlayWindow::SetGlyphScalePercent(int scalePercent) {
    glyphScalePercent_ = std::clamp(
        scalePercent,
        kMinimumGlyphScale,
        kMaximumGlyphScale);
    needsRender_ = true;
}

void OverlayWindow::RefreshCursorSettings() {
    cursorSettingsReadAt_ = 0;
    cachedCursor_ = nullptr;
    needsRender_ = true;
}

LRESULT CALLBACK OverlayWindow::WindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {
    OverlayWindow* self = reinterpret_cast<OverlayWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    return self ? self->HandleMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(window_, nullptr);
            return 0;
        case WM_DPICHANGED:
            dpi_ = HIWORD(wParam);
            needsRender_ = true;
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            cachedCursor_ = nullptr;
            needsRender_ = true;
            return 0;
        case WM_DWMCOLORIZATIONCOLORCHANGED:
            needsRender_ = true;
            return 0;
        default:
            return DefWindowProcW(window_, message, wParam, lParam);
    }
}

bool OverlayWindow::CalculateIconRect(
    const CURSORINFO& cursorInfo,
    RECT& iconRect) {
    dpi_ = GetDpiForWindow(window_);
    UpdateCursorMetrics(cursorInfo.hCursor);
    UpdateCursorStyleColor(cursorInfo.ptScreenPos);

    const int iconSize = std::max(
        Scale(1),
        static_cast<int>(std::lround(BaseIconSize() * CurrentSizeFactor())));
    const int xOffset = static_cast<int>(std::lround(
        configuredCursorSize_ * glyphPosition_.xPercent / 100.0F));
    const int yOffset = static_cast<int>(std::lround(
        configuredCursorSize_ * glyphPosition_.yPercent / 100.0F));
    iconRect = {
        cursorInfo.ptScreenPos.x + xOffset,
        cursorInfo.ptScreenPos.y + yOffset,
        cursorInfo.ptScreenPos.x + xOffset + iconSize,
        cursorInfo.ptScreenPos.y + yOffset + iconSize,
    };

    const HMONITOR monitor = MonitorFromPoint(
        cursorInfo.ptScreenPos,
        MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        ShowWindow(window_, SW_HIDE);
        return false;
    }

    const RECT& screen = monitorInfo.rcMonitor;
    if (iconRect.left < screen.left ||
        iconRect.top < screen.top ||
        iconRect.right > screen.right ||
        iconRect.bottom > screen.bottom) {
        ShowWindow(window_, SW_HIDE);
        return false;
    }
    return true;
}

bool OverlayWindow::PositionNearCursor(const CURSORINFO& cursorInfo) {
    RECT iconRect{};
    if (!CalculateIconRect(cursorInfo, iconRect)) {
        return false;
    }

    SetWindowPos(
        window_,
        HWND_TOPMOST,
        iconRect.left,
        iconRect.top,
        0,
        0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

bool OverlayWindow::RenderAtCursor(const CURSORINFO& cursorInfo) {
    RECT iconRect{};
    if (!CalculateIconRect(cursorInfo, iconRect)) {
        return false;
    }

    const int width = iconRect.right - iconRect.left;
    const int height = iconRect.bottom - iconRect.top;
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC screenContext = GetDC(nullptr);
    HBITMAP bitmapHandle = CreateDIBSection(
        screenContext,
        &bitmapInfo,
        DIB_RGB_COLORS,
        &pixels,
        nullptr,
        0);
    if (!bitmapHandle || !pixels) {
        if (bitmapHandle) {
            DeleteObject(bitmapHandle);
        }
        ReleaseDC(nullptr, screenContext);
        return false;
    }
    std::memset(pixels, 0, static_cast<size_t>(width) * height * 4);

    {
        Gdiplus::Bitmap targetBitmap(
            width,
            height,
            width * 4,
            PixelFormat32bppPARGB,
            static_cast<BYTE*>(pixels));
        const int renderWidth = width * kSupersamplingFactor;
        const int renderHeight = height * kSupersamplingFactor;
        Gdiplus::Bitmap supersampledBitmap(
            renderWidth,
            renderHeight,
            PixelFormat32bppPARGB);
        Gdiplus::Graphics graphics(&supersampledBitmap);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        if (animationStyle_ == AnimationStyle::CameraShutter) {
            const float reveal = CurrentShutterRevealFactor();
            const float radius = std::max(
                0.1F,
                static_cast<float>(renderWidth) * 0.72F * reveal);
            Gdiplus::GraphicsPath aperture;
            aperture.AddEllipse(
                renderWidth * 0.5F - radius,
                renderHeight * 0.5F - radius,
                radius * 2.0F,
                radius * 2.0F);
            graphics.SetClip(&aperture, Gdiplus::CombineModeReplace);
        }

        const float wobbleAngle = CurrentWobbleAngle();
        if (wobbleAngle != 0.0F) {
            graphics.TranslateTransform(
                renderWidth * 0.5F,
                renderHeight * 0.5F);
            graphics.RotateTransform(wobbleAngle);
            graphics.TranslateTransform(
                renderWidth * -0.5F,
                renderHeight * -0.5F);
        }

        const float progress = std::clamp(
            static_cast<float>(CurrentAnimationElapsed()) /
                static_cast<float>(kColorFadeDurationMs),
            0.0F,
            1.0F);
        const RgbColor accent = GetWindowsAccentColor();
        const RgbColor cursorColor = FromColorRef(cursorColor_);
        const RgbColor animatedTop =
            BlendColor(Lighten(accent, 0.35F), cursorColor, progress);
        const RgbColor animatedBottom =
            BlendColor(Darken(accent, 0.08F), cursorColor, progress);
        Gdiplus::LinearGradientBrush iconBrush(
            Gdiplus::PointF(0.0F, 0.0F),
            Gdiplus::PointF(0.0F, static_cast<float>(renderHeight)),
            ToGdiColor(opacity_, animatedTop),
            ToGdiColor(opacity_, animatedBottom));

        const float unit = static_cast<float>(renderWidth) / kDesignSize;
        const float glyphWidth = std::max(2.0F, unit * 2.4F);
        const float outlineWidth = std::max(1.0F, unit * 1.15F);
        Gdiplus::SolidBrush outlineBrush(
            ToGdiColor(opacity_, ContrastingColor(animatedTop, animatedBottom)));
        Gdiplus::Pen outline(
            &outlineBrush,
            glyphWidth + outlineWidth * 2.0F);
        Gdiplus::Pen glyph(&iconBrush, glyphWidth);
        outline.SetLineCap(
            Gdiplus::LineCapRound,
            Gdiplus::LineCapRound,
            Gdiplus::DashCapRound);
        outline.SetLineJoin(Gdiplus::LineJoinRound);
        glyph.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        glyph.SetLineJoin(Gdiplus::LineJoinRound);
        const auto coordinate = [unit](float value) {
            return value * unit;
        };

        if (contentType_ == ClipboardContentType::Text ||
            contentType_ == ClipboardContentType::RichText) {
            const wchar_t* label =
                contentType_ == ClipboardContentType::Text ? L"T" : L"RT";
            Gdiplus::FontFamily family(L"Segoe UI");
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::GraphicsPath textPath;
            textPath.AddString(
                label,
                -1,
                &family,
                Gdiplus::FontStyleBold,
                coordinate(contentType_ == ClipboardContentType::Text ? 30.0F : 22.0F),
                Gdiplus::RectF(
                    0.0F,
                    0.0F,
                    static_cast<float>(renderWidth),
                    static_cast<float>(renderHeight)),
                &format);
            Gdiplus::Pen textOutline(&outlineBrush, outlineWidth * 2.0F);
            textOutline.SetLineJoin(Gdiplus::LineJoinRound);
            graphics.DrawPath(&textOutline, &textPath);
            graphics.FillPath(&iconBrush, &textPath);
        } else if (contentType_ == ClipboardContentType::Image) {
            graphics.DrawRectangle(
                &outline,
                coordinate(4),
                coordinate(6),
                coordinate(32),
                coordinate(28));
            graphics.DrawEllipse(
                &outline,
                coordinate(25),
                coordinate(10),
                coordinate(5),
                coordinate(5));
            graphics.DrawRectangle(
                &glyph,
                coordinate(4),
                coordinate(6),
                coordinate(32),
                coordinate(28));
            graphics.DrawEllipse(
                &glyph,
                coordinate(25),
                coordinate(10),
                coordinate(5),
                coordinate(5));
            Gdiplus::PointF landscape[] = {
                {coordinate(6), coordinate(32)},
                {coordinate(15), coordinate(21)},
                {coordinate(21), coordinate(27)},
                {coordinate(27), coordinate(22)},
                {coordinate(34), coordinate(32)},
            };
            graphics.DrawLines(&outline, landscape, static_cast<int>(std::size(landscape)));
            graphics.DrawLines(&glyph, landscape, static_cast<int>(std::size(landscape)));
        } else if (contentType_ == ClipboardContentType::Files) {
            Gdiplus::PointF page[] = {
                {coordinate(8), coordinate(3)},
                {coordinate(25), coordinate(3)},
                {coordinate(34), coordinate(12)},
                {coordinate(34), coordinate(37)},
                {coordinate(8), coordinate(37)},
                {coordinate(8), coordinate(3)},
            };
            graphics.DrawLines(&outline, page, static_cast<int>(std::size(page)));
            graphics.DrawLines(&glyph, page, static_cast<int>(std::size(page)));
            Gdiplus::PointF fold[] = {
                {coordinate(25), coordinate(3)},
                {coordinate(25), coordinate(12)},
                {coordinate(34), coordinate(12)},
            };
            graphics.DrawLines(&outline, fold, static_cast<int>(std::size(fold)));
            graphics.DrawLines(&glyph, fold, static_cast<int>(std::size(fold)));
        } else {
            Gdiplus::PointF cube[] = {
                {coordinate(20), coordinate(3)},
                {coordinate(36), coordinate(12)},
                {coordinate(20), coordinate(21)},
                {coordinate(4), coordinate(12)},
                {coordinate(20), coordinate(3)},
            };
            graphics.DrawLines(&outline, cube, static_cast<int>(std::size(cube)));
            graphics.DrawLines(&glyph, cube, static_cast<int>(std::size(cube)));
            Gdiplus::PointF sides[] = {
                {coordinate(4), coordinate(12)},
                {coordinate(4), coordinate(29)},
                {coordinate(20), coordinate(38)},
                {coordinate(36), coordinate(29)},
                {coordinate(36), coordinate(12)},
            };
            graphics.DrawLines(&outline, sides, static_cast<int>(std::size(sides)));
            graphics.DrawLines(&glyph, sides, static_cast<int>(std::size(sides)));
            graphics.DrawLine(
                &outline,
                coordinate(20),
                coordinate(21),
                coordinate(20),
                coordinate(38));
            graphics.DrawLine(
                &glyph,
                coordinate(20),
                coordinate(21),
                coordinate(20),
                coordinate(38));
        }

        Gdiplus::Graphics targetGraphics(&targetBitmap);
        targetGraphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        targetGraphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        targetGraphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        targetGraphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        targetGraphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        targetGraphics.DrawImage(
            &supersampledBitmap,
            Gdiplus::Rect(0, 0, width, height),
            0,
            0,
            renderWidth,
            renderHeight,
            Gdiplus::UnitPixel);
    }

    HDC memoryContext = CreateCompatibleDC(screenContext);
    if (!memoryContext) {
        DeleteObject(bitmapHandle);
        ReleaseDC(nullptr, screenContext);
        return false;
    }

    HGDIOBJ previousBitmap = SelectObject(memoryContext, bitmapHandle);
    POINT destination{iconRect.left, iconRect.top};
    POINT source{0, 0};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL updated = UpdateLayeredWindow(
        window_,
        screenContext,
        &destination,
        &size,
        memoryContext,
        &source,
        0,
        &blend,
        ULW_ALPHA);

    SelectObject(memoryContext, previousBitmap);
    DeleteDC(memoryContext);
    DeleteObject(bitmapHandle);
    ReleaseDC(nullptr, screenContext);

    if (!updated) {
        return false;
    }
    SetWindowPos(
        window_,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

void OverlayWindow::UpdateCursorMetrics(HCURSOR cursor) {
    const ULONGLONG now = GetTickCount64();
    if (cursorSettingsReadAt_ == 0 ||
        now - cursorSettingsReadAt_ >= kCursorSettingsRefreshMs) {
        cursorSettingsReadAt_ = now;
        const DWORD newCursorType = ReadCursorSetting(
            L"Software\\Microsoft\\Accessibility",
            L"CursorType",
            0);
        const int newCursorSize = static_cast<int>(ReadCursorSetting(
            L"Control Panel\\Cursors",
            L"CursorBaseSize",
            32));
        const COLORREF configuredColor = static_cast<COLORREF>(ReadCursorSetting(
            L"Software\\Microsoft\\Accessibility",
            L"CursorColor",
            RGB(255, 255, 255)));

        COLORREF newCursorColor = cursorColor_;
        if (newCursorType == 0) {
            newCursorColor = RGB(255, 255, 255);
        } else if (newCursorType == 1) {
            newCursorColor = RGB(0, 0, 0);
        } else if (newCursorType >= 3) {
            newCursorColor = configuredColor;
        }
        if (newCursorType != 2) {
            SampleRenderedCursorColor(
                cursor,
                newCursorSize,
                newCursorColor);
        }

        if (newCursorType != cursorType_ ||
            newCursorSize != configuredCursorSize_ ||
            newCursorColor != cursorColor_) {
            if (newCursorSize != configuredCursorSize_) {
                cachedCursor_ = nullptr;
            }
            cursorType_ = newCursorType;
            configuredCursorSize_ = newCursorSize;
            cursorColor_ = newCursorColor;
            needsRender_ = true;
        }
    }

    if (!cursor || cursor == cachedCursor_) {
        return;
    }

    ICONINFO iconInfo{};
    if (!GetIconInfo(cursor, &iconInfo)) {
        return;
    }

    BITMAP bitmap{};
    int bitmapWidth = 32;
    int bitmapHeight = 32;
    if (iconInfo.hbmColor &&
        GetObjectW(iconInfo.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        bitmapWidth = bitmap.bmWidth;
        bitmapHeight = std::abs(bitmap.bmHeight);
    } else if (iconInfo.hbmMask &&
               GetObjectW(iconInfo.hbmMask, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        bitmapWidth = bitmap.bmWidth;
        bitmapHeight = std::max(1L, std::abs(bitmap.bmHeight) / 2);
    }

    const float pointerScale = std::max(
        0.5F,
        static_cast<float>(configuredCursorSize_) / 32.0F);
    cursorWidth_ = static_cast<int>(std::lround(bitmapWidth * pointerScale));
    cursorHeight_ = static_cast<int>(std::lround(bitmapHeight * pointerScale));
    cursorHotspotX_ =
        static_cast<int>(std::lround(iconInfo.xHotspot * pointerScale));
    cursorHotspotY_ =
        static_cast<int>(std::lround(iconInfo.yHotspot * pointerScale));
    cachedCursor_ = cursor;

    if (iconInfo.hbmColor) {
        DeleteObject(iconInfo.hbmColor);
    }
    if (iconInfo.hbmMask) {
        DeleteObject(iconInfo.hbmMask);
    }
    needsRender_ = true;
}

void OverlayWindow::UpdateCursorStyleColor(const POINT& cursorPosition) {
    if (cursorType_ != 2) {
        return;
    }

    HDC screenContext = GetDC(nullptr);
    const COLORREF backgroundColor =
        GetPixel(screenContext, cursorPosition.x, cursorPosition.y);
    ReleaseDC(nullptr, screenContext);
    if (backgroundColor == CLR_INVALID) {
        return;
    }

    const COLORREF invertedColor = RGB(
        255 - GetRValue(backgroundColor),
        255 - GetGValue(backgroundColor),
        255 - GetBValue(backgroundColor));
    if (invertedColor != cursorColor_) {
        cursorColor_ = invertedColor;
        needsRender_ = true;
    }
}

int OverlayWindow::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

int OverlayWindow::BaseIconSize() const {
    const float pointerScale = std::max(
        0.5F,
        static_cast<float>(configuredCursorSize_) / 32.0F);
    return static_cast<int>(std::lround(
        Scale(kDesignSize) *
        pointerScale *
        kGlyphScaleNormalization *
        EffectiveGlyphScalePercent(glyphScalePercent_) /
        100.0F));
}

float OverlayWindow::CurrentSizeFactor() const {
    if (animationStartedAt_ == 0) {
        return kSettledSizeFactor;
    }

    const ULONGLONG elapsed = CurrentAnimationElapsed();
    if (elapsed < kColorFadeDurationMs) {
        const float progress =
            static_cast<float>(elapsed) /
            static_cast<float>(kColorFadeDurationMs);
        if (animationStyle_ == AnimationStyle::Pulse) {
            const float wave = std::sin(progress * 4.0F * 3.14159265F);
            return 1.0F + wave * 0.14F * (1.0F - progress);
        }
    }
    if (elapsed <= kColorFadeDurationMs) {
        return 1.0F;
    }
    if (elapsed >= kAnimationDurationMs) {
        return kSettledSizeFactor;
    }

    const float shrinkProgress =
        static_cast<float>(elapsed - kColorFadeDurationMs) /
        static_cast<float>(kShrinkDurationMs);
    const float easedProgress = 1.0F - std::pow(1.0F - shrinkProgress, 3.0F);
    return 1.0F - ((1.0F - kSettledSizeFactor) * easedProgress);
}

ULONGLONG OverlayWindow::CurrentAnimationElapsed() const {
    if (animationStartedAt_ == 0) {
        return 0;
    }
    const float timeScale = animationSpeed_ <= 50
        ? 0.5F + animationSpeed_ / 100.0F
        : 1.0F + (animationSpeed_ - 50) / 50.0F;
    return static_cast<ULONGLONG>(std::lround(
        (GetTickCount64() - animationStartedAt_) * timeScale));
}

float OverlayWindow::CurrentShutterRevealFactor() const {
    if (animationStyle_ != AnimationStyle::CameraShutter ||
        animationStartedAt_ == 0) {
        return 1.0F;
    }
    const float progress = std::clamp(
        static_cast<float>(CurrentAnimationElapsed()) / 420.0F,
        0.0F,
        1.0F);
    return progress * progress * (3.0F - 2.0F * progress);
}

float OverlayWindow::CurrentWobbleAngle() const {
    if (animationStyle_ != AnimationStyle::Wobble ||
        animationStartedAt_ == 0) {
        return 0.0F;
    }
    const float progress = std::clamp(
        static_cast<float>(CurrentAnimationElapsed()) / 650.0F,
        0.0F,
        1.0F);
    return std::sin(progress * 5.0F * 3.14159265F) *
           14.0F *
           std::pow(1.0F - progress, 2.0F);
}

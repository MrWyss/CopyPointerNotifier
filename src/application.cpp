#include "application.hpp"

#include "clipboard_classifier.hpp"
#include "startup_registration.hpp"

#include <shellapi.h>

#include <filesystem>
#include <string>

namespace {

constexpr wchar_t kWindowClassName[] = L"CopyPointerNotifier.MessageWindow";
constexpr wchar_t kSingleInstanceName[] = L"Local\\CopyPointerNotifier.SingleInstance";
constexpr wchar_t kSettingsWindowProperty[] =
    L"MrWyss.CopyPointerNotifier.SettingsWindow";
constexpr UINT_PTR kAnimationTimer = 1;
constexpr UINT_PTR kClipboardRetryTimer = 2;
constexpr UINT kAnimationIntervalMs = 16;
constexpr UINT kSettledTrackingIntervalMs = 100;
constexpr UINT kClipboardRetryIntervalMs = 30;
constexpr int kClipboardRetryLimit = 5;
constexpr UINT kSettingsChangedMessage = WM_APP + 20;
constexpr UINT kPreviewGlyphMessage = WM_APP + 21;
constexpr UINT kPreviewCustomGlyphMessage = WM_APP + 22;

HWND FindSettingsWindow() {
    HWND result = nullptr;
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        if (GetPropW(window, kSettingsWindowProperty)) {
            *reinterpret_cast<HWND*>(parameter) = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

void ActivateSettingsWindow(HWND window) {
    ShowWindowAsync(window, SW_RESTORE);
    BringWindowToTop(window);
    SetForegroundWindow(window);
}

}  // namespace

Application* Application::activeInstance_ = nullptr;

Application::~Application() {
    Shutdown();
}

bool Application::Initialize(HINSTANCE instance) {
    instance_ = instance;
    singleInstanceMutex_ = CreateMutexW(nullptr, TRUE, kSingleInstanceName);
    if (!singleInstanceMutex_) {
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleInstanceMutex_);
        singleInstanceMutex_ = nullptr;
        SetLastError(ERROR_ALREADY_EXISTS);
        return false;
    }

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"Copy Pointer Notifier",
        0,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance_,
        this);
    if (!window_) {
        return false;
    }

    if (!overlay_.Initialize(instance_)) {
        return false;
    }
    if (!trayIcon_.Add(window_)) {
        return false;
    }
    if (!AddClipboardFormatListener(window_)) {
        return false;
    }
    activeInstance_ = this;
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, instance_, 0);
    if (!mouseHook_) {
        activeInstance_ = nullptr;
        return false;
    }

    enabled_ = LoadApplicationEnabled();
    ruleEngine_ = ClipboardRuleEngine(LoadClipboardRules());
    overlay_.SetGlyphPosition(LoadGlyphPosition());
    overlay_.SetGlyphScalePercent(LoadGlyphScalePercent());
    overlay_.SetAnimationStyle(LoadAnimationStyle());
    overlay_.SetAnimationSpeed(LoadAnimationSpeed());
    overlay_.SetVisibilityDuration(LoadIndicatorVisibilityDuration());
    lastClipboardSequence_ = GetClipboardSequenceNumber();
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    return true;
}

int Application::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK Application::WindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {
    Application* self = reinterpret_cast<Application*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Application*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    return self ? self->HandleMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK Application::MouseHookProc(
    int code,
    WPARAM wParam,
    LPARAM lParam) {
    if (code == HC_ACTION &&
        wParam == WM_MOUSEMOVE &&
        activeInstance_ &&
        activeInstance_->window_) {
        const auto* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        activeInstance_->overlay_.MoveToCursor(mouse->pt);
    }
    return CallNextHookEx(
        activeInstance_ ? activeInstance_->mouseHook_ : nullptr,
        code,
        wParam,
        lParam);
}

LRESULT Application::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        trayIcon_.Add(window_);
        return 0;
    }

    switch (message) {
        case WM_CLIPBOARDUPDATE:
            HandleClipboardUpdate();
            return 0;
        case WM_TIMER:
            if (wParam == kAnimationTimer) {
                if (!overlay_.Tick()) {
                    KillTimer(window_, kAnimationTimer);
                } else if (overlay_.IsSettled() && !settledTimerApplied_) {
                    SetTimer(window_, kAnimationTimer, kSettledTrackingIntervalMs, nullptr);
                    settledTimerApplied_ = true;
                }
            } else if (wParam == kClipboardRetryTimer) {
                RetryClipboardRead();
            }
            return 0;
        case TrayIcon::kCallbackMessage:
            if (lParam == WM_LBUTTONUP || lParam == NIN_SELECT) {
                OpenSettings();
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                trayIcon_.ShowMenu(enabled_, IsStartWithWindowsEnabled());
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case TrayIcon::kToggleEnabledCommand:
                    ToggleEnabled();
                    break;
                case TrayIcon::kToggleStartupCommand:
                    ToggleStartup();
                    break;
                case TrayIcon::kExitCommand:
                    DestroyWindow(window_);
                    break;
                case TrayIcon::kEditAppearanceCommand:
                    OpenSettings();
                    break;
                default:
                    break;
            }
            return 0;
        case kSettingsChangedMessage:
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            enabled_ = LoadApplicationEnabled();
            if (!enabled_) {
                overlay_.Hide();
                KillTimer(window_, kAnimationTimer);
            }
            overlay_.SetGlyphPosition(LoadGlyphPosition());
            overlay_.SetGlyphScalePercent(LoadGlyphScalePercent());
            overlay_.SetAnimationStyle(LoadAnimationStyle());
            overlay_.SetAnimationSpeed(LoadAnimationSpeed());
            overlay_.SetVisibilityDuration(LoadIndicatorVisibilityDuration());
            ruleEngine_ = ClipboardRuleEngine(LoadClipboardRules());
            overlay_.RefreshCursorSettings();
            return 0;
        case kPreviewGlyphMessage:
            if (wParam <= static_cast<WPARAM>(ClipboardContentType::Object)) {
                ShowGlyph({
                    static_cast<ClipboardContentType>(wParam),
                    {}});
            }
            return 0;
        case kPreviewCustomGlyphMessage:
            for (const auto& rule : ruleEngine_.Rules()) {
                if (rule.id == static_cast<std::uint64_t>(wParam)) {
                    ShowGlyph({ClipboardContentType::Object, rule.glyph});
                    break;
                }
            }
            return 0;
        case WM_DESTROY:
            Shutdown();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window_, message, wParam, lParam);
    }
}

void Application::HandleClipboardUpdate() {
    const DWORD sequence = GetClipboardSequenceNumber();
    if (sequence == 0 || sequence == lastClipboardSequence_) {
        return;
    }
    lastClipboardSequence_ = sequence;

    if (!enabled_) {
        return;
    }

    clipboardRetryCount_ = 0;
    ShowClipboardBadge();
}

void Application::RetryClipboardRead() {
    if (!enabled_) {
        KillTimer(window_, kClipboardRetryTimer);
        return;
    }

    ShowClipboardBadge();
}

void Application::ShowClipboardBadge() {
    const ClipboardReadResult result = ReadClipboardContentType(window_);
    if (!result.clipboardOpened) {
        ++clipboardRetryCount_;
        if (clipboardRetryCount_ >= kClipboardRetryLimit) {
            KillTimer(window_, kClipboardRetryTimer);
        } else {
            SetTimer(window_, kClipboardRetryTimer, kClipboardRetryIntervalMs, nullptr);
        }
        return;
    }

    KillTimer(window_, kClipboardRetryTimer);
    clipboardRetryCount_ = 0;
    if (result.formats.empty()) {
        overlay_.Hide();
        return;
    }

    ShowGlyph(ruleEngine_.Evaluate(result.formats));
}

void Application::ShowGlyph(ClipboardIndicator indicator) {
    overlay_.Show(std::move(indicator));
    settledTimerApplied_ = false;
    SetTimer(window_, kAnimationTimer, kAnimationIntervalMs, nullptr);
    overlay_.Tick();
}

void Application::ToggleEnabled() {
    enabled_ = !enabled_;
    if (!SaveApplicationEnabled(enabled_)) {
        enabled_ = !enabled_;
        MessageBoxW(
            window_,
            L"Could not save the enabled setting.",
            L"Copy Pointer Notifier",
            MB_OK | MB_ICONERROR);
        return;
    }

    if (!enabled_) {
        KillTimer(window_, kAnimationTimer);
        KillTimer(window_, kClipboardRetryTimer);
        settledTimerApplied_ = false;
        overlay_.Hide();
    }
}

void Application::ToggleStartup() {
    const bool desired = !IsStartWithWindowsEnabled();
    if (!SetStartWithWindowsEnabled(desired)) {
        MessageBoxW(
            window_,
            L"Could not update the Start with Windows setting.",
            L"Copy Pointer Notifier",
            MB_OK | MB_ICONERROR);
    }
}

void Application::OpenSettings() {
    if (const HWND settingsWindow = FindSettingsWindow()) {
        ActivateSettingsWindow(settingsWindow);
        return;
    }

    std::wstring executablePath(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(
        nullptr,
        executablePath.data(),
        static_cast<DWORD>(executablePath.size()));
    if (length == 0 || length == executablePath.size()) {
        MessageBoxW(
            window_,
            L"Could not locate the settings application.",
            L"Copy Pointer Notifier",
            MB_OK | MB_ICONERROR);
        return;
    }
    executablePath.resize(length);
    const std::filesystem::path applicationPath{executablePath};
    const std::filesystem::path settingsPath =
        applicationPath.parent_path() /
        L"settings" /
        L"CopyPointerNotifier.Settings.exe";

    const HINSTANCE result = ShellExecuteW(
        window_,
        L"open",
        settingsPath.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(
            window_,
            L"The WinUI 3 settings application could not be opened.",
            L"Copy Pointer Notifier",
            MB_OK | MB_ICONERROR);
    }
}

void Application::Shutdown() {
    if (shuttingDown_) {
        return;
    }
    shuttingDown_ = true;

    if (window_) {
        KillTimer(window_, kAnimationTimer);
        KillTimer(window_, kClipboardRetryTimer);
        RemoveClipboardFormatListener(window_);
    }
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    if (activeInstance_ == this) {
        activeInstance_ = nullptr;
    }
    overlay_.Hide();
    trayIcon_.Remove();

    if (singleInstanceMutex_) {
        ReleaseMutex(singleInstanceMutex_);
        CloseHandle(singleInstanceMutex_);
        singleInstanceMutex_ = nullptr;
    }
}

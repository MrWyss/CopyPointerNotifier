#pragma once

#include "overlay_window.hpp"
#include "tray_icon.hpp"

#include <windows.h>

class Application {
public:
    ~Application();

    bool Initialize(HINSTANCE instance);
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void HandleClipboardUpdate();
    void RetryClipboardRead();
    void ShowClipboardBadge();
    void ShowGlyph(ClipboardContentType contentType);
    void ToggleEnabled();
    void ToggleStartup();
    void OpenSettings();
    void Shutdown();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HANDLE singleInstanceMutex_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    OverlayWindow overlay_;
    TrayIcon trayIcon_;
    bool enabled_ = true;
    bool shuttingDown_ = false;
    bool settledTimerApplied_ = false;
    DWORD lastClipboardSequence_ = 0;
    int clipboardRetryCount_ = 0;
    UINT taskbarCreatedMessage_ = 0;

    static Application* activeInstance_;
};

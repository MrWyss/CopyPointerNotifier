#include "tray_icon.hpp"

#include "../resource.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <iterator>
#include <string>

namespace {

constexpr UINT kTrayIconId = 1;
constexpr wchar_t kPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

std::wstring LoadUiString(UINT id) {
    wchar_t value[128]{};
    const int length = LoadStringW(
        GetModuleHandleW(nullptr),
        id,
        value,
        static_cast<int>(std::size(value)));
    return length > 0 ? std::wstring(value, length) : std::wstring();
}

enum class PreferredAppMode {
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
};

bool IsSystemAppThemeDark() {
    DWORD useLightTheme = 1;
    DWORD size = sizeof(useLightTheme);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            kPersonalizeKey,
            L"AppsUseLightTheme",
            RRF_RT_REG_DWORD,
            nullptr,
            &useLightTheme,
            &size) != ERROR_SUCCESS) {
        return false;
    }
    return useLightTheme == 0;
}

void ApplySystemMenuTheme(HWND owner) {
    HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (!uxtheme) {
        uxtheme = LoadLibraryW(L"uxtheme.dll");
    }
    if (uxtheme) {
        using SetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);
        using FlushMenuThemes = void(WINAPI*)();
        auto setPreferredAppMode = reinterpret_cast<SetPreferredAppMode>(
            GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
        auto flushMenuThemes = reinterpret_cast<FlushMenuThemes>(
            GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
        if (setPreferredAppMode) {
            setPreferredAppMode(
                IsSystemAppThemeDark()
                    ? PreferredAppMode::ForceDark
                    : PreferredAppMode::ForceLight);
        }
        if (flushMenuThemes) {
            flushMenuThemes();
        }
    }

    BOOL dark = IsSystemAppThemeDark();
    DwmSetWindowAttribute(
        owner,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark,
        sizeof(dark));
}

UINT GetTaskbarDpi() {
    if (HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr)) {
        if (const UINT dpi = GetDpiForWindow(taskbar); dpi != 0) {
            return dpi;
        }
    }
    const UINT dpi = GetDpiForSystem();
    return dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
}

}  // namespace

bool TrayIcon::Add(HWND owner) {
    owner_ = owner;
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = owner_;
    icon.uID = kTrayIconId;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = kCallbackMessage;
    const UINT taskbarDpi = GetTaskbarDpi();
    icon.hIcon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXSMICON, taskbarDpi),
        GetSystemMetricsForDpi(SM_CYSMICON, taskbarDpi),
        LR_DEFAULTCOLOR));
    if (!icon.hIcon) {
        return false;
    }
    const std::wstring appName = LoadUiString(IDS_APP_NAME);
    wcscpy_s(icon.szTip, appName.c_str());
    return Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}

void TrayIcon::Remove() {
    if (!owner_) {
        return;
    }
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = owner_;
    icon.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    owner_ = nullptr;
}

void TrayIcon::ShowMenu(bool enabled, bool startWithWindows) const {
    ApplySystemMenuTheme(owner_);

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    const std::wstring enabledText = LoadUiString(IDS_TRAY_ENABLED);
    const std::wstring startupText = LoadUiString(IDS_TRAY_START_WITH_WINDOWS);
    const std::wstring settingsText = LoadUiString(IDS_TRAY_SETTINGS);
    const std::wstring exitText = LoadUiString(IDS_TRAY_EXIT);
    AppendMenuW(
        menu,
        MF_STRING | (enabled ? MF_CHECKED : MF_UNCHECKED),
        kToggleEnabledCommand,
        enabledText.c_str());
    AppendMenuW(
        menu,
        MF_STRING | (startWithWindows ? MF_CHECKED : MF_UNCHECKED),
        kToggleStartupCommand,
        startupText.c_str());
    AppendMenuW(menu, MF_STRING, kEditAppearanceCommand, settingsText.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitCommand, exitText.c_str());

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(owner_);
    const UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        cursor.x,
        cursor.y,
        0,
        owner_,
        nullptr);
    DestroyMenu(menu);
    if (command != 0) {
        PostMessageW(owner_, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    }
}

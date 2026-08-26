#pragma once

#include <windows.h>

class TrayIcon {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 1;
    static constexpr UINT kToggleEnabledCommand = 1001;
    static constexpr UINT kToggleStartupCommand = 1002;
    static constexpr UINT kExitCommand = 1003;
    static constexpr UINT kEditAppearanceCommand = 1004;

    bool Add(HWND owner);
    void Remove();
    void ShowMenu(bool enabled, bool startWithWindows) const;

private:
    HWND owner_ = nullptr;
};

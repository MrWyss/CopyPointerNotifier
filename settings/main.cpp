#include "pch.h"
#include "App.xaml.h"
#include <MddBootstrap.h>

using namespace winrt;
using namespace winrt::CopyPointerNotifier_Settings::implementation;

namespace
{
    constexpr wchar_t SettingsMutexName[] =
        L"Local\\MrWyss.CopyPointerNotifier.Settings";
    constexpr wchar_t SettingsWindowProperty[] =
        L"MrWyss.CopyPointerNotifier.SettingsWindow";

    HWND FindSettingsWindow()
    {
        HWND result = nullptr;
        EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
            if (GetPropW(window, SettingsWindowProperty))
            {
                *reinterpret_cast<HWND*>(parameter) = window;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&result));
        return result;
    }

    void ActivateExistingSettingsWindow()
    {
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            if (HWND window = FindSettingsWindow())
            {
                ShowWindowAsync(window, SW_RESTORE);
                BringWindowToTop(window);
                SetForegroundWindow(window);
                return;
            }
            Sleep(50);
        }
    }
}

int WINAPI wWinMain(
    [[maybe_unused]] HINSTANCE hInstance,
    [[maybe_unused]] HINSTANCE hPrevInstance,
    [[maybe_unused]] PWSTR pCmdLine,
    [[maybe_unused]] int nCmdShow)
{
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, SettingsMutexName);
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        ActivateExistingSettingsWindow();
        CloseHandle(instanceMutex);
        return 0;
    }

    // Bootstrap the Windows App SDK for unpackaged apps
    const UINT32 majorMinorVersion{ 0x00010008 }; // 1.8
    PCWSTR versionTag{ L"" };
    const PACKAGE_VERSION minVersion{};
    HRESULT hr = MddBootstrapInitialize2(
        majorMinorVersion,
        versionTag,
        minVersion,
        MddBootstrapInitializeOptions_OnPackageIdentity_NOOP);
    if (FAILED(hr))
    {
        if (instanceMutex) CloseHandle(instanceMutex);
        return static_cast<int>(hr);
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    ::Microsoft::UI::Xaml::Application::Start(
        [](auto&&) {
            ::winrt::make<App>();
        });

    MddBootstrapShutdown();
    if (instanceMutex) CloseHandle(instanceMutex);
    return 0;
}

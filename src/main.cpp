#include "application.hpp"

#include <windows.h>
#include <winrt/base.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    Application application;
    if (!application.Initialize(instance)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            MessageBoxW(
                nullptr,
                L"Copy Pointer Notifier could not start.",
                L"Copy Pointer Notifier",
                MB_OK | MB_ICONERROR);
        }
        return 1;
    }

    return application.Run();
}

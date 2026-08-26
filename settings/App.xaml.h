#pragma once
#include "App.g.h"

namespace winrt::CopyPointerNotifier_Settings::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

        Microsoft::UI::Xaml::Window MainWindow() const { return m_window; }

    private:
        Microsoft::UI::Xaml::Window m_window{ nullptr };
    };
}

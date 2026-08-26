#pragma once
#include "MainWindow.g.h"

namespace winrt::CopyPointerNotifier_Settings::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

    private:
        void ExpandToFitContent(double scale);
    };
}

namespace winrt::CopyPointerNotifier_Settings::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}

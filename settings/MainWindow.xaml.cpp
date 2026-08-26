#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#if __has_include("MainWindow.xaml.g.hpp")
#include "MainWindow.xaml.g.hpp"
#endif

#include "MainPage.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml::Interop;
using namespace Windows::Graphics;

namespace winrt::CopyPointerNotifier_Settings::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader resources;
        hstring title = resources.GetString(L"SettingsTitle");
        Title(title);
        AppTitleBar().Title(title);

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        HWND hwnd = nullptr;
        if (auto windowNative = this->try_as<IWindowNative>())
        {
            check_hresult(windowNative->get_WindowHandle(&hwnd));
            if (hwnd)
            {
                SetPropW(
                    hwnd,
                    L"MrWyss.CopyPointerNotifier.SettingsWindow",
                    reinterpret_cast<HANDLE>(1));
            }
        }

        wchar_t modulePath[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath)) != 0)
        {
            const auto assetsDir =
                std::filesystem::path{ modulePath }.parent_path() / L"Assets";

            const auto iconPath = assetsDir / L"AppIcon.ico";
            if (std::filesystem::exists(iconPath))
            {
                AppWindow().SetIcon(iconPath.wstring());
            }

            const auto titleIconPath = assetsDir / L"AppIcon.png";
            if (std::filesystem::exists(titleIconPath))
            {
                winrt::Windows::Foundation::Uri iconUri{
                    L"file:///" + titleIconPath.generic_wstring() };
                Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap{};
                bitmap.UriSource(iconUri);
                Microsoft::UI::Xaml::Controls::ImageIconSource titleIcon{};
                titleIcon.ImageSource(bitmap);
                AppTitleBar().IconSource(titleIcon);
            }
        }

        const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : USER_DEFAULT_SCREEN_DPI;
        const double scale =
            static_cast<double>(dpi) / USER_DEFAULT_SCREEN_DPI;
        int widthPx = static_cast<int>(std::lround(390 * scale));
        int heightPx = static_cast<int>(std::lround(920 * scale));

        auto displayArea = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(
            AppWindow().Id(),
            Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
        if (displayArea)
        {
            const auto workArea = displayArea.WorkArea();
            const int margin = static_cast<int>(std::lround(24 * scale));
            widthPx = (std::min)(widthPx, workArea.Width);
            heightPx = (std::min)(heightPx, workArea.Height - margin);
            AppWindow().Resize(SizeInt32{ widthPx, heightPx });

            const int x =
                workArea.X + (std::max)(0, (workArea.Width - widthPx) / 2);
            const int y =
                workArea.Y + (std::max)(margin, (workArea.Height - heightPx) / 2);
            AppWindow().Move(PointInt32{ x, y });
        }
        else
        {
            AppWindow().Resize(SizeInt32{ widthPx, heightPx });
        }

        if (auto presenter = AppWindow().Presenter().try_as<OverlappedPresenter>())
        {
            presenter.IsMaximizable(false);
        }

        winrt::Windows::UI::Xaml::Interop::TypeName pageType;
        pageType.Name = winrt::name_of<CopyPointerNotifier_Settings::MainPage>();
        pageType.Kind = winrt::Windows::UI::Xaml::Interop::TypeKind::Metadata;
        RootFrame().Navigate(pageType);
        RootFrame().Loaded(
            [this, scale](
                [[maybe_unused]] IInspectable const&,
                [[maybe_unused]] RoutedEventArgs const&)
            {
                ExpandToFitContent(scale);
            });
    }

    void MainWindow::ExpandToFitContent(double scale)
    {
        auto page = RootFrame().Content().try_as<FrameworkElement>();
        if (!page)
        {
            return;
        }

        auto scrollViewer = page.FindName(L"SettingsScrollViewer")
            .try_as<Controls::ScrollViewer>();
        if (!scrollViewer || scrollViewer.ScrollableHeight() <= 0)
        {
            return;
        }

        const auto currentSize = AppWindow().Size();
        const int additionalHeightPx = static_cast<int>(std::ceil(
            (scrollViewer.ScrollableHeight() + 1.0) * scale));
        int heightPx = currentSize.Height + additionalHeightPx;

        auto displayArea = DisplayArea::GetFromWindowId(
            AppWindow().Id(),
            DisplayAreaFallback::Nearest);
        if (!displayArea)
        {
            AppWindow().Resize(
                SizeInt32{ currentSize.Width, heightPx });
            return;
        }

        const auto workArea = displayArea.WorkArea();
        const int margin = static_cast<int>(std::lround(24 * scale));
        heightPx = (std::min)(heightPx, workArea.Height - margin);
        if (heightPx <= currentSize.Height)
        {
            return;
        }

        AppWindow().Resize(SizeInt32{ currentSize.Width, heightPx });
        const int x =
            workArea.X +
            (std::max)(0, (workArea.Width - currentSize.Width) / 2);
        const int y =
            workArea.Y +
            (std::max)(margin, (workArea.Height - heightPx) / 2);
        AppWindow().Move(PointInt32{ x, y });
    }
}

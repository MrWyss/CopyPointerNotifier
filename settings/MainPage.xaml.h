#pragma once
#include "MainPage.g.h"

namespace winrt::CopyPointerNotifier_Settings::implementation
{
    struct MainPage : MainPageT<MainPage>
    {
        MainPage();

        void Page_Loaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PositionPad_SizeChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void PositionPad_PointerPressed(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void PositionPad_PointerMoved(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void PositionPad_PointerReleased(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void SizeSlider_ValueChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void AnimationComboBox_SelectionChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void AnimationSpeedSlider_ValueChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void VisibilityComboBox_SelectionChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void VisibilityDurationSlider_ValueChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void Reset_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Close_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PreviewGlyph_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PointerSettings_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Backup_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Restore_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        static constexpr int MinimumPosition = -125;
        static constexpr int MaximumPosition = 125;
        static constexpr int DefaultX = 30;
        static constexpr int DefaultY = 50;
        static constexpr int DefaultScale = 50;
        static constexpr int DefaultAnimationStyle = 0;
        static constexpr int DefaultAnimationSpeed = 50;
        static constexpr int DefaultVisibilityMode = 0;
        static constexpr int DefaultVisibilityDurationSeconds = 5;

        static constexpr wchar_t PreferencesKey[] = L"Software\\MrWyss\\CopyPointerNotifier";
        static constexpr wchar_t StartupKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        static constexpr wchar_t StartupValueName[] = L"CopyPointerNotifier";
        static constexpr wchar_t StartupTaskId[] = L"CopyPointerNotifierStartup";
        static constexpr wchar_t NativeWindowClass[] = L"CopyPointerNotifier.MessageWindow";
        static constexpr uint32_t SettingsChangedMessage = 0x8014; // WM_APP + 20
        static constexpr uint32_t PreviewGlyphMessage = 0x8015;   // WM_APP + 21

        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader m_resources;
        bool m_loaded{ false };
        bool m_dragging{ false };
        uint32_t m_pointerId{ 0 };
        int m_xPercent{ DefaultX };
        int m_yPercent{ DefaultY };

        struct BackupSettings
        {
            bool appEnabled;
            bool startWithWindows;
            int xPercent;
            int yPercent;
            int scalePercent;
            int animationStyle;
            int animationSpeed;
            int visibilityMode;
            int visibilityDurationSeconds;
        };

        void UpdatePosition(Windows::Foundation::Point point);
        void UpdatePositionMarker();
        void UpdateLabels();
        void UpdateVisibilityControls();
        void SaveSettings();
        void NotifyNative(uint32_t message, uintptr_t value = 0);

        static int ReadDword(HKEY key, const wchar_t* name, int fallback);
        static int ToPercent(double normalized);
        static double FromPercent(int percent);

        HWND GetWindowHandle();
        void ShowError(hstring title, hstring message) noexcept;
        winrt::fire_and_forget BackupAsync();
        winrt::fire_and_forget RestoreAsync();
        Windows::Foundation::IAsyncOperation<bool> ReadStartWithWindowsAsync();
        Windows::Foundation::IAsyncAction WriteStartWithWindowsAsync(bool enabled);

        static bool HasPackageIdentity();
        std::string CreateBackupJson(bool startWithWindows);
        BackupSettings ParseBackupJson(const std::string& json);
        void ApplyBackupSettings(const BackupSettings& settings);
    };
}

namespace winrt::CopyPointerNotifier_Settings::factory_implementation
{
    struct MainPage : MainPageT<MainPage, implementation::MainPage>
    {
    };
}

#include "pch.h"
#include "MainPage.xaml.h"
#include "../src/clipboard_classifier.hpp"
#if __has_include("MainPage.g.cpp")
#include "MainPage.g.cpp"
#endif
#if __has_include("MainPage.xaml.g.hpp")
#include "MainPage.xaml.g.hpp"
#endif

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Shapes;

namespace winrt::CopyPointerNotifier_Settings::implementation
{
    namespace
    {
        void CopyTextToClipboard(HWND owner, std::wstring_view text)
        {
            constexpr int maxAttempts = 10;
            constexpr DWORD retryDelayMs = 20;

            bool opened = false;
            for (int attempt = 0; attempt < maxAttempts; ++attempt)
            {
                if (OpenClipboard(owner))
                {
                    opened = true;
                    break;
                }
                Sleep(retryDelayMs);
            }
            if (!opened)
            {
                throw hresult_error(
                    HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED),
                    L"The clipboard is busy.");
            }

            struct ClipboardCloser
            {
                ~ClipboardCloser()
                {
                    CloseClipboard();
                }
            } closer;

            check_bool(EmptyClipboard());

            const size_t byteCount = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byteCount);
            if (!memory)
            {
                throw_last_error();
            }

            void* buffer = GlobalLock(memory);
            if (!buffer)
            {
                GlobalFree(memory);
                throw_last_error();
            }

            memcpy(buffer, text.data(), text.size() * sizeof(wchar_t));
            static_cast<wchar_t*>(buffer)[text.size()] = L'\0';
            GlobalUnlock(memory);

            if (!SetClipboardData(CF_UNICODETEXT, memory))
            {
                GlobalFree(memory);
                throw_last_error();
            }
        }

        std::optional<std::filesystem::path> PickJsonFile(
            HWND owner,
            bool save,
            const wchar_t* suggestedName = nullptr)
        {
            wchar_t filePath[MAX_PATH]{};
            if (suggestedName)
            {
                wcscpy_s(filePath, suggestedName);
            }

            constexpr wchar_t filter[] =
                L"JSON files (*.json)\0*.json\0All files (*.*)\0*.*\0";
            OPENFILENAMEW options{ sizeof(options) };
            options.hwndOwner = owner;
            options.lpstrFilter = filter;
            options.lpstrFile = filePath;
            options.nMaxFile = ARRAYSIZE(filePath);
            options.lpstrDefExt = L"json";
            options.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

            const BOOL selected = save
                ? GetSaveFileNameW(&options)
                : GetOpenFileNameW(&options);
            if (selected)
            {
                return std::filesystem::path{ filePath };
            }

            const DWORD error = CommDlgExtendedError();
            if (error != 0)
            {
                throw hresult_error(
                    HRESULT_FROM_WIN32(error),
                    L"The file dialog could not be opened.");
            }
            return std::nullopt;
        }
    }

    MainPage::MainPage()
    {
        InitializeComponent();
        m_clipboardStatusTimer = DispatcherTimer{};
        m_clipboardStatusTimer.Interval(std::chrono::milliseconds{ 250 });
        m_clipboardStatusTimer.Tick(
            { this, &MainPage::ClipboardStatusTimer_Tick });
    }

    // --- Registry helpers ---

    int MainPage::ReadDword(HKEY key, const wchar_t* name, int fallback)
    {
        DWORD value = 0;
        DWORD size = sizeof(DWORD);
        DWORD type = 0;
        LONG result = RegQueryValueExW(key, name, nullptr, &type,
            reinterpret_cast<BYTE*>(&value), &size);
        if (result == ERROR_SUCCESS && type == REG_DWORD)
            return static_cast<int>(value);
        return fallback;
    }

    int MainPage::ToPercent(double normalized)
    {
        return static_cast<int>(std::round(
            MinimumPosition + normalized * (MaximumPosition - MinimumPosition)));
    }

    double MainPage::FromPercent(int percent)
    {
        return (percent - MinimumPosition) /
            static_cast<double>(MaximumPosition - MinimumPosition);
    }

    HWND MainPage::GetWindowHandle()
    {
        struct WindowSearch
        {
            DWORD processId;
            HWND result;
        } search{ GetCurrentProcessId(), nullptr };

        EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
            auto* search = reinterpret_cast<WindowSearch*>(parameter);
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            if (processId == search->processId &&
                GetPropW(window, L"MrWyss.CopyPointerNotifier.SettingsWindow"))
            {
                search->result = window;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));

        return search.result;
    }

    // --- Page lifecycle ---

    void MainPage::Page_Loaded([[maybe_unused]] IInspectable const&, [[maybe_unused]] RoutedEventArgs const&)
    {
        HKEY key = nullptr;
        RegOpenKeyExW(HKEY_CURRENT_USER, PreferencesKey, 0, KEY_READ, &key);

        m_xPercent = ReadDword(key, L"GlyphXPercent", DefaultX);
        m_yPercent = ReadDword(key, L"GlyphYPercent", DefaultY);
        SizeSlider().Value(static_cast<double>(std::clamp(
            ReadDword(key, L"GlyphScalePercent", DefaultScale), 0, 100)));
        AnimationComboBox().SelectedIndex(std::clamp(
            ReadDword(key, L"AnimationStyle", DefaultAnimationStyle), 0, 3));
        AnimationSpeedSlider().Value(static_cast<double>(std::clamp(
            ReadDword(key, L"AnimationSpeed", DefaultAnimationSpeed), 0, 100)));
        VisibilityComboBox().SelectedIndex(std::clamp(
            ReadDword(key, L"IndicatorVisibilityMode", DefaultVisibilityMode), 0, 1));
        VisibilityDurationSlider().Value(static_cast<double>(std::clamp(
            ReadDword(key, L"IndicatorDurationSeconds", DefaultVisibilityDurationSeconds), 1, 60)));

        if (key) RegCloseKey(key);

        m_loaded = true;
        UpdatePositionMarker();
        UpdateLabels();
        m_lastClipboardSequence = 0;
        UpdateClipboardStatus();
        m_clipboardStatusTimer.Start();
    }

    void MainPage::Page_Unloaded(
        [[maybe_unused]] IInspectable const&,
        [[maybe_unused]] RoutedEventArgs const&)
    {
        m_clipboardStatusTimer.Stop();
    }

    // --- Position pad ---

    void MainPage::PositionPad_SizeChanged([[maybe_unused]] IInspectable const&, [[maybe_unused]] SizeChangedEventArgs const&)
    {
        UpdatePositionMarker();
    }

    void MainPage::PositionPad_PointerPressed([[maybe_unused]] IInspectable const&, PointerRoutedEventArgs const& e)
    {
        m_dragging = true;
        m_pointerId = e.Pointer().PointerId();
        PositionPad().CapturePointer(e.Pointer());
        UpdatePosition(e.GetCurrentPoint(PositionPad()).Position());
        e.Handled(true);
    }

    void MainPage::PositionPad_PointerMoved([[maybe_unused]] IInspectable const&, PointerRoutedEventArgs const& e)
    {
        if (!m_dragging || e.Pointer().PointerId() != m_pointerId)
            return;
        UpdatePosition(e.GetCurrentPoint(PositionPad()).Position());
        e.Handled(true);
    }

    void MainPage::PositionPad_PointerReleased([[maybe_unused]] IInspectable const&, PointerRoutedEventArgs const& e)
    {
        if (m_dragging && e.Pointer().PointerId() == m_pointerId)
        {
            UpdatePosition(e.GetCurrentPoint(PositionPad()).Position());
            PositionPad().ReleasePointerCapture(e.Pointer());
            m_dragging = false;
            e.Handled(true);
        }
    }

    void MainPage::UpdatePosition(Point point)
    {
        double width = (std::max)(1.0, PositionPad().ActualWidth());
        double height = (std::max)(1.0, PositionPad().ActualHeight());
        m_xPercent = ToPercent(std::clamp(static_cast<double>(point.X), 0.0, width) / width);
        m_yPercent = ToPercent(std::clamp(static_cast<double>(point.Y), 0.0, height) / height);
        SaveSettings();
        UpdatePositionMarker();
        UpdateLabels();
    }

    void MainPage::UpdatePositionMarker()
    {
        if (!m_loaded) return;
        double x = FromPercent(m_xPercent) * PositionPad().ActualWidth();
        double y = FromPercent(m_yPercent) * PositionPad().ActualHeight();
        Microsoft::UI::Xaml::Controls::Canvas::SetLeft(PositionMarker(),
            x - PositionMarker().Width() / 2);
        Microsoft::UI::Xaml::Controls::Canvas::SetTop(PositionMarker(),
            y - PositionMarker().Height() / 2);
    }

    // --- Controls ---

    void MainPage::SizeSlider_ValueChanged([[maybe_unused]] IInspectable const&, [[maybe_unused]] RangeBaseValueChangedEventArgs const&)
    {
        if (!m_loaded) return;
        SaveSettings();
        UpdateLabels();
    }

    void MainPage::AnimationComboBox_SelectionChanged([[maybe_unused]] IInspectable const&, [[maybe_unused]] SelectionChangedEventArgs const&)
    {
        if (!m_loaded) return;
        SaveSettings();
    }

    void MainPage::AnimationSpeedSlider_ValueChanged([[maybe_unused]] IInspectable const&, [[maybe_unused]] RangeBaseValueChangedEventArgs const&)
    {
        if (!m_loaded) return;
        SaveSettings();
        UpdateLabels();
    }

    void MainPage::VisibilityComboBox_SelectionChanged([[maybe_unused]] IInspectable const&, [[maybe_unused]] SelectionChangedEventArgs const&)
    {
        UpdateVisibilityControls();
        if (m_loaded) SaveSettings();
    }

    void MainPage::VisibilityDurationSlider_ValueChanged([[maybe_unused]] IInspectable const&, [[maybe_unused]] RangeBaseValueChangedEventArgs const&)
    {
        if (!m_loaded) return;
        SaveSettings();
        UpdateLabels();
    }

    // --- Labels ---

    void MainPage::UpdateLabels()
    {
        PositionValueText().Text(
            L"X " + to_hstring(m_xPercent) + L"%   Y " + to_hstring(m_yPercent) + L"%");
        SizeValueText().Text(
            to_hstring(static_cast<int>(std::round(SizeSlider().Value()))) + L"%");
        AnimationSpeedValueText().Text(
            to_hstring(static_cast<int>(std::round(AnimationSpeedSlider().Value()))) + L"%");

        hstring secondsFmt = m_resources.GetString(L"SecondsValue");
        int seconds = static_cast<int>(std::round(VisibilityDurationSlider().Value()));
        // Simple format: replace {0:0} or {0} with the number
        std::wstring fmt{ secondsFmt };
        auto pos = fmt.find(L"{0:0}");
        if (pos != std::wstring::npos)
            fmt.replace(pos, 5, std::to_wstring(seconds));
        else if ((pos = fmt.find(L"{0}")) != std::wstring::npos)
            fmt.replace(pos, 3, std::to_wstring(seconds));
        VisibilityDurationValueText().Text(hstring{ fmt });

        UpdateVisibilityControls();
    }

    void MainPage::UpdateVisibilityControls()
    {
        bool isTimed = VisibilityComboBox().SelectedIndex() == 1;
        VisibilityDurationSlider().IsEnabled(isTimed);
        VisibilityDurationValueText().Opacity(isTimed ? 1.0 : 0.5);
    }

    void MainPage::ClipboardStatusTimer_Tick(
        [[maybe_unused]] IInspectable const&,
        [[maybe_unused]] IInspectable const&)
    {
        UpdateClipboardStatus();
    }

    void MainPage::UpdateClipboardStatus()
    {
        const DWORD sequence = GetClipboardSequenceNumber();
        if (sequence != 0 && sequence == m_lastClipboardSequence)
        {
            return;
        }

        const ClipboardReadResult result =
            ReadClipboardContentType(GetWindowHandle());
        if (!result.clipboardOpened)
        {
            m_clipboardDiagnosticText.clear();
            CopyClipboardStatusButton().IsEnabled(false);
            DetectedIndicatorValueText().Text(
                m_resources.GetString(L"ClipboardUnavailable"));
            ClipboardFormatsText().Text(
                m_resources.GetString(L"ClipboardUnavailableDescription"));
            return;
        }

        m_lastClipboardSequence = sequence;

        hstring indicator;
        if (!result.contentType)
        {
            indicator = m_resources.GetString(L"ClipboardIndicatorNone");
        }
        else
        {
            switch (*result.contentType)
            {
                case ClipboardContentType::Image: indicator = L"Image"; break;
                case ClipboardContentType::Files: indicator = L"Files"; break;
                case ClipboardContentType::RichText: indicator = L"RT"; break;
                case ClipboardContentType::Text: indicator = L"T"; break;
                case ClipboardContentType::Object: indicator = L"Object"; break;
            }
        }
        DetectedIndicatorValueText().Text(indicator);

        std::wstring formatList;
        for (const UINT format : result.formats)
        {
            if (!formatList.empty())
            {
                formatList += L"\n";
            }
            formatList += DescribeClipboardFormat(format);
            formatList += L" (";
            formatList += std::to_wstring(format);
            formatList += L")";
        }
        if (formatList.empty())
        {
            formatList = m_resources.GetString(L"ClipboardFormatsEmpty");
        }
        ClipboardFormatsText().Text(hstring{ formatList });

        m_clipboardDiagnosticText =
            std::wstring{ m_resources.GetString(L"ClipboardReportIndicatorLabel") } +
            L": " + std::wstring{ indicator } + L"\n" +
            std::wstring{ m_resources.GetString(L"ClipboardReportFormatsLabel") } +
            L":\n" + formatList;
        CopyClipboardStatusButton().IsEnabled(true);
    }

    // --- Save settings ---

    void MainPage::SaveSettings()
    {
        HKEY key = nullptr;
        RegCreateKeyExW(HKEY_CURRENT_USER, PreferencesKey, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
        if (!key) return;

        auto setDword = [&](const wchar_t* name, int val) {
            DWORD dw = static_cast<DWORD>(val);
            RegSetValueExW(key, name, 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&dw), sizeof(DWORD));
        };

        setDword(L"GlyphXPercent", m_xPercent);
        setDword(L"GlyphYPercent", m_yPercent);
        setDword(L"GlyphScalePercent", static_cast<int>(std::round(SizeSlider().Value())));
        setDword(L"AnimationStyle", AnimationComboBox().SelectedIndex());
        setDword(L"AnimationSpeed", static_cast<int>(std::round(AnimationSpeedSlider().Value())));
        setDword(L"IndicatorVisibilityMode", VisibilityComboBox().SelectedIndex());
        setDword(L"IndicatorDurationSeconds", static_cast<int>(std::round(VisibilityDurationSlider().Value())));

        RegCloseKey(key);
        NotifyNative(SettingsChangedMessage);
    }

    // --- Native IPC ---

    void MainPage::NotifyNative(uint32_t message, uintptr_t value)
    {
        std::pair<uint32_t, uintptr_t> params{ message, value };
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            wchar_t className[128]{};
            GetClassNameW(hwnd, className, 128);
            if (std::wstring_view{ className } != NativeWindowClass)
                return TRUE;
            auto* params = reinterpret_cast<std::pair<uint32_t, uintptr_t>*>(lParam);
            PostMessageW(hwnd, params->first, static_cast<WPARAM>(params->second), 0);
            return FALSE;
        }, reinterpret_cast<LPARAM>(&params));
    }

    // --- Actions ---

    void MainPage::Reset_Click([[maybe_unused]] IInspectable const&, [[maybe_unused]] RoutedEventArgs const&)
    {
        m_xPercent = DefaultX;
        m_yPercent = DefaultY;
        SizeSlider().Value(DefaultScale);
        AnimationComboBox().SelectedIndex(DefaultAnimationStyle);
        AnimationSpeedSlider().Value(DefaultAnimationSpeed);
        VisibilityComboBox().SelectedIndex(DefaultVisibilityMode);
        VisibilityDurationSlider().Value(DefaultVisibilityDurationSeconds);
        SaveSettings();
        UpdatePositionMarker();
        UpdateLabels();
    }

    void MainPage::Close_Click([[maybe_unused]] IInspectable const&, [[maybe_unused]] RoutedEventArgs const&)
    {
        if (HWND window = GetWindowHandle())
        {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }

    void MainPage::PreviewGlyph_Click(IInspectable const& sender, [[maybe_unused]] RoutedEventArgs const&)
    {
        auto button = sender.try_as<Controls::Button>();
        if (!button) return;
        auto tag = button.Tag();
        if (!tag) return;
        auto tagStr = unbox_value_or<hstring>(tag, L"");
        uint32_t contentType = 0;
        try { contentType = static_cast<uint32_t>(std::stoul(std::wstring(tagStr))); }
        catch (...) { return; }
        NotifyNative(PreviewGlyphMessage, contentType);
    }

    void MainPage::CopyClipboardStatus_Click(
        [[maybe_unused]] IInspectable const&,
        [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_clipboardDiagnosticText.empty())
        {
            return;
        }

        try
        {
            CopyTextToClipboard(
                GetWindowHandle(),
                m_clipboardDiagnosticText);

            // Keep the captured report visible instead of inspecting our own copy.
            m_lastClipboardSequence = GetClipboardSequenceNumber();
        }
        catch (hresult_error const& error)
        {
            const hstring explanation =
                m_resources.GetString(L"ClipboardCopyFailedMessage") +
                L"\n\n" + error.message();
            ShowError(
                m_resources.GetString(L"ClipboardCopyFailedTitle"),
                explanation);
        }
    }

    void MainPage::PointerSettings_Click([[maybe_unused]] IInspectable const&, [[maybe_unused]] RoutedEventArgs const&)
    {
        Windows::System::Launcher::LaunchUriAsync(
            Uri{ L"ms-settings:easeofaccess-mousepointer" });
    }

    void MainPage::Backup_Click([[maybe_unused]] IInspectable const&, [[maybe_unused]] RoutedEventArgs const&)
    {
        BackupAsync();
    }

    void MainPage::Restore_Click([[maybe_unused]] IInspectable const&, [[maybe_unused]] RoutedEventArgs const&)
    {
        RestoreAsync();
    }

    // --- Async operations ---

    void MainPage::ShowError(hstring title, hstring message) noexcept
    {
        MessageBoxW(
            GetWindowHandle(),
            message.c_str(),
            title.c_str(),
            MB_OK | MB_ICONERROR);
    }

    // --- JSON backup/restore (minimal manual JSON) ---

    std::string MainPage::CreateBackupJson(bool startWithWindows)
    {
        HKEY prefKey = nullptr;
        RegOpenKeyExW(HKEY_CURRENT_USER, PreferencesKey, 0, KEY_READ, &prefKey);
        int enabled = ReadDword(prefKey, L"Enabled", 1);
        if (prefKey) RegCloseKey(prefKey);

        std::ostringstream json;
        json << "{\n";
        json << "  \"schemaVersion\": 1,\n";
        json << "  \"app\": {\n";
        json << "    \"enabled\": " << (enabled != 0 ? "true" : "false") << ",\n";
        json << "    \"startWithWindows\": " << (startWithWindows ? "true" : "false") << "\n";
        json << "  },\n";
        json << "  \"indicator\": {\n";
        json << "    \"xPercent\": " << m_xPercent << ",\n";
        json << "    \"yPercent\": " << m_yPercent << ",\n";
        json << "    \"scalePercent\": " << static_cast<int>(std::round(SizeSlider().Value())) << ",\n";
        json << "    \"animationStyle\": " << AnimationComboBox().SelectedIndex() << ",\n";
        json << "    \"animationSpeed\": " << static_cast<int>(std::round(AnimationSpeedSlider().Value())) << ",\n";
        json << "    \"visibilityMode\": " << VisibilityComboBox().SelectedIndex() << ",\n";
        json << "    \"visibilityDurationSeconds\": " << static_cast<int>(std::round(VisibilityDurationSlider().Value())) << "\n";
        json << "  }\n";
        json << "}";
        return json.str();
    }

    // Minimal JSON parser for the known backup schema
    static std::string GetJsonString(const std::string& json, const std::string& key)
    {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos < json.size() && json[pos] == '"')
        {
            pos++;
            auto end = json.find('"', pos);
            if (end == std::string::npos) return "";
            return json.substr(pos, end - pos);
        }
        return "";
    }

    static int GetJsonInt(const std::string& json, const std::string& key, int fallback)
    {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return fallback;
        pos = json.find(':', pos);
        if (pos == std::string::npos) return fallback;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        // Parse integer (may be negative)
        std::string numStr;
        if (pos < json.size() && json[pos] == '-') { numStr += '-'; pos++; }
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
        {
            numStr += json[pos++];
        }
        if (numStr.empty() || numStr == "-") return fallback;
        try { return std::stoi(numStr); }
        catch (...) { return fallback; }
    }

    static bool GetJsonBool(const std::string& json, const std::string& key, bool fallback)
    {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return fallback;
        pos = json.find(':', pos);
        if (pos == std::string::npos) return fallback;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (json.compare(pos, 4, "true") == 0) return true;
        if (json.compare(pos, 5, "false") == 0) return false;
        return fallback;
    }

    static bool HasJsonKey(const std::string& json, const std::string& key)
    {
        return json.find("\"" + key + "\"") != std::string::npos;
    }

    MainPage::BackupSettings MainPage::ParseBackupJson(const std::string& json)
    {
        int schemaVersion = GetJsonInt(json, "schemaVersion", -1);
        if (schemaVersion != 1)
        {
            hstring msg = m_resources.GetString(L"UnsupportedBackupVersionError");
            std::wstring fmtMsg{ msg };
            auto pos = fmtMsg.find(L"{0}");
            if (pos != std::wstring::npos)
                fmtMsg.replace(pos, 3, std::to_wstring(schemaVersion));
            throw std::invalid_argument(winrt::to_string(hstring{ fmtMsg }));
        }

        if (!HasJsonKey(json, "app") || !HasJsonKey(json, "indicator"))
        {
            throw std::invalid_argument(
                winrt::to_string(m_resources.GetString(L"BackupMissingSettingsError")));
        }

        int xPct = GetJsonInt(json, "xPercent", DefaultX);
        int yPct = GetJsonInt(json, "yPercent", DefaultY);
        int scale = GetJsonInt(json, "scalePercent", DefaultScale);
        int animStyle = GetJsonInt(json, "animationStyle", DefaultAnimationStyle);
        int animSpeed = GetJsonInt(json, "animationSpeed", DefaultAnimationSpeed);
        int visMode = GetJsonInt(json, "visibilityMode", DefaultVisibilityMode);
        int visDur = GetJsonInt(json, "visibilityDurationSeconds", DefaultVisibilityDurationSeconds);

        if (xPct < MinimumPosition || xPct > MaximumPosition ||
            yPct < MinimumPosition || yPct > MaximumPosition ||
            scale < 0 || scale > 100)
        {
            throw std::invalid_argument(
                winrt::to_string(m_resources.GetString(L"BackupInvalidIndicatorError")));
        }
        if (animStyle < 0 || animStyle > 3)
        {
            throw std::invalid_argument(
                winrt::to_string(m_resources.GetString(L"BackupInvalidAnimationError")));
        }
        if (animSpeed < 0 || animSpeed > 100)
        {
            throw std::invalid_argument(
                winrt::to_string(m_resources.GetString(L"BackupInvalidAnimationSpeedError")));
        }
        if (visMode < 0 || visMode > 1 || visDur < 1 || visDur > 60)
        {
            throw std::invalid_argument(
                winrt::to_string(m_resources.GetString(L"BackupInvalidVisibilityError")));
        }

        return BackupSettings{
            GetJsonBool(json, "enabled", true),
            GetJsonBool(json, "startWithWindows", false),
            xPct,
            yPct,
            scale,
            animStyle,
            animSpeed,
            visMode,
            visDur,
        };
    }

    void MainPage::ApplyBackupSettings(const BackupSettings& settings)
    {
        HKEY prefKey = nullptr;
        RegCreateKeyExW(HKEY_CURRENT_USER, PreferencesKey, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &prefKey, nullptr);
        if (prefKey)
        {
            DWORD enabledDw = settings.appEnabled ? 1 : 0;
            RegSetValueExW(prefKey, L"Enabled", 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&enabledDw), sizeof(DWORD));
            RegCloseKey(prefKey);
        }

        m_xPercent = settings.xPercent;
        m_yPercent = settings.yPercent;
        SizeSlider().Value(static_cast<double>(settings.scalePercent));
        AnimationComboBox().SelectedIndex(settings.animationStyle);
        AnimationSpeedSlider().Value(static_cast<double>(settings.animationSpeed));
        VisibilityComboBox().SelectedIndex(settings.visibilityMode);
        VisibilityDurationSlider().Value(
            static_cast<double>(settings.visibilityDurationSeconds));
        SaveSettings();
        UpdatePositionMarker();
        UpdateLabels();
    }

    bool MainPage::HasPackageIdentity()
    {
        UINT32 length = 0;
        return GetCurrentPackageFullName(&length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
    }

    IAsyncOperation<bool> MainPage::ReadStartWithWindowsAsync()
    {
        if (HasPackageIdentity())
        {
            auto task = co_await Windows::ApplicationModel::StartupTask::GetAsync(StartupTaskId);
            auto state = task.State();
            co_return state == Windows::ApplicationModel::StartupTaskState::Enabled ||
                state == Windows::ApplicationModel::StartupTaskState::EnabledByPolicy;
        }

        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, StartupKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
            co_return false;

        DWORD type = 0;
        DWORD size = 0;
        bool enabled =
            RegQueryValueExW(key, StartupValueName, nullptr, &type, nullptr, &size) ==
                ERROR_SUCCESS &&
            type == REG_SZ;
        RegCloseKey(key);
        co_return enabled;
    }

    IAsyncAction MainPage::WriteStartWithWindowsAsync(bool enabled)
    {
        if (HasPackageIdentity())
        {
            auto task = co_await Windows::ApplicationModel::StartupTask::GetAsync(StartupTaskId);
            if (!enabled)
            {
                task.Disable();
                co_return;
            }

            auto state = co_await task.RequestEnableAsync();
            if (state != Windows::ApplicationModel::StartupTaskState::Enabled &&
                state != Windows::ApplicationModel::StartupTaskState::EnabledByPolicy)
            {
                throw hresult_error(E_ACCESSDENIED);
            }
            co_return;
        }

        HKEY key = nullptr;
        check_win32(RegCreateKeyExW(
            HKEY_CURRENT_USER,
            StartupKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr));

        LSTATUS status = ERROR_SUCCESS;
        if (!enabled)
        {
            status = RegDeleteValueW(key, StartupValueName);
            if (status == ERROR_FILE_NOT_FOUND)
                status = ERROR_SUCCESS;
        }
        else
        {
            wchar_t modulePath[MAX_PATH]{};
            DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
            if (length == 0 || length == MAX_PATH)
            {
                RegCloseKey(key);
                throw hresult_error(HRESULT_FROM_WIN32(GetLastError()));
            }

            std::filesystem::path settingsExe{ modulePath };
            auto mainExe = settingsExe.parent_path().parent_path() /
                L"CopyPointerNotifier.exe";
            if (!std::filesystem::exists(mainExe))
            {
                RegCloseKey(key);
                throw std::invalid_argument(
                    winrt::to_string(m_resources.GetString(L"MainApplicationMissingError")));
            }

            std::wstring command = L"\"" + mainExe.wstring() + L"\"";
            status = RegSetValueExW(
                key,
                StartupValueName,
                0,
                REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }

        RegCloseKey(key);
        check_win32(status);
    }

    winrt::fire_and_forget MainPage::BackupAsync()
    {
        auto strong = get_strong();

        try
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t suggestedName[64];
            swprintf_s(suggestedName, L"CopyPointerNotifier-settings-%04d-%02d-%02d.json",
                st.wYear, st.wMonth, st.wDay);

            HWND hwnd = GetWindowHandle();
            if (!hwnd)
            {
                throw hresult_error(E_HANDLE, L"The Settings window is unavailable.");
            }
            auto file = PickJsonFile(hwnd, true, suggestedName);
            if (!file) co_return;

            bool startWithWindows = co_await ReadStartWithWindowsAsync();
            std::string jsonContent = CreateBackupJson(startWithWindows);
            std::ofstream output(*file, std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::failbit | std::ios::badbit);
            output.write(jsonContent.data(), static_cast<std::streamsize>(jsonContent.size()));
        }
        catch (const hresult_error& ex)
        {
            ShowError(m_resources.GetString(L"BackupFailedTitle"),
                ex.message());
        }
        catch (const std::exception& ex)
        {
            ShowError(m_resources.GetString(L"BackupFailedTitle"),
                to_hstring(ex.what()));
        }
        catch (...)
        {
            ShowError(
                m_resources.GetString(L"BackupFailedTitle"),
                L"An unexpected error occurred.");
        }
    }

    winrt::fire_and_forget MainPage::RestoreAsync()
    {
        auto strong = get_strong();

        try
        {
            HWND hwnd = GetWindowHandle();
            if (!hwnd)
            {
                throw hresult_error(E_HANDLE, L"The Settings window is unavailable.");
            }
            auto file = PickJsonFile(hwnd, false);
            if (!file) co_return;

            std::ifstream input(*file, std::ios::binary);
            input.exceptions(std::ios::badbit);
            std::string json{
                std::istreambuf_iterator<char>{ input },
                std::istreambuf_iterator<char>{}
            };
            if (json.empty())
            {
                throw std::invalid_argument(
                    winrt::to_string(m_resources.GetString(L"BackupEmptyError")));
            }
            BackupSettings settings = ParseBackupJson(json);
            co_await WriteStartWithWindowsAsync(settings.startWithWindows);
            ApplyBackupSettings(settings);
        }
        catch (const std::invalid_argument& ex)
        {
            ShowError(m_resources.GetString(L"RestoreFailedTitle"),
                to_hstring(ex.what()));
        }
        catch (const hresult_error& ex)
        {
            ShowError(m_resources.GetString(L"RestoreFailedTitle"),
                ex.message());
        }
        catch (const std::exception& ex)
        {
            ShowError(m_resources.GetString(L"RestoreFailedTitle"),
                to_hstring(ex.what()));
        }
        catch (...)
        {
            ShowError(
                m_resources.GetString(L"RestoreFailedTitle"),
                L"An unexpected error occurred.");
        }
    }
}

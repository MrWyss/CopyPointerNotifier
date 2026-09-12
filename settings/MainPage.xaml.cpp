#include "pch.h"
#include "MainPage.xaml.h"
#include "../src/clipboard_classifier.hpp"
#include "../src/clipboard_rules.hpp"
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
using namespace Windows::Data::Json;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Shapes;

namespace winrt::CopyPointerNotifier_Settings::implementation
{
    namespace
    {
        constexpr wchar_t GitHubUrl[] =
            L"https://github.com/MrWyss/CopyPointerNotifier";

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

        class GdiplusSession
        {
        public:
            GdiplusSession()
            {
                Gdiplus::GdiplusStartupInput input;
                check_hresult(Gdiplus::GdiplusStartup(
                    &m_token, &input, nullptr) == Gdiplus::Ok
                        ? S_OK
                        : E_FAIL);
            }

            ~GdiplusSession()
            {
                if (m_token != 0)
                {
                    Gdiplus::GdiplusShutdown(m_token);
                }
            }

        private:
            ULONG_PTR m_token = 0;
        };

        Media::PathGeometry CreatePathGeometry(
            Gdiplus::GraphicsPath& outline)
        {
            constexpr float visualSize = 24.0F;
            constexpr float padding = 1.0F;

            Gdiplus::RectF bounds;
            if (outline.GetBounds(&bounds) != Gdiplus::Ok ||
                bounds.Width <= 0.0F ||
                bounds.Height <= 0.0F)
            {
                return nullptr;
            }

            const INT pointCount = outline.GetPointCount();
            std::vector<Gdiplus::PointF> points(pointCount);
            std::vector<BYTE> types(pointCount);
            if (outline.GetPathPoints(points.data(), pointCount) !=
                    Gdiplus::Ok ||
                outline.GetPathTypes(types.data(), pointCount) !=
                    Gdiplus::Ok)
            {
                throw hresult_error(E_FAIL, L"The glyph outline could not be read.");
            }

            const float scale = (std::min)(
                (visualSize - padding * 2.0F) / bounds.Width,
                (visualSize - padding * 2.0F) / bounds.Height);
            const float offsetX =
                (visualSize - bounds.Width * scale) / 2.0F;
            const float offsetY =
                (visualSize - bounds.Height * scale) / 2.0F;
            const auto transform = [&](const Gdiplus::PointF& point)
            {
                return Point{
                    (point.X - bounds.X) * scale + offsetX,
                    (point.Y - bounds.Y) * scale + offsetY };
            };

            Media::PathGeometry geometry;
            Media::PathFigure figure{ nullptr };
            for (INT index = 0; index < pointCount; ++index)
            {
                const BYTE type =
                    types[index] & Gdiplus::PathPointTypePathTypeMask;
                const bool closesFigure =
                    (types[index] &
                     Gdiplus::PathPointTypeCloseSubpath) != 0;
                if (type == Gdiplus::PathPointTypeStart)
                {
                    figure = Media::PathFigure{};
                    figure.StartPoint(transform(points[index]));
                    figure.IsFilled(true);
                    geometry.Figures().Append(figure);
                }
                else if (type == Gdiplus::PathPointTypeLine && figure)
                {
                    Media::LineSegment segment;
                    segment.Point(transform(points[index]));
                    figure.Segments().Append(segment);
                }
                else if (type == Gdiplus::PathPointTypeBezier &&
                         figure &&
                         index + 2 < pointCount)
                {
                    Media::BezierSegment segment;
                    segment.Point1(transform(points[index]));
                    segment.Point2(transform(points[index + 1]));
                    segment.Point3(transform(points[index + 2]));
                    figure.Segments().Append(segment);
                    if ((types[index + 2] &
                         Gdiplus::PathPointTypeCloseSubpath) != 0)
                    {
                        figure.IsClosed(true);
                    }
                    index += 2;
                    continue;
                }
                if (closesFigure && figure)
                {
                    figure.IsClosed(true);
                }
            }

            return geometry;
        }

        Media::PathGeometry CreateGlyphGeometry(std::wstring_view glyph)
        {
            static GdiplusSession gdiplus;
            Gdiplus::FontFamily family(GlyphFontFamilyName(glyph));
            Gdiplus::StringFormat format;
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            Gdiplus::GraphicsPath outline;
            if (outline.AddString(
                    glyph.data(),
                    static_cast<INT>(glyph.size()),
                    &family,
                    Gdiplus::FontStyleBold,
                    28.0F,
                    Gdiplus::PointF{},
                    &format) != Gdiplus::Ok)
            {
                return nullptr;
            }
            return CreatePathGeometry(outline);
        }

        Media::PathGeometry CreateBuiltInGeometry(
            ClipboardContentType contentType)
        {
            static GdiplusSession gdiplus;
            Gdiplus::GraphicsPath outline;
            Gdiplus::Pen pen(Gdiplus::Color::Black, 1.8F);
            pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
            pen.SetLineJoin(Gdiplus::LineJoinRound);

            switch (contentType)
            {
                case ClipboardContentType::Image:
                {
                    outline.AddRectangle(Gdiplus::RectF{ 2, 3, 20, 18 });
                    outline.AddEllipse(Gdiplus::RectF{ 15, 6, 3, 3 });
                    const Gdiplus::PointF mountains[]{
                        { 3, 20 }, { 9, 12 }, { 13, 16 },
                        { 17, 12 }, { 22, 19 } };
                    outline.AddLines(mountains, ARRAYSIZE(mountains));
                    break;
                }
                case ClipboardContentType::Files:
                {
                    const Gdiplus::PointF page[]{
                        { 5, 2 }, { 15, 2 }, { 21, 8 },
                        { 21, 22 }, { 5, 22 } };
                    outline.AddPolygon(page, ARRAYSIZE(page));
                    const Gdiplus::PointF fold[]{
                        { 15, 2 }, { 15, 8 }, { 21, 8 } };
                    outline.AddLines(fold, ARRAYSIZE(fold));
                    break;
                }
                case ClipboardContentType::Object:
                {
                    const Gdiplus::PointF top[]{
                        { 12, 2 }, { 22, 7.5F }, { 12, 13 },
                        { 2, 7.5F } };
                    outline.AddPolygon(top, ARRAYSIZE(top));
                    const Gdiplus::PointF left[]{
                        { 2, 7.5F }, { 2, 17 }, { 12, 22 },
                        { 22, 17 }, { 22, 7.5F } };
                    outline.AddLines(left, ARRAYSIZE(left));
                    const Gdiplus::PointF center[]{
                        { 12, 13 }, { 12, 22 } };
                    outline.AddLines(center, ARRAYSIZE(center));
                    break;
                }
                default:
                    return nullptr;
            }

            if (outline.Widen(&pen) != Gdiplus::Ok)
            {
                return nullptr;
            }
            return CreatePathGeometry(outline);
        }

        FrameworkElement CreateGlyphVisual(
            std::wstring_view glyph,
            const wchar_t* brushResource = L"TextFillColorPrimaryBrush")
        {
            const bool useAccentBrush =
                std::wstring_view{ brushResource } ==
                L"AccentFillColorDefaultBrush";
            const auto geometry = CreateGlyphGeometry(glyph);
            if (!geometry)
            {
                const wchar_t* markup = useAccentBrush
                    ? LR"(<TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Foreground="{ThemeResource AccentFillColorDefaultBrush}" />)"
                    : LR"(<TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Foreground="{ThemeResource TextFillColorPrimaryBrush}" />)";
                auto fallback =
                    Markup::XamlReader::Load(markup).as<TextBlock>();
                fallback.Text(hstring{ glyph });
                fallback.IsColorFontEnabled(true);
                fallback.FontSize(20);
                fallback.Width(30);
                return fallback;
            }

            const wchar_t* markup = useAccentBrush
                ? LR"(<Path xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Fill="{ThemeResource AccentFillColorDefaultBrush}" />)"
                : LR"(<Path xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Fill="{ThemeResource TextFillColorPrimaryBrush}" />)";
            auto preview =
                Markup::XamlReader::Load(markup).as<Shapes::Path>();
            preview.Data(geometry);
            preview.Width(30);
            preview.Height(24);
            preview.HorizontalAlignment(HorizontalAlignment::Left);
            preview.VerticalAlignment(VerticalAlignment::Center);
            return preview;
        }

        FrameworkElement CreateBuiltInVisual(ClipboardContentType contentType)
        {
            if (contentType == ClipboardContentType::Text)
            {
                return CreateGlyphVisual(L"T");
            }
            if (contentType == ClipboardContentType::RichText)
            {
                return CreateGlyphVisual(L"RT");
            }

            const wchar_t* markup = nullptr;
            switch (contentType)
            {
                case ClipboardContentType::Image:
                    markup = LR"(
<Viewbox xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Width="24" Height="24" HorizontalAlignment="Left">
  <Canvas Width="24" Height="24">
    <Rectangle Canvas.Left="2" Canvas.Top="3" Width="20" Height="18" RadiusX="1" RadiusY="1" Stroke="{ThemeResource TextFillColorPrimaryBrush}" StrokeThickness="1.8" />
    <Ellipse Canvas.Left="15" Canvas.Top="6" Width="3" Height="3" Stroke="{ThemeResource TextFillColorPrimaryBrush}" StrokeThickness="1.8" />
    <Path Data="M 3,20 L 9,12 L 13,16 L 17,12 L 22,19" Stroke="{ThemeResource TextFillColorPrimaryBrush}" StrokeEndLineCap="Round" StrokeLineJoin="Round" StrokeStartLineCap="Round" StrokeThickness="1.8" />
  </Canvas>
</Viewbox>)";
                    break;
                case ClipboardContentType::Files:
                    markup = LR"(
<Viewbox xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Width="24" Height="24" HorizontalAlignment="Left">
  <Canvas Width="24" Height="24">
    <Path Data="M 5,2 L 15,2 L 21,8 L 21,22 L 5,22 Z M 15,2 L 15,8 L 21,8" Stroke="{ThemeResource TextFillColorPrimaryBrush}" StrokeEndLineCap="Round" StrokeLineJoin="Round" StrokeStartLineCap="Round" StrokeThickness="1.8" />
  </Canvas>
</Viewbox>)";
                    break;
                case ClipboardContentType::Object:
                    markup = LR"(
<Viewbox xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Width="24" Height="24" HorizontalAlignment="Left">
  <Canvas Width="24" Height="24">
    <Path Data="M 12,2 L 22,7.5 L 12,13 L 2,7.5 Z M 2,7.5 L 2,17 L 12,22 L 22,17 L 22,7.5 M 12,13 L 12,22" Stroke="{ThemeResource TextFillColorPrimaryBrush}" StrokeEndLineCap="Round" StrokeLineJoin="Round" StrokeStartLineCap="Round" StrokeThickness="1.8" />
  </Canvas>
</Viewbox>)";
                    break;
                default:
                    return CreateGlyphVisual(L"?");
            }
            return Markup::XamlReader::Load(markup).as<FrameworkElement>();
        }

        IconElement CreateGlyphIcon(std::wstring_view glyph)
        {
            const auto geometry = CreateGlyphGeometry(glyph);
            if (geometry)
            {
                PathIcon icon;
                icon.Data(geometry);
                return icon;
            }

            FontIcon icon;
            icon.Glyph(hstring{ glyph });
            icon.FontFamily(
                Media::FontFamily{ GlyphFontFamilyName(glyph) });
            return icon;
        }

        IconElement CreateBuiltInIcon(ClipboardContentType contentType)
        {
            if (contentType == ClipboardContentType::Text)
            {
                return CreateGlyphIcon(L"T");
            }
            if (contentType == ClipboardContentType::RichText)
            {
                return CreateGlyphIcon(L"RT");
            }

            const auto geometry = CreateBuiltInGeometry(contentType);
            if (geometry)
            {
                PathIcon icon;
                icon.Data(geometry);
                return icon;
            }
            return CreateGlyphIcon(L"?");
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
        m_rules = LoadClipboardRules();
        RefreshRulesList();
        UpdatePositionMarker();
        UpdateLabels();
        UpdateVersionInformation();
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

    void MainPage::SettingsSelectorBar_SelectionChanged(
        SelectorBar const& sender,
        [[maybe_unused]] SelectorBarSelectionChangedEventArgs const&)
    {
        if (!AppearancePage() || !RulesPage() || !AdvancedPage())
        {
            return;
        }
        const auto selected = sender.SelectedItem();
        AppearancePage().Visibility(
            selected == AppearanceSelector() ? Visibility::Visible : Visibility::Collapsed);
        RulesPage().Visibility(
            selected == RulesSelector() ? Visibility::Visible : Visibility::Collapsed);
        AdvancedPage().Visibility(
            selected == AdvancedSelector() ? Visibility::Visible : Visibility::Collapsed);
        SettingsScrollViewer().ChangeView(nullptr, 0.0, nullptr);
    }

    void MainPage::UpdateVersionInformation()
    {
        std::wstring version =
            std::to_wstring(COPY_POINTER_NOTIFIER_VERSION_MAJOR) + L"." +
            std::to_wstring(COPY_POINTER_NOTIFIER_VERSION_MINOR) + L"." +
            std::to_wstring(COPY_POINTER_NOTIFIER_VERSION_BUILD) + L"." +
            std::to_wstring(COPY_POINTER_NOTIFIER_VERSION_REVISION);
        hstring releaseChannel =
            m_resources.GetString(L"DevelopmentBuildLabel");

        if (HasPackageIdentity())
        {
            const auto package = Windows::ApplicationModel::Package::Current();
            const auto packageVersion = package.Id().Version();
            version =
                std::to_wstring(packageVersion.Major) + L"." +
                std::to_wstring(packageVersion.Minor) + L"." +
                std::to_wstring(packageVersion.Build) + L"." +
                std::to_wstring(packageVersion.Revision);
            releaseChannel =
                package.SignatureKind() ==
                    Windows::ApplicationModel::PackageSignatureKind::Store
                ? m_resources.GetString(L"StoreReleaseLabel")
                : m_resources.GetString(L"TestReleaseLabel");
        }

        VersionLink().Content(box_value(
            std::wstring{ m_resources.GetString(L"VersionLabel") } +
            L" " + version));
        VersionLink().NavigateUri(Uri{ GitHubUrl });
        ReleaseChannelText().Text(releaseChannel);
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

    void MainPage::AddRule_Click(
        [[maybe_unused]] IInspectable const&,
        [[maybe_unused]] RoutedEventArgs const&)
    {
        const ClipboardReadResult result =
            ReadClipboardContentType(GetWindowHandle());
        if (!result.clipboardOpened)
        {
            ShowError(
                m_resources.GetString(L"RuleClipboardUnavailableTitle"),
                m_resources.GetString(L"RuleClipboardUnavailableMessage"));
            return;
        }

        std::vector<ClipboardFormatIdentity> formats;
        std::set<std::pair<UINT, std::wstring>> seen;
        std::size_t omitted = 0;
        for (const UINT format : result.formats)
        {
            auto identity = IdentifyClipboardFormat(format);
            if (!identity)
            {
                ++omitted;
                continue;
            }
            if (seen.emplace(
                    identity->standardFormat,
                    identity->registeredName).second)
            {
                formats.push_back(std::move(*identity));
            }
        }
        if (formats.empty())
        {
            ShowError(
                m_resources.GetString(L"RuleNoFormatsTitle"),
                m_resources.GetString(L"RuleNoFormatsMessage"));
            return;
        }
        EditRuleAsync(std::nullopt, std::move(formats), omitted);
    }

    std::uint64_t MainPage::GenerateRuleId() const
    {
        FILETIME time{};
        GetSystemTimeAsFileTime(&time);
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        std::uint64_t id =
            (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) |
            time.dwLowDateTime;
        id ^= static_cast<std::uint64_t>(counter.QuadPart);
        if (id == 0)
        {
            id = 1;
        }
        while (std::ranges::any_of(
            m_rules, [id](const ClipboardRule& rule) { return rule.id == id; }))
        {
            ++id;
            if (id == 0)
            {
                ++id;
            }
        }
        return id;
    }

    bool MainPage::PersistRules(std::vector<ClipboardRule> rules)
    {
        if (!SaveClipboardRules(rules))
        {
            ShowError(
                m_resources.GetString(L"RuleSaveFailedTitle"),
                m_resources.GetString(L"RuleSaveFailedMessage"));
            return false;
        }
        m_rules = std::move(rules);
        RefreshRulesList();
        NotifyNative(SettingsChangedMessage);
        m_lastClipboardSequence = 0;
        UpdateClipboardStatus();
        return true;
    }

    void MainPage::ToggleRule(std::size_t index)
    {
        if (index >= m_rules.size())
        {
            return;
        }
        auto rules = m_rules;
        rules[index].enabled = !rules[index].enabled;
        PersistRules(std::move(rules));
    }

    void MainPage::DeleteRule(std::size_t index)
    {
        if (index >= m_rules.size())
        {
            return;
        }
        auto rules = m_rules;
        rules.erase(rules.begin() + static_cast<std::ptrdiff_t>(index));
        PersistRules(std::move(rules));
    }

    void MainPage::TestRule(std::size_t index)
    {
        if (index < m_rules.size())
        {
            NotifyNative(
                PreviewCustomGlyphMessage,
                static_cast<std::uintptr_t>(m_rules[index].id));
        }
    }

    void MainPage::SelectBuiltInTestIndicator(
        ClipboardContentType contentType)
    {
        m_selectedTestRuleId.reset();
        m_selectedTestContentType = contentType;
        TestIndicatorDropDownButton().Content(
            CreateBuiltInVisual(contentType));

        hstring name;
        switch (contentType)
        {
            case ClipboardContentType::Image:
                name = m_resources.GetString(L"BuiltInImageName");
                break;
            case ClipboardContentType::Files:
                name = m_resources.GetString(L"BuiltInFilesName");
                break;
            case ClipboardContentType::RichText:
                name = m_resources.GetString(L"BuiltInRichTextName");
                break;
            case ClipboardContentType::Text:
                name = m_resources.GetString(L"BuiltInTextName");
                break;
            case ClipboardContentType::Object:
                name = m_resources.GetString(L"BuiltInObjectName");
                break;
        }
        Automation::AutomationProperties::SetName(
            TestIndicatorDropDownButton(), name);
    }

    void MainPage::SelectCustomTestIndicator(std::uint64_t ruleId)
    {
        const auto rule = std::ranges::find_if(
            m_rules,
            [ruleId](const ClipboardRule& candidate)
            {
                return candidate.id == ruleId;
            });
        if (rule == m_rules.end())
        {
            SelectBuiltInTestIndicator(ClipboardContentType::Text);
            return;
        }

        m_selectedTestRuleId = ruleId;
        TestIndicatorDropDownButton().Content(
            CreateGlyphVisual(rule->glyph));
        Automation::AutomationProperties::SetName(
            TestIndicatorDropDownButton(), hstring{ rule->name });
    }

    void MainPage::RefreshTestIndicatorMenu()
    {
        if (!TestIndicatorMenu() || !TestIndicatorDropDownButton())
        {
            return;
        }
        TestIndicatorMenu().Items().Clear();
        auto weak = get_weak();

        const auto appendBuiltIn =
            [&](ClipboardContentType contentType, hstring const& name)
            {
                MenuFlyoutItem item;
                item.Text(name);
                item.Icon(CreateBuiltInIcon(contentType));
                item.Click(
                    [weak, contentType](
                        [[maybe_unused]] IInspectable const&,
                        [[maybe_unused]] RoutedEventArgs const&)
                    {
                        if (auto self = weak.get())
                        {
                            self->SelectBuiltInTestIndicator(contentType);
                        }
                    });
                TestIndicatorMenu().Items().Append(item);
            };

        appendBuiltIn(
            ClipboardContentType::Text,
            m_resources.GetString(L"BuiltInTextName"));
        appendBuiltIn(
            ClipboardContentType::RichText,
            m_resources.GetString(L"BuiltInRichTextName"));
        appendBuiltIn(
            ClipboardContentType::Image,
            m_resources.GetString(L"BuiltInImageName"));
        appendBuiltIn(
            ClipboardContentType::Files,
            m_resources.GetString(L"BuiltInFilesName"));
        appendBuiltIn(
            ClipboardContentType::Object,
            m_resources.GetString(L"BuiltInObjectName"));

        if (!m_rules.empty())
        {
            TestIndicatorMenu().Items().Append(MenuFlyoutSeparator{});
        }
        for (const auto& rule : m_rules)
        {
            MenuFlyoutItem item;
            item.Text(hstring{ rule.name });
            item.Icon(CreateGlyphIcon(rule.glyph));
            item.Click(
                [weak, ruleId = rule.id](
                    [[maybe_unused]] IInspectable const&,
                    [[maybe_unused]] RoutedEventArgs const&)
                {
                    if (auto self = weak.get())
                    {
                        self->SelectCustomTestIndicator(ruleId);
                    }
                });
            TestIndicatorMenu().Items().Append(item);
        }

        if (m_selectedTestRuleId)
        {
            SelectCustomTestIndicator(*m_selectedTestRuleId);
        }
        else
        {
            SelectBuiltInTestIndicator(m_selectedTestContentType);
        }
    }

    void MainPage::TestSelectedIndicator_Click(
        [[maybe_unused]] IInspectable const&,
        [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_selectedTestRuleId)
        {
            const auto rule = std::ranges::find_if(
                m_rules,
                [this](const ClipboardRule& candidate)
                {
                    return candidate.id == *m_selectedTestRuleId;
                });
            if (rule != m_rules.end())
            {
                NotifyNative(
                    PreviewCustomGlyphMessage,
                    static_cast<std::uintptr_t>(rule->id));
                return;
            }
            SelectBuiltInTestIndicator(ClipboardContentType::Text);
        }
        NotifyNative(
            PreviewGlyphMessage,
            static_cast<std::uintptr_t>(m_selectedTestContentType));
    }

    void MainPage::RulesListView_DragItemsCompleted(
        [[maybe_unused]] ListViewBase const& sender,
        [[maybe_unused]] DragItemsCompletedEventArgs const& args)
    {
        std::vector<ClipboardRule> reordered;
        reordered.reserve(m_rules.size());
        for (const auto& value : RulesListView().Items())
        {
            const auto item = value.try_as<ListViewItem>();
            if (!item || !item.Tag())
            {
                return;
            }
            const auto id = unbox_value<std::uint64_t>(item.Tag());
            const auto rule = std::ranges::find_if(
                m_rules,
                [id](const ClipboardRule& candidate)
                {
                    return candidate.id == id;
                });
            if (rule == m_rules.end())
            {
                return;
            }
            reordered.push_back(*rule);
        }
        bool changed = reordered.size() == m_rules.size();
        if (changed)
        {
            changed = !std::ranges::equal(
                reordered,
                m_rules,
                [](const ClipboardRule& left, const ClipboardRule& right)
                {
                    return left.id == right.id;
                });
        }
        if (changed)
        {
            if (!PersistRules(std::move(reordered)))
            {
                RefreshRulesList();
            }
        }
    }

    void MainPage::RefreshRulesList()
    {
        if (!RulesListView() || !BuiltInRulesListView())
        {
            return;
        }
        RulesListView().Items().Clear();
        BuiltInRulesListView().Items().Clear();
        auto weak = get_weak();

        const auto appendRow = [&](ListView const& target,
                                   FrameworkElement const& glyphVisual,
                                   hstring const& name,
                                   hstring const& status,
                                   std::optional<std::size_t> index)
        {
            Grid row;
            row.ColumnSpacing(10);
            row.Padding(Thickness{ 4, 8, 4, 8 });
            row.ColumnDefinitions().Append(ColumnDefinition{});
            row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(42));
            row.ColumnDefinitions().Append(ColumnDefinition{});
            row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(
                1, GridUnitType::Star));
            row.ColumnDefinitions().Append(ColumnDefinition{});
            row.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::Auto());

            row.Children().Append(glyphVisual);

            StackPanel labels;
            labels.Spacing(2);
            TextBlock nameText;
            nameText.Text(name);
            nameText.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            nameText.TextTrimming(TextTrimming::CharacterEllipsis);
            labels.Children().Append(nameText);
            auto statusText = Markup::XamlReader::Load(
                LR"(<TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Foreground="{ThemeResource TextFillColorSecondaryBrush}" />)")
                .as<TextBlock>();
            statusText.Text(status);
            statusText.FontSize(12);
            labels.Children().Append(statusText);
            Grid::SetColumn(labels, 1);
            row.Children().Append(labels);

            if (index)
            {
                Button actions;
                actions.Content(box_value(L"\u2026"));
                actions.VerticalAlignment(VerticalAlignment::Center);
                Automation::AutomationProperties::SetName(
                    actions, m_resources.GetString(L"RuleActionsAutomationName"));

                MenuFlyout menu;
                MenuFlyoutItem edit;
                edit.Text(m_resources.GetString(L"RuleEditAction"));
                edit.Click([weak, value = *index](
                    [[maybe_unused]] IInspectable const&,
                    [[maybe_unused]] RoutedEventArgs const&)
                {
                    if (auto self = weak.get())
                    {
                        self->EditRuleAsync(value);
                    }
                });
                menu.Items().Append(edit);

                MenuFlyoutItem test;
                test.Text(m_resources.GetString(L"RuleTestAction"));
                test.Click([weak, value = *index](
                    [[maybe_unused]] IInspectable const&,
                    [[maybe_unused]] RoutedEventArgs const&)
                {
                    if (auto self = weak.get())
                    {
                        self->TestRule(value);
                    }
                });
                menu.Items().Append(test);

                MenuFlyoutItem toggle;
                toggle.Text(m_rules[*index].enabled
                    ? m_resources.GetString(L"RuleDisableAction")
                    : m_resources.GetString(L"RuleEnableAction"));
                toggle.Click([weak, value = *index](
                    [[maybe_unused]] IInspectable const&,
                    [[maybe_unused]] RoutedEventArgs const&)
                {
                    if (auto self = weak.get())
                    {
                        self->ToggleRule(value);
                    }
                });
                menu.Items().Append(toggle);

                MenuFlyoutItem remove;
                remove.Text(m_resources.GetString(L"RuleDeleteAction"));
                remove.Click([weak, value = *index](
                    [[maybe_unused]] IInspectable const&,
                    [[maybe_unused]] RoutedEventArgs const&)
                {
                    if (auto self = weak.get())
                    {
                        self->DeleteRule(value);
                    }
                });
                menu.Items().Append(remove);

                row.ContextFlyout(menu);
                actions.Flyout(menu);
                Grid::SetColumn(actions, 2);
                row.Children().Append(actions);
            }

            ListViewItem item;
            item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            item.Content(row);
            if (index)
            {
                item.Tag(box_value(m_rules[*index].id));
                item.DoubleTapped([weak, value = *index](
                    [[maybe_unused]] IInspectable const&,
                    DoubleTappedRoutedEventArgs const& args)
                {
                    if (auto self = weak.get())
                    {
                        self->TestRule(value);
                        args.Handled(true);
                    }
                });
            }
            target.Items().Append(item);
        };

        for (std::size_t index = 0; index < m_rules.size(); ++index)
        {
            const auto& rule = m_rules[index];
            appendRow(
                RulesListView(),
                CreateGlyphVisual(rule.glyph),
                hstring{ rule.name },
                rule.enabled
                    ? m_resources.GetString(L"RuleEnabledStatus")
                    : m_resources.GetString(L"RuleDisabledStatus"),
                index);
        }

        const hstring locked = m_resources.GetString(L"RuleBuiltInStatus");
        appendRow(BuiltInRulesListView(), CreateBuiltInVisual(ClipboardContentType::Image), m_resources.GetString(L"BuiltInImageName"), locked, std::nullopt);
        appendRow(BuiltInRulesListView(), CreateBuiltInVisual(ClipboardContentType::Files), m_resources.GetString(L"BuiltInFilesName"), locked, std::nullopt);
        appendRow(BuiltInRulesListView(), CreateBuiltInVisual(ClipboardContentType::RichText), m_resources.GetString(L"BuiltInRichTextName"), locked, std::nullopt);
        appendRow(BuiltInRulesListView(), CreateBuiltInVisual(ClipboardContentType::Text), m_resources.GetString(L"BuiltInTextName"), locked, std::nullopt);
        appendRow(BuiltInRulesListView(), CreateBuiltInVisual(ClipboardContentType::Object), m_resources.GetString(L"BuiltInObjectName"), locked, std::nullopt);
        RefreshTestIndicatorMenu();
    }

    winrt::fire_and_forget MainPage::EditRuleAsync(
        std::optional<std::size_t> index,
        std::vector<ClipboardFormatIdentity> formats,
        std::size_t omittedFormats)
    {
        auto strong = get_strong();
        try
        {
            ClipboardRule initial;
            if (index)
            {
                if (*index >= m_rules.size())
                {
                    co_return;
                }
                initial = m_rules[*index];
                formats.reserve(initial.conditions.size());
                for (const auto& condition : initial.conditions)
                {
                    formats.push_back(condition.format);
                }
            }
            else
            {
                initial.id = GenerateRuleId();
                initial.enabled = true;
                for (const auto& format : formats)
                {
                    initial.conditions.push_back({ format, true });
                }
            }

            ContentDialog dialog;
            dialog.XamlRoot(XamlRoot());
            dialog.Title(box_value(index
                ? m_resources.GetString(L"RuleEditDialogTitle")
                : m_resources.GetString(L"RuleAddDialogTitle")));
            dialog.PrimaryButtonText(m_resources.GetString(L"RuleSaveButton"));
            dialog.CloseButtonText(m_resources.GetString(L"CancelButton"));
            dialog.DefaultButton(ContentDialogButton::Primary);

            StackPanel panel;
            panel.Spacing(10);

            TextBox nameBox;
            nameBox.Header(box_value(m_resources.GetString(L"RuleNameLabel")));
            nameBox.Text(hstring{ initial.name });
            panel.Children().Append(nameBox);

            TextBox glyphBox;
            glyphBox.Header(box_value(m_resources.GetString(L"RuleGlyphLabel")));
            glyphBox.MaxLength(16);
            glyphBox.IsColorFontEnabled(true);
            glyphBox.Width(200);
            glyphBox.HorizontalAlignment(HorizontalAlignment::Left);
            glyphBox.Text(hstring{ initial.glyph });
            glyphBox.BeforeTextChanging(
                [](
                    [[maybe_unused]] TextBox const&,
                    TextBoxBeforeTextChangingEventArgs const& args)
                {
                    const hstring text = args.NewText();
                    if (!text.empty() &&
                        !IsValidRuleGlyph(std::wstring_view{
                            text.c_str(), text.size() }))
                    {
                        args.Cancel(true);
                    }
                });

            Grid glyphEditor;
            glyphEditor.ColumnSpacing(16);
            ColumnDefinition inputColumn;
            inputColumn.Width(GridLengthHelper::Auto());
            glyphEditor.ColumnDefinitions().Append(inputColumn);
            ColumnDefinition previewColumn;
            previewColumn.Width(
                GridLengthHelper::FromValueAndType(
                    1, GridUnitType::Star));
            glyphEditor.ColumnDefinitions().Append(previewColumn);
            glyphEditor.Children().Append(glyphBox);

            Border editorPreview;
            editorPreview.Width(48);
            editorPreview.Height(48);
            editorPreview.VerticalAlignment(VerticalAlignment::Center);
            editorPreview.HorizontalAlignment(HorizontalAlignment::Center);
            editorPreview.BorderBrush(
                Application::Current().Resources().Lookup(
                    box_value(L"CardStrokeColorDefaultBrush"))
                    .as<Media::Brush>());
            editorPreview.BorderThickness(Thickness{ 1 });
            editorPreview.CornerRadius(
                Microsoft::UI::Xaml::CornerRadius{ 4, 4, 4, 4 });
            auto initialPreview = CreateGlyphVisual(
                initial.glyph, L"AccentFillColorDefaultBrush");
            initialPreview.HorizontalAlignment(HorizontalAlignment::Center);
            initialPreview.VerticalAlignment(VerticalAlignment::Center);
            editorPreview.Child(initialPreview);
            Grid::SetColumn(editorPreview, 1);
            glyphEditor.Children().Append(editorPreview);
            glyphBox.TextChanged(
                [editorPreview](
                    IInspectable const& sender,
                    [[maybe_unused]] TextChangedEventArgs const&)
                {
                    const hstring text = sender.as<TextBox>().Text();
                    auto preview = CreateGlyphVisual(
                        std::wstring_view{ text.c_str(), text.size() },
                        L"AccentFillColorDefaultBrush");
                    preview.HorizontalAlignment(HorizontalAlignment::Center);
                    preview.VerticalAlignment(VerticalAlignment::Center);
                    editorPreview.Child(preview);
                });
            panel.Children().Append(glyphEditor);

            TextBlock glyphDescription;
            glyphDescription.Text(
                m_resources.GetString(L"RuleGlyphDescription"));
            glyphDescription.Foreground(
                Application::Current().Resources().Lookup(
                    box_value(L"TextFillColorSecondaryBrush"))
                    .as<Media::Brush>());
            glyphDescription.FontSize(12);
            glyphDescription.TextWrapping(TextWrapping::Wrap);
            panel.Children().Append(glyphDescription);

            TextBlock formatsHeader;
            formatsHeader.Text(m_resources.GetString(L"RuleFormatsLabel"));
            formatsHeader.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            panel.Children().Append(formatsHeader);

            if (omittedFormats > 0)
            {
                std::wstring text{ m_resources.GetString(L"RuleUnsupportedFormatsOmitted") };
                const auto marker = text.find(L"{0}");
                if (marker != std::wstring::npos)
                {
                    text.replace(marker, 3, std::to_wstring(omittedFormats));
                }
                TextBlock omitted;
                omitted.Text(hstring{ text });
                omitted.TextWrapping(TextWrapping::Wrap);
                omitted.Foreground(
                    Application::Current().Resources().Lookup(
                        box_value(L"TextFillColorSecondaryBrush"))
                        .as<Media::Brush>());
                panel.Children().Append(omitted);
            }

            std::vector<ComboBox> choices;
            choices.reserve(formats.size());
            for (std::size_t formatIndex = 0;
                 formatIndex < formats.size();
                 ++formatIndex)
            {
                Grid row;
                row.ColumnSpacing(8);
                row.ColumnDefinitions().Append(ColumnDefinition{});
                row.ColumnDefinitions().GetAt(0).Width(
                    GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                row.ColumnDefinitions().Append(ColumnDefinition{});
                row.ColumnDefinitions().GetAt(1).Width(
                    GridLengthHelper::FromPixels(132));

                TextBlock formatName;
                formatName.Text(hstring{
                    ClipboardFormatIdentityName(formats[formatIndex]) });
                formatName.TextWrapping(TextWrapping::Wrap);
                formatName.VerticalAlignment(VerticalAlignment::Center);
                row.Children().Append(formatName);

                ComboBox choice;
                choice.HorizontalAlignment(HorizontalAlignment::Stretch);
                choice.Items().Append(box_value(
                    m_resources.GetString(L"RuleMustHaveChoice")));
                choice.Items().Append(box_value(
                    m_resources.GetString(L"RuleMustNotHaveChoice")));
                choice.Items().Append(box_value(
                    m_resources.GetString(L"RuleRemoveChoice")));
                choice.SelectedIndex(
                    initial.conditions[formatIndex].mustBePresent ? 0 : 1);
                Grid::SetColumn(choice, 1);
                row.Children().Append(choice);
                choices.push_back(choice);
                panel.Children().Append(row);
            }

            TextBlock errorText;
            errorText.Foreground(
                Application::Current().Resources().Lookup(
                    box_value(L"SystemFillColorCriticalBrush"))
                    .as<Media::Brush>());
            errorText.TextWrapping(TextWrapping::Wrap);
            errorText.Visibility(Visibility::Collapsed);
            panel.Children().Append(errorText);

            ScrollViewer scroll;
            scroll.MaxHeight(500);
            scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
            scroll.Content(panel);
            dialog.Content(scroll);

            std::optional<ClipboardRule> accepted;
            dialog.PrimaryButtonClick(
                [&](ContentDialog const&,
                    ContentDialogButtonClickEventArgs const& args)
                {
                    ClipboardRule candidate = initial;
                    candidate.name = std::wstring{ nameBox.Text() };
                    candidate.glyph = std::wstring{ glyphBox.Text() };
                    candidate.conditions.clear();
                    for (std::size_t conditionIndex = 0;
                         conditionIndex < choices.size();
                         ++conditionIndex)
                    {
                        const int selected = choices[conditionIndex].SelectedIndex();
                        if (selected != 2)
                        {
                            candidate.conditions.push_back(
                                { formats[conditionIndex], selected == 0 });
                        }
                    }

                    const bool blankName =
                        candidate.name.empty() ||
                        std::ranges::all_of(
                            candidate.name,
                            [](wchar_t value) { return iswspace(value) != 0; });
                    if (blankName)
                    {
                        errorText.Text(m_resources.GetString(L"RuleNameRequiredError"));
                    }
                    else if (!IsValidRuleGlyph(candidate.glyph))
                    {
                        errorText.Text(m_resources.GetString(L"RuleGlyphInvalidError"));
                    }
                    else if (candidate.conditions.empty())
                    {
                        errorText.Text(m_resources.GetString(L"RuleConditionsRequiredError"));
                    }
                    else if (!ValidateClipboardRule(candidate))
                    {
                        errorText.Text(m_resources.GetString(L"RuleInvalidError"));
                    }
                    else
                    {
                        accepted = std::move(candidate);
                        return;
                    }
                    errorText.Visibility(Visibility::Visible);
                    args.Cancel(true);
                });

            const auto result = co_await dialog.ShowAsync();
            if (result != ContentDialogResult::Primary || !accepted)
            {
                co_return;
            }

            auto rules = m_rules;
            if (index)
            {
                if (*index >= rules.size())
                {
                    co_return;
                }
                rules[*index] = std::move(*accepted);
            }
            else
            {
                rules.push_back(std::move(*accepted));
            }
            PersistRules(std::move(rules));
        }
        catch (const hresult_error& ex)
        {
            ShowError(m_resources.GetString(L"RuleEditorFailedTitle"), ex.message());
        }
        catch (const std::exception& ex)
        {
            ShowError(
                m_resources.GetString(L"RuleEditorFailedTitle"),
                to_hstring(ex.what()));
        }
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
        if (result.formats.empty())
        {
            indicator = m_resources.GetString(L"ClipboardIndicatorNone");
        }
        else
        {
            const ClipboardIndicator evaluated =
                ClipboardRuleEngine{ m_rules }.Evaluate(result.formats);
            if (evaluated.IsCustom())
            {
                indicator = hstring{ evaluated.glyph };
            }
            else switch (evaluated.contentType)
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
        ResetAsync();
    }

    winrt::fire_and_forget MainPage::ResetAsync()
    {
        auto strong = get_strong();
        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(m_resources.GetString(L"ResetConfirmTitle")));
        dialog.Content(box_value(m_resources.GetString(L"ResetConfirmMessage")));
        dialog.PrimaryButtonText(m_resources.GetString(L"ResetConfirmButton"));
        dialog.CloseButtonText(m_resources.GetString(L"CancelButton"));
        dialog.DefaultButton(ContentDialogButton::Close);

        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        if (!PersistRules({}))
        {
            co_return;
        }
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

    // --- JSON backup/restore ---

    std::string MainPage::CreateBackupJson(bool startWithWindows)
    {
        HKEY prefKey = nullptr;
        RegOpenKeyExW(HKEY_CURRENT_USER, PreferencesKey, 0, KEY_READ, &prefKey);
        int enabled = ReadDword(prefKey, L"Enabled", 1);
        if (prefKey) RegCloseKey(prefKey);

        JsonObject root;
        root.Insert(L"schemaVersion", JsonValue::CreateNumberValue(2));

        JsonObject app;
        app.Insert(L"enabled", JsonValue::CreateBooleanValue(enabled != 0));
        app.Insert(
            L"startWithWindows",
            JsonValue::CreateBooleanValue(startWithWindows));
        root.Insert(L"app", app);

        JsonObject indicator;
        indicator.Insert(L"xPercent", JsonValue::CreateNumberValue(m_xPercent));
        indicator.Insert(L"yPercent", JsonValue::CreateNumberValue(m_yPercent));
        indicator.Insert(
            L"scalePercent",
            JsonValue::CreateNumberValue(std::round(SizeSlider().Value())));
        indicator.Insert(
            L"animationStyle",
            JsonValue::CreateNumberValue(AnimationComboBox().SelectedIndex()));
        indicator.Insert(
            L"animationSpeed",
            JsonValue::CreateNumberValue(
                std::round(AnimationSpeedSlider().Value())));
        indicator.Insert(
            L"visibilityMode",
            JsonValue::CreateNumberValue(VisibilityComboBox().SelectedIndex()));
        indicator.Insert(
            L"visibilityDurationSeconds",
            JsonValue::CreateNumberValue(
                std::round(VisibilityDurationSlider().Value())));
        root.Insert(L"indicator", indicator);

        JsonArray rules;
        for (const auto& rule : m_rules)
        {
            JsonObject ruleObject;
            ruleObject.Insert(
                L"id",
                JsonValue::CreateStringValue(
                    hstring{ std::to_wstring(rule.id) }));
            ruleObject.Insert(
                L"name", JsonValue::CreateStringValue(hstring{ rule.name }));
            ruleObject.Insert(
                L"glyph", JsonValue::CreateStringValue(hstring{ rule.glyph }));
            ruleObject.Insert(
                L"enabled",
                JsonValue::CreateBooleanValue(rule.enabled));

            JsonArray conditions;
            for (const auto& condition : rule.conditions)
            {
                JsonObject conditionObject;
                conditionObject.Insert(
                    L"mustBePresent",
                    JsonValue::CreateBooleanValue(condition.mustBePresent));
                JsonObject format;
                if (condition.format.IsRegistered())
                {
                    format.Insert(
                        L"registeredName",
                        JsonValue::CreateStringValue(
                            hstring{ condition.format.registeredName }));
                }
                else
                {
                    format.Insert(
                        L"standardFormat",
                        JsonValue::CreateNumberValue(
                            condition.format.standardFormat));
                }
                conditionObject.Insert(L"format", format);
                conditions.Append(conditionObject);
            }
            ruleObject.Insert(L"conditions", conditions);
            rules.Append(ruleObject);
        }
        root.Insert(L"rules", rules);
        return to_string(root.Stringify());
    }

    MainPage::BackupSettings MainPage::ParseBackupJson(const std::string& json)
    {
        try
        {
            const JsonObject root = JsonObject::Parse(to_hstring(json));
            const double schemaNumber = root.GetNamedNumber(L"schemaVersion");
            if (schemaNumber != std::round(schemaNumber))
            {
                throw std::invalid_argument(
                    to_string(m_resources.GetString(L"BackupInvalidJsonError")));
            }
            const int schemaVersion = static_cast<int>(schemaNumber);
            if (schemaVersion != 1 && schemaVersion != 2)
            {
                hstring msg =
                    m_resources.GetString(L"UnsupportedBackupVersionError");
                std::wstring formatted{ msg };
                const auto marker = formatted.find(L"{0}");
                if (marker != std::wstring::npos)
                {
                    formatted.replace(
                        marker, 3, std::to_wstring(schemaVersion));
                }
                throw std::invalid_argument(
                    to_string(hstring{ formatted }));
            }

            const JsonObject app = root.GetNamedObject(L"app");
            const JsonObject indicator = root.GetNamedObject(L"indicator");
            const auto readInteger =
                [this](JsonObject const& object, wchar_t const* name)
                {
                    const double value = object.GetNamedNumber(name);
                    if (value != std::round(value) ||
                        value < std::numeric_limits<int>::min() ||
                        value > std::numeric_limits<int>::max())
                    {
                        throw std::invalid_argument(to_string(
                            m_resources.GetString(L"BackupInvalidJsonError")));
                    }
                    return static_cast<int>(value);
                };

            const int xPct = readInteger(indicator, L"xPercent");
            const int yPct = readInteger(indicator, L"yPercent");
            const int scale = readInteger(indicator, L"scalePercent");
            const int animStyle = readInteger(indicator, L"animationStyle");
            const int animSpeed = readInteger(indicator, L"animationSpeed");
            const int visMode = readInteger(indicator, L"visibilityMode");
            const int visDur =
                readInteger(indicator, L"visibilityDurationSeconds");

            if (xPct < MinimumPosition || xPct > MaximumPosition ||
                yPct < MinimumPosition || yPct > MaximumPosition ||
                scale < 0 || scale > 100)
            {
                throw std::invalid_argument(to_string(
                    m_resources.GetString(L"BackupInvalidIndicatorError")));
            }
            if (animStyle < 0 || animStyle > 3)
            {
                throw std::invalid_argument(to_string(
                    m_resources.GetString(L"BackupInvalidAnimationError")));
            }
            if (animSpeed < 0 || animSpeed > 100)
            {
                throw std::invalid_argument(to_string(
                    m_resources.GetString(L"BackupInvalidAnimationSpeedError")));
            }
            if (visMode < 0 || visMode > 1 || visDur < 1 || visDur > 60)
            {
                throw std::invalid_argument(to_string(
                    m_resources.GetString(L"BackupInvalidVisibilityError")));
            }

            std::vector<ClipboardRule> rules;
            if (schemaVersion == 2)
            {
                const JsonArray rulesArray = root.GetNamedArray(L"rules");
                std::set<std::uint64_t> ids;
                for (std::uint32_t ruleIndex = 0;
                     ruleIndex < rulesArray.Size();
                     ++ruleIndex)
                {
                    const JsonObject object =
                        rulesArray.GetObjectAt(ruleIndex);
                    ClipboardRule rule;
                    const std::wstring idText{
                        object.GetNamedString(L"id") };
                    wchar_t* end = nullptr;
                    errno = 0;
                    rule.id = std::wcstoull(
                        idText.c_str(), &end, 10);
                    if (idText.empty() ||
                        !std::ranges::all_of(
                            idText,
                            [](wchar_t value)
                            {
                                return value >= L'0' && value <= L'9';
                            }) ||
                        errno == ERANGE || end == idText.c_str() ||
                        *end != L'\0')
                    {
                        throw std::invalid_argument(to_string(
                            m_resources.GetString(L"BackupInvalidRulesError")));
                    }
                    rule.name = object.GetNamedString(L"name");
                    rule.glyph = object.GetNamedString(L"glyph");
                    rule.enabled = object.GetNamedBoolean(L"enabled");

                    const JsonArray conditions =
                        object.GetNamedArray(L"conditions");
                    for (std::uint32_t conditionIndex = 0;
                         conditionIndex < conditions.Size();
                         ++conditionIndex)
                    {
                        const JsonObject conditionObject =
                            conditions.GetObjectAt(conditionIndex);
                        ClipboardRuleCondition condition;
                        condition.mustBePresent =
                            conditionObject.GetNamedBoolean(
                                L"mustBePresent");
                        const JsonObject format =
                            conditionObject.GetNamedObject(L"format");
                        const bool hasStandard =
                            format.HasKey(L"standardFormat");
                        const bool hasRegistered =
                            format.HasKey(L"registeredName");
                        if (hasStandard == hasRegistered)
                        {
                            throw std::invalid_argument(to_string(
                                m_resources.GetString(
                                    L"BackupInvalidRulesError")));
                        }
                        if (hasRegistered)
                        {
                            condition.format.registeredName =
                                format.GetNamedString(L"registeredName");
                        }
                        else
                        {
                            const double number =
                                format.GetNamedNumber(L"standardFormat");
                            if (number != std::round(number) ||
                                number <= 0 ||
                                number > std::numeric_limits<UINT>::max())
                            {
                                throw std::invalid_argument(to_string(
                                    m_resources.GetString(
                                        L"BackupInvalidRulesError")));
                            }
                            condition.format.standardFormat =
                                static_cast<UINT>(number);
                        }
                        rule.conditions.push_back(std::move(condition));
                    }
                    if (!ValidateClipboardRule(rule) ||
                        !ids.insert(rule.id).second)
                    {
                        throw std::invalid_argument(to_string(
                            m_resources.GetString(
                                L"BackupInvalidRulesError")));
                    }
                    rules.push_back(std::move(rule));
                }
            }

            return BackupSettings{
                app.GetNamedBoolean(L"enabled"),
                app.GetNamedBoolean(L"startWithWindows"),
                xPct,
                yPct,
                scale,
                animStyle,
                animSpeed,
                visMode,
                visDur,
                std::move(rules),
            };
        }
        catch (const std::invalid_argument&)
        {
            throw;
        }
        catch (const hresult_error&)
        {
            throw std::invalid_argument(
                to_string(m_resources.GetString(L"BackupInvalidJsonError")));
        }
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

        const bool wasLoaded = m_loaded;
        m_loaded = false;
        m_xPercent = settings.xPercent;
        m_yPercent = settings.yPercent;
        SizeSlider().Value(static_cast<double>(settings.scalePercent));
        AnimationComboBox().SelectedIndex(settings.animationStyle);
        AnimationSpeedSlider().Value(static_cast<double>(settings.animationSpeed));
        VisibilityComboBox().SelectedIndex(settings.visibilityMode);
        VisibilityDurationSlider().Value(
            static_cast<double>(settings.visibilityDurationSeconds));
        m_loaded = wasLoaded;
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
            IsEnabled(false);

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
            const auto previousRules = m_rules;
            if (!SaveClipboardRules(settings.rules))
            {
                throw std::invalid_argument(to_string(
                    m_resources.GetString(L"RuleSaveFailedMessage")));
            }
            try
            {
                co_await WriteStartWithWindowsAsync(
                    settings.startWithWindows);
            }
            catch (...)
            {
                if (!SaveClipboardRules(previousRules))
                {
                    throw std::runtime_error(to_string(
                        m_resources.GetString(
                            L"RuleRollbackFailedMessage")));
                }
                throw;
            }
            m_rules = settings.rules;
            ApplyBackupSettings(settings);
            RefreshRulesList();
            NotifyNative(SettingsChangedMessage);
            m_lastClipboardSequence = 0;
            UpdateClipboardStatus();
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
        IsEnabled(true);
    }
}

#include "clipboard_classifier.hpp"

#include <algorithm>

namespace {

bool Contains(std::span<const UINT> formats, UINT format) {
    return format != 0 && std::ranges::find(formats, format) != formats.end();
}

}  // namespace

std::wstring DescribeClipboardFormat(UINT format) {
    switch (format) {
        case CF_TEXT: return L"CF_TEXT";
        case CF_BITMAP: return L"CF_BITMAP";
        case CF_METAFILEPICT: return L"CF_METAFILEPICT";
        case CF_SYLK: return L"CF_SYLK";
        case CF_DIF: return L"CF_DIF";
        case CF_TIFF: return L"CF_TIFF";
        case CF_OEMTEXT: return L"CF_OEMTEXT";
        case CF_DIB: return L"CF_DIB";
        case CF_PALETTE: return L"CF_PALETTE";
        case CF_PENDATA: return L"CF_PENDATA";
        case CF_RIFF: return L"CF_RIFF";
        case CF_WAVE: return L"CF_WAVE";
        case CF_UNICODETEXT: return L"CF_UNICODETEXT";
        case CF_ENHMETAFILE: return L"CF_ENHMETAFILE";
        case CF_HDROP: return L"CF_HDROP";
        case CF_LOCALE: return L"CF_LOCALE";
        case CF_DIBV5: return L"CF_DIBV5";
        case CF_OWNERDISPLAY: return L"CF_OWNERDISPLAY";
        case CF_DSPTEXT: return L"CF_DSPTEXT";
        case CF_DSPBITMAP: return L"CF_DSPBITMAP";
        case CF_DSPMETAFILEPICT: return L"CF_DSPMETAFILEPICT";
        case CF_DSPENHMETAFILE: return L"CF_DSPENHMETAFILE";
        default:
            break;
    }

    wchar_t name[256]{};
    const int length = GetClipboardFormatNameW(
        format,
        name,
        static_cast<int>(std::size(name)));
    if (length > 0) {
        return std::wstring{name, static_cast<size_t>(length)};
    }

    wchar_t fallback[32]{};
    swprintf_s(fallback, L"Format 0x%04X", format);
    return fallback;
}

std::optional<ClipboardContentType> ClassifyClipboardFormats(
    std::span<const UINT> formats,
    UINT richTextFormat,
    UINT htmlFormat) {
    if (formats.empty()) {
        return std::nullopt;
    }

    const bool hasRtf = Contains(formats, richTextFormat);
    const bool hasHtml = Contains(formats, htmlFormat);
    const bool hasPlainText =
        Contains(formats, CF_UNICODETEXT) ||
        Contains(formats, CF_TEXT) ||
        Contains(formats, CF_OEMTEXT);

    if (hasRtf &&
        hasPlainText &&
        !Contains(formats, CF_HDROP)) {
        return ClipboardContentType::RichText;
    }

    if (Contains(formats, CF_DIBV5) ||
        Contains(formats, CF_DIB) ||
        Contains(formats, CF_BITMAP)) {
        return ClipboardContentType::Image;
    }

    if (Contains(formats, CF_HDROP)) {
        return ClipboardContentType::Files;
    }

    if (hasRtf || hasHtml) {
        return ClipboardContentType::RichText;
    }

    if (hasPlainText) {
        return ClipboardContentType::Text;
    }

    if (Contains(formats, CF_ENHMETAFILE) ||
        Contains(formats, CF_METAFILEPICT)) {
        return ClipboardContentType::Image;
    }

    return ClipboardContentType::Object;
}

ClipboardReadResult ReadClipboardContentType(HWND owner) {
    if (!OpenClipboard(owner)) {
        return {false, std::nullopt, {}};
    }

    std::vector<UINT> formats;
    UINT format = 0;
    while ((format = EnumClipboardFormats(format)) != 0) {
        formats.push_back(format);
    }

    CloseClipboard();

    const UINT richTextFormat = RegisterClipboardFormatW(L"Rich Text Format");
    const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
    return {
        true,
        ClassifyClipboardFormats(formats, richTextFormat, htmlFormat),
        std::move(formats),
    };
}

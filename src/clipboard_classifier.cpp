#include "clipboard_classifier.hpp"

#include <algorithm>

namespace {

bool Contains(std::span<const UINT> formats, UINT format) {
    return format != 0 && std::ranges::find(formats, format) != formats.end();
}

}  // namespace

std::optional<ClipboardContentType> ClassifyClipboardFormats(
    std::span<const UINT> formats,
    UINT richTextFormat,
    UINT htmlFormat) {
    if (formats.empty()) {
        return std::nullopt;
    }

    if (Contains(formats, CF_DIBV5) ||
        Contains(formats, CF_DIB) ||
        Contains(formats, CF_BITMAP)) {
        return ClipboardContentType::Image;
    }

    if (Contains(formats, CF_HDROP)) {
        return ClipboardContentType::Files;
    }

    if (Contains(formats, richTextFormat) || Contains(formats, htmlFormat)) {
        return ClipboardContentType::RichText;
    }

    if (Contains(formats, CF_UNICODETEXT) ||
        Contains(formats, CF_TEXT) ||
        Contains(formats, CF_OEMTEXT)) {
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
        return {false, std::nullopt};
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
    };
}

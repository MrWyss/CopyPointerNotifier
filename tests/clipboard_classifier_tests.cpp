#include "clipboard_classifier.hpp"
#include "glyph_scale.hpp"

#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <vector>

namespace {

constexpr UINT kRichText = 0xC100;
constexpr UINT kHtml = 0xC101;
constexpr UINT kUnknown = 0xC102;

void Expect(
    std::initializer_list<UINT> formats,
    std::optional<ClipboardContentType> expected) {
    const std::vector<UINT> values(formats);
    if (ClassifyClipboardFormats(values, kRichText, kHtml) != expected) {
        std::abort();
    }
}

}  // namespace

int main() {
    static_assert(EffectiveGlyphScalePercent(0) == 0.0F);
    static_assert(EffectiveGlyphScalePercent(50) == 50.0F);
    static_assert(EffectiveGlyphScalePercent(100) == 160.0F);

    if (DescribeClipboardFormat(CF_DIBV5) != L"CF_DIBV5" ||
        DescribeClipboardFormat(CF_ENHMETAFILE) != L"CF_ENHMETAFILE") {
        std::abort();
    }

    Expect({}, std::nullopt);
    Expect({kUnknown}, ClipboardContentType::Object);
    Expect({CF_UNICODETEXT}, ClipboardContentType::Text);
    Expect({CF_UNICODETEXT, kRichText}, ClipboardContentType::RichText);
    Expect({CF_UNICODETEXT, kRichText, CF_HDROP}, ClipboardContentType::Files);
    Expect({CF_UNICODETEXT, kRichText, CF_HDROP, CF_DIB}, ClipboardContentType::Image);
    Expect({CF_UNICODETEXT, kRichText, CF_ENHMETAFILE},
           ClipboardContentType::RichText);
    Expect({CF_UNICODETEXT, kHtml, CF_METAFILEPICT},
           ClipboardContentType::RichText);
    Expect({CF_UNICODETEXT, CF_ENHMETAFILE}, ClipboardContentType::Text);
    Expect({CF_TEXT, CF_METAFILEPICT}, ClipboardContentType::Text);
    Expect({kRichText, CF_BITMAP}, ClipboardContentType::Image);
    Expect(
        {kRichText, CF_UNICODETEXT, CF_BITMAP, CF_DIB, CF_DIBV5},
        ClipboardContentType::RichText);
    Expect(
        {
            kHtml,
            kRichText,
            CF_UNICODETEXT,
            CF_BITMAP,
            CF_ENHMETAFILE,
            CF_METAFILEPICT,
            CF_TEXT,
            CF_OEMTEXT,
            CF_DIB,
            CF_DIBV5,
        },
        ClipboardContentType::RichText);
    Expect(
        {kHtml, CF_UNICODETEXT, CF_BITMAP, CF_DIB},
        ClipboardContentType::Image);
    Expect({CF_ENHMETAFILE}, ClipboardContentType::Image);
    Expect({CF_METAFILEPICT}, ClipboardContentType::Image);
    Expect({kHtml}, ClipboardContentType::RichText);
    return 0;
}

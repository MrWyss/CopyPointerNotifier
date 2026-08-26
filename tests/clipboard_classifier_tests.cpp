#include "clipboard_classifier.hpp"
#include "glyph_scale.hpp"

#include <cassert>
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
    assert(ClassifyClipboardFormats(values, kRichText, kHtml) == expected);
}

}  // namespace

int main() {
    static_assert(EffectiveGlyphScalePercent(0) == 0.0F);
    static_assert(EffectiveGlyphScalePercent(50) == 50.0F);
    static_assert(EffectiveGlyphScalePercent(100) == 160.0F);

    Expect({}, std::nullopt);
    Expect({kUnknown}, ClipboardContentType::Object);
    Expect({CF_UNICODETEXT}, ClipboardContentType::Text);
    Expect({CF_UNICODETEXT, kRichText}, ClipboardContentType::RichText);
    Expect({CF_UNICODETEXT, kRichText, CF_HDROP}, ClipboardContentType::Files);
    Expect({CF_UNICODETEXT, kRichText, CF_HDROP, CF_DIB}, ClipboardContentType::Image);
    Expect({kHtml}, ClipboardContentType::RichText);
    return 0;
}

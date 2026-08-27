#include "clipboard_classifier.hpp"
#include "clipboard_rules.hpp"
#include "glyph_scale.hpp"

#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
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

void ExpectIndicator(
    std::vector<ClipboardRule> rules,
    std::initializer_list<UINT> formats,
    ClipboardContentType expectedType,
    std::wstring_view expectedGlyph = {}) {
    ClipboardRuleEngine engine(std::move(rules));
    const std::vector<UINT> values(formats);
    const ClipboardIndicator result = engine.Evaluate(values);
    if (result.contentType != expectedType ||
        result.glyph != expectedGlyph) {
        std::abort();
    }
}

ClipboardRule MakeRule(
    std::uint64_t id,
    std::wstring glyph,
    bool enabled,
    std::initializer_list<ClipboardRuleCondition> conditions) {
    return {
        id,
        L"Test rule",
        std::move(glyph),
        enabled,
        std::vector<ClipboardRuleCondition>(conditions),
    };
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

    if (!IsValidRuleGlyph(L"A") ||
        !IsValidRuleGlyph(L"RT") ||
        !IsValidRuleGlyph(L"\xD83D\xDCCB") ||
        !IsValidRuleGlyph(L"\x2764\xFE0F") ||
        !IsValidRuleGlyph(L"\x00A9\xFE0F") ||
        !IsValidRuleGlyph(L"\xD83D\xDC4D\xD83C\xDFFD") ||
        !IsValidRuleGlyph(L"\xD83C\xDDFA\xD83C\xDDF8") ||
        !IsValidRuleGlyph(L"1\xFE0F\x20E3") ||
        IsValidRuleGlyph(L"") ||
        IsValidRuleGlyph(L"ABC") ||
        IsValidRuleGlyph(L"\xD83D\xDCCB\xD83D\xDCAA") ||
        IsValidRuleGlyph(L"\xD83D\xDCCB" L"A") ||
        IsValidRuleGlyph(L"\xD83C\xDDFA\xD83C\xDDF8"
                         L"\xD83C\xDDE8\xD83C\xDDE6") ||
        IsValidRuleGlyph(L" ") ||
        IsValidRuleGlyph(L"\x00A0") ||
        IsValidRuleGlyph(L"\x3000") ||
        IsValidRuleGlyph(L"\xFE0F") ||
        IsValidRuleGlyph(L"\x200D") ||
        IsValidRuleGlyph(L"\xD83D")) {
        std::abort();
    }

    const ClipboardRuleCondition hasText{
        {CF_UNICODETEXT, {}}, true};
    const ClipboardRuleCondition noFiles{{CF_HDROP, {}}, false};
    ExpectIndicator(
        {MakeRule(1, L"C", true, {hasText, noFiles})},
        {CF_UNICODETEXT},
        ClipboardContentType::Object,
        L"C");
    ExpectIndicator(
        {MakeRule(1, L"C", true, {hasText, noFiles})},
        {CF_UNICODETEXT, CF_HDROP},
        ClipboardContentType::Files);
    ExpectIndicator(
        {MakeRule(1, L"C", false, {hasText})},
        {CF_UNICODETEXT},
        ClipboardContentType::Text);
    ExpectIndicator(
        {
            MakeRule(1, L"1", true, {hasText}),
            MakeRule(2, L"2", true, {hasText}),
        },
        {CF_UNICODETEXT},
        ClipboardContentType::Object,
        L"1");

    const UINT registered =
        RegisterClipboardFormatW(L"CopyPointerNotifier.TestFormat");
    const auto identity = IdentifyClipboardFormat(registered);
    if (!identity ||
        !identity->IsRegistered() ||
        identity->registeredName != L"CopyPointerNotifier.TestFormat" ||
        ClipboardFormatIdentityName(*identity) !=
            L"CopyPointerNotifier.TestFormat") {
        std::abort();
    }
    ExpectIndicator(
        {MakeRule(
            3,
            L"R",
            true,
            {{{0, L"CopyPointerNotifier.TestFormat"}, true}})},
        {registered},
        ClipboardContentType::Object,
        L"R");

    ClipboardRule conflicting =
        MakeRule(4, L"X", true, {hasText, hasText});
    if (ValidateClipboardRule(conflicting) ||
        ValidateClipboardRule(MakeRule(0, L"X", true, {hasText}))) {
        std::abort();
    }
    return 0;
}

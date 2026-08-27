#include "clipboard_rules.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>

namespace {

constexpr wchar_t kPreferencesKey[] =
    L"Software\\MrWyss\\CopyPointerNotifier";
constexpr wchar_t kRulesValue[] = L"ClipboardRules";
constexpr wchar_t kPendingRulesValue[] = L"ClipboardRules.Pending";
constexpr std::uint32_t kMagic = 0x52504E43;  // CNPR
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaximumRules = 256;
constexpr std::uint32_t kMaximumConditions = 256;
constexpr std::uint32_t kMaximumStringCharacters = 4096;

template <typename T>
void Append(std::vector<std::byte>& bytes, const T& value) {
    const auto* first = reinterpret_cast<const std::byte*>(&value);
    bytes.insert(bytes.end(), first, first + sizeof(T));
}

void AppendString(std::vector<std::byte>& bytes, std::wstring_view value) {
    const auto length = static_cast<std::uint32_t>(value.size());
    Append(bytes, length);
    const auto* first = reinterpret_cast<const std::byte*>(value.data());
    bytes.insert(bytes.end(), first, first + value.size() * sizeof(wchar_t));
}

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    template <typename T>
    bool Read(T& value) {
        if (offset_ > bytes_.size() || bytes_.size() - offset_ < sizeof(T)) {
            return false;
        }
        std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
        return true;
    }

    bool ReadString(std::wstring& value) {
        std::uint32_t length = 0;
        if (!Read(length) || length > kMaximumStringCharacters) {
            return false;
        }
        const size_t byteCount = static_cast<size_t>(length) * sizeof(wchar_t);
        if (offset_ > bytes_.size() || bytes_.size() - offset_ < byteCount) {
            return false;
        }
        value.assign(
            reinterpret_cast<const wchar_t*>(bytes_.data() + offset_),
            length);
        offset_ += byteCount;
        return true;
    }

    bool AtEnd() const noexcept {
        return offset_ == bytes_.size();
    }

private:
    std::span<const std::byte> bytes_;
    size_t offset_ = 0;
};

bool Contains(std::span<const UINT> formats, UINT format) {
    return format != 0 &&
        std::ranges::find(formats, format) != formats.end();
}

ClipboardIndicator EvaluateBuiltIns(std::span<const UINT> formats) {
    const UINT richTextFormat = RegisterClipboardFormatW(L"Rich Text Format");
    const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
    const auto contentType =
        ClassifyClipboardFormats(formats, richTextFormat, htmlFormat);
    return {*contentType, {}};
}

UINT ResolveFormat(const ClipboardFormatIdentity& identity) {
    return identity.IsRegistered()
        ? RegisterClipboardFormatW(identity.registeredName.c_str())
        : identity.standardFormat;
}

bool IsStandardClipboardFormat(UINT format) {
    switch (format) {
        case CF_TEXT:
        case CF_BITMAP:
        case CF_METAFILEPICT:
        case CF_SYLK:
        case CF_DIF:
        case CF_TIFF:
        case CF_OEMTEXT:
        case CF_DIB:
        case CF_PALETTE:
        case CF_PENDATA:
        case CF_RIFF:
        case CF_WAVE:
        case CF_UNICODETEXT:
        case CF_ENHMETAFILE:
        case CF_HDROP:
        case CF_LOCALE:
        case CF_DIBV5:
        case CF_OWNERDISPLAY:
        case CF_DSPTEXT:
        case CF_DSPBITMAP:
        case CF_DSPMETAFILEPICT:
        case CF_DSPENHMETAFILE:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool IsPersistableClipboardFormat(UINT format) {
    if (IsStandardClipboardFormat(format)) {
        return true;
    }
    if (format < 0xC000 || format > 0xFFFF) {
        return false;
    }
    wchar_t name[256]{};
    return GetClipboardFormatNameW(
        format, name, static_cast<int>(std::size(name))) > 0;
}

std::optional<ClipboardFormatIdentity> IdentifyClipboardFormat(UINT format) {
    if (IsStandardClipboardFormat(format)) {
        return ClipboardFormatIdentity{format, {}};
    }
    if (format < 0xC000 || format > 0xFFFF) {
        return std::nullopt;
    }
    wchar_t name[256]{};
    const int length = GetClipboardFormatNameW(
        format, name, static_cast<int>(std::size(name)));
    if (length <= 0) {
        return std::nullopt;
    }
    return ClipboardFormatIdentity{
        0, std::wstring{name, static_cast<size_t>(length)}};
}

std::wstring ClipboardFormatIdentityName(
    const ClipboardFormatIdentity& format) {
    return format.IsRegistered()
        ? format.registeredName
        : DescribeClipboardFormat(format.standardFormat);
}

namespace {

bool IsEmojiBase(char32_t value) {
    return value == 0x00A9 ||
        value == 0x00AE ||
        value == 0x203C ||
        value == 0x2049 ||
        value == 0x2122 ||
        value == 0x2139 ||
        (value >= 0x2194 && value <= 0x2199) ||
        (value >= 0x21A9 && value <= 0x21AA) ||
        (value >= 0x231A && value <= 0x231B) ||
        value == 0x2328 ||
        value == 0x23CF ||
        (value >= 0x23E9 && value <= 0x23F3) ||
        (value >= 0x23F8 && value <= 0x23FA) ||
        value == 0x24C2 ||
        (value >= 0x25AA && value <= 0x25AB) ||
        value == 0x25B6 ||
        value == 0x25C0 ||
        (value >= 0x25FB && value <= 0x25FE) ||
        (value >= 0x2600 && value <= 0x27BF) ||
        (value >= 0x2934 && value <= 0x2935) ||
        (value >= 0x2B05 && value <= 0x2B07) ||
        (value >= 0x2B1B && value <= 0x2B1C) ||
        value == 0x2B50 ||
        value == 0x2B55 ||
        value == 0x3030 ||
        value == 0x303D ||
        value == 0x3297 ||
        value == 0x3299 ||
        ((value >= 0x1F000 && value <= 0x1FAFF) &&
         !(value >= 0x1F3FB && value <= 0x1F3FF));
}

bool IsEmojiDecoration(char32_t value) {
    return value == 0xFE0F ||
        (value >= 0x1F3FB && value <= 0x1F3FF);
}

bool IsRegionalIndicator(char32_t value) {
    return value >= 0x1F1E6 && value <= 0x1F1FF;
}

bool IsInvisibleStandalone(char32_t value) {
    const bool isUnicodeWhitespace =
        (value >= 0x0009 && value <= 0x000D) ||
        value == 0x0020 ||
        value == 0x0085 ||
        value == 0x00A0 ||
        value == 0x1680 ||
        (value >= 0x2000 && value <= 0x200A) ||
        (value >= 0x2028 && value <= 0x2029) ||
        value == 0x202F ||
        value == 0x205F ||
        value == 0x3000;
    return isUnicodeWhitespace ||
        value < 0x20 ||
        (value >= 0x7F && value <= 0x9F) ||
        value == 0x200D ||
        value == 0x20E3 ||
        (value >= 0x0300 && value <= 0x036F) ||
        (value >= 0x200B && value <= 0x200F) ||
        (value >= 0x202A && value <= 0x202E) ||
        (value >= 0x2060 && value <= 0x206F) ||
        (value >= 0xFE00 && value <= 0xFE0F) ||
        (value >= 0xE0100 && value <= 0xE01EF) ||
        IsEmojiDecoration(value);
}

bool IsKeycapSequence(std::span<const char32_t> values) {
    if (values.size() < 2 || values.size() > 3 ||
        !((values[0] >= U'0' && values[0] <= U'9') ||
          values[0] == U'#' ||
          values[0] == U'*') ||
        values.back() != 0x20E3) {
        return false;
    }
        return values.size() == 2 || values[1] == 0xFE0F;
}

bool IsEmojiTagSequence(std::span<const char32_t> values) {
        if (values.size() < 3 ||
            values.front() != 0x1F3F4 ||
            values.back() != 0xE007F) {
            return false;
        }
        return std::ranges::all_of(
            values.subspan(1, values.size() - 2),
            [](char32_t value) {
                return value >= 0xE0020 && value <= 0xE007E;
            });
}

std::optional<std::vector<char32_t>> DecodeCodePoints(
    std::wstring_view glyph) {
    std::vector<char32_t> codePoints;
    for (size_t index = 0; index < glyph.size(); ++index) {
        const wchar_t value = glyph[index];
        if (value >= 0xD800 && value <= 0xDBFF) {
            if (index + 1 >= glyph.size() ||
                glyph[index + 1] < 0xDC00 ||
                glyph[index + 1] > 0xDFFF) {
                return std::nullopt;
            }
            const char32_t high = value - 0xD800;
            const char32_t low = glyph[++index] - 0xDC00;
            codePoints.push_back(0x10000 + (high << 10) + low);
        } else if (value >= 0xDC00 && value <= 0xDFFF) {
            return std::nullopt;
        } else {
            codePoints.push_back(value);
        }
    }
    return codePoints;
}

}  // namespace

bool IsEmojiRuleGlyph(std::wstring_view glyph) {
    const auto codePoints = DecodeCodePoints(glyph);
    return codePoints &&
        (IsKeycapSequence(*codePoints) ||
         IsEmojiTagSequence(*codePoints) ||
         std::ranges::any_of(*codePoints, IsEmojiBase));
}

bool IsValidRuleGlyph(std::wstring_view glyph) {
    if (glyph.empty()) {
        return false;
    }
    const auto codePoints = DecodeCodePoints(glyph);
    if (!codePoints || codePoints->empty()) {
        return false;
    }

    if (IsKeycapSequence(*codePoints)) {
        return true;
    }
    if (IsEmojiTagSequence(*codePoints)) {
        return true;
    }
    if (!std::ranges::any_of(*codePoints, IsEmojiBase)) {
        return codePoints->size() <= 2 &&
            std::ranges::none_of(
                *codePoints, IsInvisibleStandalone);
    }
    if (codePoints->size() == 2 &&
        IsRegionalIndicator((*codePoints)[0]) &&
        IsRegionalIndicator((*codePoints)[1])) {
        return true;
    }

    bool needsBase = true;
    bool hasBase = false;
    for (const char32_t value : *codePoints) {
        if (IsEmojiBase(value)) {
            if (IsRegionalIndicator(value) || !needsBase) {
                return false;
            }
            needsBase = false;
            hasBase = true;
        } else if (IsEmojiDecoration(value)) {
            if (needsBase) {
                return false;
            }
        } else if (value == 0x200D) {
            if (needsBase) {
                return false;
            }
            needsBase = true;
        } else {
            return false;
        }
    }
    return hasBase && !needsBase;
}

const wchar_t* GlyphFontFamilyName(std::wstring_view glyph) {
    const auto codePoints = DecodeCodePoints(glyph);
    if (codePoints &&
        (IsKeycapSequence(*codePoints) ||
         IsEmojiTagSequence(*codePoints) ||
         std::ranges::any_of(*codePoints, IsEmojiBase))) {
        return L"Segoe UI Emoji";
    }
    for (const wchar_t value : glyph) {
        if ((value >= 0x2190 && value <= 0x25FF) ||
            (value >= 0xE000 && value <= 0xF8FF)) {
            return L"Segoe UI Symbol";
        }
    }
    return L"Segoe UI";
}

bool ValidateClipboardRule(const ClipboardRule& rule) {
    if (rule.id == 0 ||
        rule.name.empty() ||
        rule.name.size() > kMaximumStringCharacters ||
        !IsValidRuleGlyph(rule.glyph) ||
        rule.conditions.empty() ||
        rule.conditions.size() > kMaximumConditions) {
        return false;
    }

    std::set<std::pair<UINT, std::wstring>> seen;
    for (const auto& condition : rule.conditions) {
        const auto& format = condition.format;
        if (format.IsRegistered()) {
            if (format.standardFormat != 0 ||
                format.registeredName.size() > kMaximumStringCharacters) {
                return false;
            }
        } else if (!IsStandardClipboardFormat(format.standardFormat)) {
            return false;
        }
        if (!seen.emplace(
                format.standardFormat,
                format.registeredName).second) {
            return false;
        }
    }
    return true;
}

ClipboardRuleEngine::ClipboardRuleEngine(std::vector<ClipboardRule> rules) {
    rules_.reserve(rules.size());
    compiledRules_.reserve(rules.size());
    for (auto& rule : rules) {
        if (!ValidateClipboardRule(rule)) {
            continue;
        }
        CompiledRule compiled;
        compiled.rule = rule;
        compiled.conditions.reserve(rule.conditions.size());
        bool resolved = true;
        for (const auto& condition : rule.conditions) {
            const UINT format = ResolveFormat(condition.format);
            if (format == 0) {
                resolved = false;
                break;
            }
            compiled.conditions.push_back(
                {format, condition.mustBePresent});
        }
        if (!resolved) {
            continue;
        }
        rules_.push_back(rule);
        compiledRules_.push_back(std::move(compiled));
    }
}

ClipboardIndicator ClipboardRuleEngine::Evaluate(
    std::span<const UINT> formats) const {
    if (formats.empty()) {
        return {ClipboardContentType::Object, {}};
    }

    for (const auto& compiled : compiledRules_) {
        if (!compiled.rule.enabled) {
            continue;
        }
        const bool matches = std::ranges::all_of(
            compiled.conditions,
            [formats](const CompiledCondition& condition) {
                const bool present = Contains(formats, condition.format);
                return condition.mustBePresent ? present : !present;
            });
        if (matches) {
            return {ClipboardContentType::Object, compiled.rule.glyph};
        }
    }
    return EvaluateBuiltIns(formats);
}

const std::vector<ClipboardRule>& ClipboardRuleEngine::Rules() const noexcept {
    return rules_;
}

std::vector<ClipboardRule> LoadClipboardRules() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(
            HKEY_CURRENT_USER,
            kPreferencesKey,
            0,
            KEY_QUERY_VALUE,
            &key) != ERROR_SUCCESS) {
        return {};
    }

    DWORD type = 0;
    DWORD size = 0;
    LONG status = RegQueryValueExW(
        key, kRulesValue, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || type != REG_BINARY || size == 0) {
        RegCloseKey(key);
        return {};
    }

    std::vector<std::byte> bytes(size);
    status = RegQueryValueExW(
        key,
        kRulesValue,
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(bytes.data()),
        &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return {};
    }

    Reader reader(bytes);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if (!reader.Read(magic) ||
        !reader.Read(version) ||
        !reader.Read(count) ||
        magic != kMagic ||
        version != kVersion ||
        count > kMaximumRules) {
        return {};
    }

    std::vector<ClipboardRule> rules;
    rules.reserve(count);
    std::set<std::uint64_t> ids;
    for (std::uint32_t index = 0; index < count; ++index) {
        ClipboardRule rule;
        std::uint8_t enabled = 0;
        std::uint32_t conditionCount = 0;
        if (!reader.Read(rule.id) ||
            !reader.Read(enabled) ||
            !reader.ReadString(rule.name) ||
            !reader.ReadString(rule.glyph) ||
            !reader.Read(conditionCount) ||
            conditionCount > kMaximumConditions) {
            return {};
        }
        rule.enabled = enabled != 0;
        rule.conditions.reserve(conditionCount);
        for (std::uint32_t conditionIndex = 0;
             conditionIndex < conditionCount;
             ++conditionIndex) {
            ClipboardRuleCondition condition;
            std::uint8_t mustBePresent = 0;
            std::uint8_t registered = 0;
            if (!reader.Read(mustBePresent) || !reader.Read(registered)) {
                return {};
            }
            condition.mustBePresent = mustBePresent != 0;
            if (registered != 0) {
                if (!reader.ReadString(condition.format.registeredName)) {
                    return {};
                }
            } else if (!reader.Read(condition.format.standardFormat)) {
                return {};
            }
            rule.conditions.push_back(std::move(condition));
        }
        if (!ValidateClipboardRule(rule) || !ids.insert(rule.id).second) {
            return {};
        }
        rules.push_back(std::move(rule));
    }
    return reader.AtEnd() ? rules : std::vector<ClipboardRule>{};
}

bool SaveClipboardRules(std::span<const ClipboardRule> rules) {
    if (rules.size() > kMaximumRules) {
        return false;
    }
    std::set<std::uint64_t> ids;
    for (const auto& rule : rules) {
        if (!ValidateClipboardRule(rule) || !ids.insert(rule.id).second) {
            return false;
        }
    }

    std::vector<std::byte> bytes;
    Append(bytes, kMagic);
    Append(bytes, kVersion);
    Append(bytes, static_cast<std::uint32_t>(rules.size()));
    for (const auto& rule : rules) {
        Append(bytes, rule.id);
        Append(bytes, static_cast<std::uint8_t>(rule.enabled ? 1 : 0));
        AppendString(bytes, rule.name);
        AppendString(bytes, rule.glyph);
        Append(bytes, static_cast<std::uint32_t>(rule.conditions.size()));
        for (const auto& condition : rule.conditions) {
            Append(
                bytes,
                static_cast<std::uint8_t>(
                    condition.mustBePresent ? 1 : 0));
            Append(
                bytes,
                static_cast<std::uint8_t>(
                    condition.format.IsRegistered() ? 1 : 0));
            if (condition.format.IsRegistered()) {
                AppendString(bytes, condition.format.registeredName);
            } else {
                Append(bytes, condition.format.standardFormat);
            }
        }
    }

    if (bytes.size() > std::numeric_limits<DWORD>::max()) {
        return false;
    }
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kPreferencesKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD size = static_cast<DWORD>(bytes.size());
    LONG status = RegSetValueExW(
        key,
        kPendingRulesValue,
        0,
        REG_BINARY,
        reinterpret_cast<const BYTE*>(bytes.data()),
        size);
    if (status == ERROR_SUCCESS) {
        status = RegSetValueExW(
            key,
            kRulesValue,
            0,
            REG_BINARY,
            reinterpret_cast<const BYTE*>(bytes.data()),
            size);
    }
    RegDeleteValueW(key, kPendingRulesValue);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

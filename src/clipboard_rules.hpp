#pragma once

#include "clipboard_classifier.hpp"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct ClipboardFormatIdentity {
    UINT standardFormat = 0;
    std::wstring registeredName;

    bool IsRegistered() const noexcept {
        return !registeredName.empty();
    }
};

struct ClipboardRuleCondition {
    ClipboardFormatIdentity format;
    bool mustBePresent = true;
};

struct ClipboardRule {
    std::uint64_t id = 0;
    std::wstring name;
    std::wstring glyph;
    bool enabled = true;
    std::vector<ClipboardRuleCondition> conditions;
};

struct ClipboardIndicator {
    ClipboardContentType contentType = ClipboardContentType::Object;
    std::wstring glyph;

    bool IsCustom() const noexcept {
        return !glyph.empty();
    }
};

bool IsPersistableClipboardFormat(UINT format);
std::optional<ClipboardFormatIdentity> IdentifyClipboardFormat(UINT format);
std::wstring ClipboardFormatIdentityName(const ClipboardFormatIdentity& format);
bool IsEmojiRuleGlyph(std::wstring_view glyph);
bool IsValidRuleGlyph(std::wstring_view glyph);
const wchar_t* GlyphFontFamilyName(std::wstring_view glyph);
bool ValidateClipboardRule(const ClipboardRule& rule);

class ClipboardRuleEngine {
public:
    ClipboardRuleEngine() = default;
    explicit ClipboardRuleEngine(std::vector<ClipboardRule> rules);

    ClipboardIndicator Evaluate(std::span<const UINT> formats) const;
    const std::vector<ClipboardRule>& Rules() const noexcept;

private:
    struct CompiledCondition {
        UINT format = 0;
        bool mustBePresent = true;
    };

    struct CompiledRule {
        ClipboardRule rule;
        std::vector<CompiledCondition> conditions;
    };

    std::vector<ClipboardRule> rules_;
    std::vector<CompiledRule> compiledRules_;
};

std::vector<ClipboardRule> LoadClipboardRules();
bool SaveClipboardRules(std::span<const ClipboardRule> rules);

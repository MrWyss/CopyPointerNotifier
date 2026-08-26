#pragma once

#include <windows.h>

#include <optional>
#include <span>
#include <vector>

enum class ClipboardContentType {
    Image,
    Files,
    RichText,
    Text,
    Object,
};

struct ClipboardReadResult {
    bool clipboardOpened;
    std::optional<ClipboardContentType> contentType;
};

std::optional<ClipboardContentType> ClassifyClipboardFormats(
    std::span<const UINT> formats,
    UINT richTextFormat,
    UINT htmlFormat);

ClipboardReadResult ReadClipboardContentType(HWND owner);

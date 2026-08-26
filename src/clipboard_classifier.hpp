#pragma once

#include <windows.h>

#include <optional>
#include <span>
#include <string>
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
    std::vector<UINT> formats;
};

std::wstring DescribeClipboardFormat(UINT format);

std::optional<ClipboardContentType> ClassifyClipboardFormats(
    std::span<const UINT> formats,
    UINT richTextFormat,
    UINT htmlFormat);

ClipboardReadResult ReadClipboardContentType(HWND owner);

#include "startup_registration.hpp"

#include "glyph_scale.hpp"

#include <appmodel.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace {

constexpr wchar_t kApplicationName[] = L"CopyPointerNotifier";
constexpr wchar_t kStartupTaskId[] = L"CopyPointerNotifierStartup";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kPreferencesKey[] = L"Software\\MrWyss\\CopyPointerNotifier";

bool HasPackageIdentity() {
    UINT32 length = 0;
    return GetCurrentPackageFullName(&length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
}

bool DeleteUnpackagedStartupEntry() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return true;
    }

    LSTATUS status = RegDeleteValueW(key, kApplicationName);
    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

std::wstring ExecutableCommand() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (length == path.size() && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        path.resize(path.size() * 2);
        length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    if (length == 0) {
        return {};
    }
    path.resize(length);
    return L"\"" + path + L"\"";
}

}  // namespace

bool IsStartWithWindowsEnabled() {
    if (HasPackageIdentity()) {
        try {
            const auto task =
                winrt::Windows::ApplicationModel::StartupTask::GetAsync(kStartupTaskId).get();
            const auto state = task.State();
            return state == winrt::Windows::ApplicationModel::StartupTaskState::Enabled ||
                   state == winrt::Windows::ApplicationModel::StartupTaskState::EnabledByPolicy;
        } catch (const winrt::hresult_error&) {
            return false;
        }
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    const LSTATUS status = RegQueryValueExW(
        key,
        kApplicationName,
        nullptr,
        nullptr,
        nullptr,
        nullptr);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool SetStartWithWindowsEnabled(bool enabled) {
    if (HasPackageIdentity()) {
        try {
            if (!DeleteUnpackagedStartupEntry()) {
                return false;
            }

            const auto task =
                winrt::Windows::ApplicationModel::StartupTask::GetAsync(kStartupTaskId).get();
            if (!enabled) {
                task.Disable();
                return true;
            }

            const auto state = task.RequestEnableAsync().get();
            return state == winrt::Windows::ApplicationModel::StartupTaskState::Enabled ||
                   state == winrt::Windows::ApplicationModel::StartupTaskState::EnabledByPolicy;
        } catch (const winrt::hresult_error&) {
            return false;
        }
    }

    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kRunKey,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }

    LSTATUS status = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring command = ExecutableCommand();
        if (command.empty()) {
            RegCloseKey(key);
            return false;
        }
        status = RegSetValueExW(
            key,
            kApplicationName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kApplicationName);
        if (status == ERROR_FILE_NOT_FOUND) {
            status = ERROR_SUCCESS;
        }
    }

    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool LoadApplicationEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPreferencesKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return true;
    }

    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(
        key,
        L"Enabled",
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(&value),
        &size);
    RegCloseKey(key);
    return status != ERROR_SUCCESS || type != REG_DWORD || value != 0;
}

bool SaveApplicationEnabled(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kPreferencesKey,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD value = enabled ? 1 : 0;
    const LSTATUS status = RegSetValueExW(
        key,
        L"Enabled",
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&value),
        sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

GlyphPosition LoadGlyphPosition() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPreferencesKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return {};
    }

    DWORD x = 30;
    DWORD y = 50;
    DWORD size = sizeof(DWORD);
    DWORD type = 0;
    const LSTATUS xStatus = RegQueryValueExW(
        key,
        L"GlyphXPercent",
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(&x),
        &size);
    size = sizeof(DWORD);
    const LSTATUS yStatus = RegQueryValueExW(
        key,
        L"GlyphYPercent",
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(&y),
        &size);
    RegCloseKey(key);
    if (xStatus != ERROR_SUCCESS || yStatus != ERROR_SUCCESS || type != REG_DWORD) {
        return {};
    }
    return {
        std::clamp(static_cast<int>(static_cast<std::int32_t>(x)), -125, 125),
        std::clamp(static_cast<int>(static_cast<std::int32_t>(y)), -125, 125),
    };
}

int LoadGlyphScalePercent() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPreferencesKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return 50;
    }

    DWORD value = 50;
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(
        key,
        L"GlyphScalePercent",
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(&value),
        &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_DWORD) {
        return 50;
    }
    return std::clamp(
        static_cast<int>(value),
        kMinimumGlyphScale,
        kMaximumGlyphScale);
}

AnimationStyle LoadAnimationStyle() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            kPreferencesKey,
            L"AnimationStyle",
            RRF_RT_REG_DWORD,
            nullptr,
            &value,
            &size) != ERROR_SUCCESS) {
        return AnimationStyle::FadeAndShrink;
    }
    return NormalizeAnimationStyle(static_cast<int>(value));
}

int LoadAnimationSpeed() {
    DWORD value = 50;
    DWORD size = sizeof(value);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            kPreferencesKey,
            L"AnimationSpeed",
            RRF_RT_REG_DWORD,
            nullptr,
            &value,
            &size) != ERROR_SUCCESS) {
        return 50;
    }
    return std::clamp(static_cast<int>(value), 0, 100);
}

int LoadIndicatorVisibilityDuration() {
    DWORD mode = 0;
    DWORD size = sizeof(mode);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            kPreferencesKey,
            L"IndicatorVisibilityMode",
            RRF_RT_REG_DWORD,
            nullptr,
            &mode,
            &size) != ERROR_SUCCESS ||
        mode == 0) {
        return 0;
    }

    DWORD duration = 5;
    size = sizeof(duration);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            kPreferencesKey,
            L"IndicatorDurationSeconds",
            RRF_RT_REG_DWORD,
            nullptr,
            &duration,
            &size) != ERROR_SUCCESS) {
        duration = 5;
    }
    return std::clamp(static_cast<int>(duration), 1, 60) * 1000;
}

// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "platform/text.h"

#include "platform/windows_config.h"

#include <windows.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <locale.h>

namespace thread_sim::platform
{

std::string WideToUtf8(std::wstring_view text)
{
    if (text.empty() || text.size() > static_cast<size_t>(INT_MAX))
    {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string output(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), size, nullptr, nullptr);
    return output;
}

std::optional<std::wstring> Utf8ToWide(std::string_view text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    if (text.size() > static_cast<size_t>(INT_MAX))
    {
        return std::nullopt;
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
    {
        return std::nullopt;
    }
    std::wstring output(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), output.data(), size);
    return output;
}

std::wstring Trim(std::wstring_view text)
{
    size_t first = 0;
    while (first < text.size() && (text[first] == L' ' || text[first] == L'\t'))
    {
        ++first;
    }
    size_t last = text.size();
    while (last > first && (text[last - 1] == L' ' || text[last - 1] == L'\t'))
    {
        --last;
    }
    return std::wstring(text.substr(first, last - first));
}

std::optional<uint64_t> ParseUnsigned(std::wstring_view text)
{
    const std::wstring trimmed = Trim(text);
    if (trimmed.empty() || trimmed.size() > 20)
    {
        return std::nullopt;
    }
    uint64_t value = 0;
    for (wchar_t ch : trimmed)
    {
        if (ch < L'0' || ch > L'9')
        {
            return std::nullopt;
        }
        const uint64_t digit = static_cast<uint64_t>(ch - L'0');
        if (value > (UINT64_MAX - digit) / 10)
        {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    return value;
}

std::wstring FormatUnsigned(uint64_t value)
{
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%llu", static_cast<unsigned long long>(value));
    return buffer;
}

std::wstring FormatFixed(double value, int digits)
{
    if (!std::isfinite(value))
    {
        return L"";
    }
    static const _locale_t invariant = _create_locale(LC_NUMERIC, "C");
    wchar_t buffer[64] = {};
    _swprintf_s_l(buffer, std::size(buffer), L"%.*f", invariant, digits, value);
    return buffer;
}

std::wstring FormatHex(uint64_t value, int min_digits)
{
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"0x%0*llX", min_digits, static_cast<unsigned long long>(value));
    return buffer;
}

} // namespace thread_sim::platform

// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace thread_sim::platform
{

std::string WideToUtf8(std::wstring_view text);
std::optional<std::wstring> Utf8ToWide(std::string_view text);

std::wstring Trim(std::wstring_view text);

std::optional<uint64_t> ParseUnsigned(std::wstring_view text);

std::wstring FormatUnsigned(uint64_t value);
std::wstring FormatFixed(double value, int digits);
std::wstring FormatHex(uint64_t value, int min_digits = 0);

} // namespace thread_sim::platform

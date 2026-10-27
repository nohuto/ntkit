// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "platform/error.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace thread_sim::platform
{

inline constexpr uint64_t kMaxConfigFileBytes = 4ull * 1024ull * 1024ull;

Result<std::string> ReadFileBytes(const std::wstring& path, uint64_t max_bytes);
Result<std::wstring> ReadUtf8File(const std::wstring& path, uint64_t max_bytes = kMaxConfigFileBytes);
Status WriteFileAtomic(const std::wstring& path, std::string_view bytes);
Status WriteUtf8FileAtomic(const std::wstring& path, std::wstring_view text);
Status EnsureDirectory(const std::wstring& path);

std::wstring JoinPath(std::wstring_view directory, std::wstring_view name);
std::wstring DefaultDataDirectory();

} // namespace thread_sim::platform

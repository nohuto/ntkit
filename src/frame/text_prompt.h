// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace thread_sim::frame
{

// text is initial value and output, false on cancel
bool PromptText(HWND owner, std::wstring_view title, std::wstring_view label, std::wstring* text);

} // namespace thread_sim::frame

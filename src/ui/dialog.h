// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ui/theme.h"

#include "platform/error.h"

#include <win32ui/dialog/dialog_window.h>
#include <win32ui/dialog/message_dialog.h>

#include <windows.h>

#include <commctrl.h>

#include <string>
#include <string_view>
#include <vector>

namespace thread_sim::ui
{

inline constexpr wchar_t kDialogClass[] = L"thread-sim.Dialog";

void RegisterDialogClass(HINSTANCE instance, int icon);

INT_PTR ShowMessage(HWND owner, win32ui::MessageSpec spec);
void ShowAbout(HWND owner, std::wstring_view title, std::vector<std::wstring> lines);
void ShowError(HWND owner, std::wstring_view title, std::wstring_view summary, const platform::Error& error);

enum class SaveChoice
{
    kSave,
    kDiscard,
    kCancel,
};
SaveChoice AskSaveChanges(HWND owner, std::wstring_view document_name);

} // namespace thread_sim::ui

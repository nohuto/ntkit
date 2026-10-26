// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

// app theme hooks, list labels and grid icons for win32ui

#pragma once

#include <win32ui/dialog/dialog_layout.h>
#include <win32ui/list_view/list_view.h>
#include <win32ui/status_bar/status_bar.h>
#include <win32ui/theme/theme.h>
#include <win32ui/toolbar/toolbar.h>

#include <windows.h>

#include <functional>

namespace thread_sim::ui
{

using win32ui::SetTextRole;
using win32ui::TextRole;

// dialog theme, lives for the whole run
const win32ui::DialogTheme* DialogThemeHooks();
// list frame and menu labels, live for the whole run
const win32ui::ListViewHooks* ListHooks();
// toolbar frame, window supplies icons, tips and dropdown
win32ui::ToolbarHooks ToolbarHooks();
// status bar frame, window supplies tips and messages
win32ui::StatusBarHooks StatusBarHooks();
// dark/light grid icons and shared grid toggle
void SetListGrid(int dark_lines_icon, int light_lines_icon, std::function<void(bool)> changed);

} // namespace thread_sim::ui

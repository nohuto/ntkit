// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "platform/error.h"

#include <win32ui/theme/theme.h>

#include <string>
#include <vector>

namespace thread_sim::frame
{

// store layout in 96 DPI units across DPI changes
// window rect uses physical pixels plus capture DPI
struct UiSettings
{
    win32ui::ThemeMode theme = win32ui::ThemeMode::system;
    // custom preset name in data folder theme_presets.rktheme
    std::wstring theme_preset;
    bool has_window = false;
    RECT window = {};
    UINT window_dpi = 96;
    bool maximized = false;
    std::vector<int> column_widths_dip;
    std::wstring last_directory;
    bool show_all_threads = true;
    bool show_grid_lines = false;
    bool show_toolbar = true;
    bool show_status_bar = true;
};

platform::Result<UiSettings> LoadUiSettings(const std::wstring& path);
platform::Status SaveUiSettings(const std::wstring& path, const UiSettings& settings);

} // namespace thread_sim::frame

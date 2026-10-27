// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

namespace thread_sim::frame
{

struct AppOptions
{
    std::wstring data_dir;
    std::wstring config_path;
    // dev builds only, otherwise command line parsing rejects these
    std::optional<size_t> dev_fail_setter;
    UINT dev_dpi = 0;
    int dev_font_points = 0;
    uint16_t dev_groups = 0;
    std::wstring dev_perf_log;
};

int RunApplication(HINSTANCE instance, const AppOptions& options, int show_command);

} // namespace thread_sim::frame

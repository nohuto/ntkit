// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/theme.h"

#include <win32ui/dialog/single_line_edit.h>
#include <win32ui/gdi/icon_loader.h>

namespace thread_sim::ui
{

const win32ui::DialogTheme* DialogThemeHooks()
{
    static const win32ui::DialogTheme hooks = [] {
        win32ui::DialogTheme theme = win32ui::ThemeDialogHooks();
        theme.apply = [](HWND dialog) {
            win32ui::ApplyThemeToWindow(dialog);
            win32ui::AddSelectAllShortcut(dialog);
        };
        return theme;
    }();
    return &hooks;
}

namespace
{

int g_grid_icons[2] = {};
std::function<void(bool)> g_grid_changed;

} // namespace

void SetListGrid(int dark_lines_icon, int light_lines_icon, std::function<void(bool)> changed)
{
    g_grid_icons[0] = dark_lines_icon;
    g_grid_icons[1] = light_lines_icon;
    g_grid_changed = std::move(changed);
}

const win32ui::ListViewHooks* ListHooks()
{
    static const win32ui::ListViewHooks hooks = {
        .label = [](win32ui::ListLabel label) -> const wchar_t* {
            switch (label)
            {
            case win32ui::ListLabel::fit_column:
                return L"Size column to fit";
            case win32ui::ListLabel::fit_all:
                return L"Size all columns to fit";
            case win32ui::ListLabel::reset_columns:
                return L"Reset columns";
            case win32ui::ListLabel::copy:
                return L"&Copy";
            case win32ui::ListLabel::select_all:
                return L"Select &all";
            case win32ui::ListLabel::grid_lines:
                return L"Grid lines";
            case win32ui::ListLabel::more_text:
                return L"...";
            }
            return L"";
        },
        .frame = DialogThemeHooks(),
        .grid_icon = [](UINT dpi) -> HICON {
            const int icon = g_grid_icons[win32ui::ThemeLightGlyphs() ? 1 : 0];
            return icon ? win32ui::LoadIconResource(icon, dpi) : nullptr;
        },
        .grid_changed = [](bool shown) {
            if (g_grid_changed)
            {
                g_grid_changed(shown);
            } },
    };
    return &hooks;
}

win32ui::ToolbarHooks ToolbarHooks()
{
    win32ui::ToolbarHooks hooks;
    hooks.frame = DialogThemeHooks();
    return hooks;
}

win32ui::StatusBarHooks StatusBarHooks()
{
    win32ui::StatusBarHooks hooks;
    hooks.frame = DialogThemeHooks();
    return hooks;
}

} // namespace thread_sim::ui

// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/main_window.h"

#include "frame/window_detail.h"

#include "ui/controls.h"
#include "ui/dialog.h"
#include "ui/theme.h"

#include "platform/clock.h"

#include "resource.h"

#include <win32ui/gdi/fonts.h>
#include <win32ui/gdi/icon_loader.h>
#include <win32ui/list_view/list_view.h>
#include <win32ui/window_metrics.h>

#include <algorithm>

namespace thread_sim::frame
{

using namespace ui;
using namespace window_detail;

void MainWindow::CreateChildren()
{
    dpi_ = win32ui::WindowDpi(hwnd_);
    fonts_.Create(dpi_);
    menu_.Build(hwnd_);
    CreateToolbar();
    // shared grid setting for table and dialog lists, save on change
    win32ui::SetAllListGridLines(settings_.show_grid_lines);
    SetListGrid(IDI_LIST_GRID_DARK, IDI_LIST_GRID_LIGHT, [this](bool shown) {
        settings_.show_grid_lines = shown;
        SaveSettings();
    });
    table_.Create(hwnd_, ids::kThreadTable);
    status_hooks_ = StatusBarHooks();
    status_hooks_.context = this;
    // first part shows run details, others show full text when truncated
    status_hooks_.part_tip = [](void* context, int part) { return part == 0 ? static_cast<const MainWindow*>(context)->RunTipText() : std::wstring(); };
    status_hooks_.message_ended = [](void* context) { static_cast<MainWindow*>(context)->UpdateStatus(); };
    status_ = win32ui::CreateStatusBar(hwnd_, ids::kStatusBar, L"Status", &status_hooks_);
    // size for typical text, last part fills the rest
    win32ui::SetStatusParts(status_, {{L"Stopping 99999.9 of 86400 s"}, {L"256 threads, 256 active, 256 selected"}, {L"PID 999999, Below normal, CPU 9999.9 %"}, {L"", 0, true}});

    children_ready_ = true;
    ShowBars();
    ApplyFontsAndMetrics();
    if (settings_.column_widths_dip.empty())
    {
        table_.ResetColumns(dpi_);
    }
    else
    {
        table_.SetColumnWidths(settings_.column_widths_dip, dpi_);
    }
    ApplyTheme();
    window_alive_ = true;
    post_target_.store(hwnd_, std::memory_order_release);

    Revalidate();
    ResetThreadKeys();
    table_.SetData(&config_, nullptr);
    table_.RefreshAll();
    table_.SelectThread(0, true);
    OnSelectionChanged();
    UpdateTitle();
    UpdateCommandStates();
    UpdateStatus();
}

void MainWindow::CreateToolbar()
{
    toolbar_hooks_ = ToolbarHooks();
    toolbar_hooks_.context = this;
    toolbar_hooks_.icon = [](void*, int command, UINT dpi) -> HICON {
        const auto button = std::ranges::find(kToolButtons, command, &ToolButton::command);
        return button != std::end(kToolButtons) ? win32ui::LoadIconResource(button->icon, dpi) : nullptr;
    };
    toolbar_hooks_.tip = [](void* context, int command) { return static_cast<const MainWindow*>(context)->ToolTipText(command); };
    toolbar_hooks_.shortcut = [](int command) { return ShortcutText(command); };
    toolbar_hooks_.dropdown = [](void* context, int command, RECT button) {
        if (command == cmd::kRunStart)
        {
            static_cast<MainWindow*>(context)->ShowDurationMenu(button);
        }
    };
    std::vector<win32ui::ToolbarButton> buttons;
    for (const auto& button : kToolButtons)
    {
        buttons.push_back({button.command, button.text ? button.text : L"", button.command == cmd::kRunStart});
    }
    toolbar_ = win32ui::CreateToolbar(hwnd_, ids::kToolbar, L"Toolbar", &toolbar_hooks_, buttons);
}

void MainWindow::ReplaceFonts(UINT dpi)
{
    win32ui::FontSet next;
    next.Create(dpi);
    std::swap(fonts_, next);
    ApplyFontsAndMetrics();
}

void MainWindow::ApplyFontsAndMetrics()
{
    ApplyFonts(hwnd_, fonts_);
    win32ui::RefreshToolbar(toolbar_);
    win32ui::RefreshStatusBar(status_);
    menu_.bar().ApplyTheme();
}

void MainWindow::ApplyTheme()
{
    const int64_t start = platform::QpcClock::Now();
    menu_.bar().ApplyTheme();
    win32ui::ReapplyTheme(hwnd_);
    perf_.Event("theme_apply", ElapsedMs(start), "dark", win32ui::ThemeDark() ? 1 : 0);
}

void MainWindow::Layout()
{
    if (!hwnd_ || !children_ready_ || IsIconic(hwnd_))
    {
        return;
    }
    RECT client = {};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    // move all children without redraw, then repaint once
    // avoids copying pixels from moved siblings and completes each resize frame
    const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW;
    const int toolbar_height = settings_.show_toolbar ? win32ui::ToolbarHeight(toolbar_) : 0;
    SetWindowPos(toolbar_, nullptr, 0, 0, width, toolbar_height, flags);
    SendMessageW(status_, WM_SIZE, 0, 0);
    RECT status_rect = {};
    GetWindowRect(status_, &status_rect);
    const int status_height = settings_.show_status_bar ? status_rect.bottom - status_rect.top : 0;
    SetWindowPos(table_.hwnd(), nullptr, 0, toolbar_height, width, std::max(0, height - status_height - toolbar_height), flags);
    // parts exist only after SB_SETPARTS, set text afterward
    UpdateStatus();
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
}

void MainWindow::ShowBars()
{
    // hidden toolbars cannot keep focus
    if (!settings_.show_toolbar && GetFocus() == toolbar_)
    {
        SetFocus(table_.hwnd());
    }
    ShowWindow(toolbar_, settings_.show_toolbar ? SW_SHOWNA : SW_HIDE);
    ShowWindow(status_, settings_.show_status_bar ? SW_SHOWNA : SW_HIDE);
    Layout();
}

void MainWindow::OnDpiChanged(UINT dpi, const RECT* suggested)
{
    const UINT old_dpi = dpi_;
    dpi_ = dpi;
    table_.Rescale(old_dpi, dpi_);
    ReplaceFonts(dpi_);
    win32ui::RefreshListView(table_.hwnd());
    if (suggested)
    {
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    Layout();
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

} // namespace thread_sim::frame

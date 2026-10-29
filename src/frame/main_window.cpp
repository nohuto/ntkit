// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/main_window.h"

#include "frame/activity_dialog.h"
#include "frame/control_ids.h"
#include "frame/formatting.h"
#include "frame/processor_picker.h"
#include "frame/run_value_dialog.h"
#include "frame/window_detail.h"

#include "ui/controls.h"
#include "ui/dialog.h"
#include "ui/theme.h"

#include "engine/workload_backend.h"

#include "platform/clock.h"
#include "platform/environment.h"
#include "platform/text.h"

#include <win32ui/dialog/single_line_edit.h>
#include <win32ui/gdi/buffered_paint.h>
#include <win32ui/list_view/list_view.h>
#include <win32ui/menu/popup_menu.h>
#include <win32ui/process_token.h>
#include <win32ui/window_metrics.h>

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

namespace thread_sim::frame
{

using namespace ui;
using namespace window_detail;

namespace
{

const LPCWSTR kLastFocusProp = MAKEINTATOM(GlobalAddAtomW(L"thread-sim.LastFocus"));

} // namespace

double window_detail::ElapsedMs(int64_t start)
{
    return static_cast<double>(platform::QpcClock::Now() - start) * 1000.0 / static_cast<double>(platform::QpcClock::Frequency());
}

double MsSinceProcessStart()
{
    FILETIME creation = {}, exit = {}, kernel = {}, user = {};
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
    FILETIME now = {};
    GetSystemTimePreciseAsFileTime(&now);
    return static_cast<double>(platform::FileTimeTo100ns(now) - platform::FileTimeTo100ns(creation)) / 10000.0;
}

PerfLog::~PerfLog()
{
    if (file_)
    {
        fclose(file_);
    }
}

bool PerfLog::Open(const std::wstring& path)
{
    file_ = _wfsopen(path.c_str(), L"ab", _SH_DENYWR);
    return file_ != nullptr;
}

void PerfLog::Event(const char* name, double milliseconds, const char* detail_key, long long detail)
{
    if (!file_)
    {
        return;
    }
    char line[256] = {};
    if (detail_key)
    {
        _snprintf_s(line, _TRUNCATE, "{\"event\":\"%s\",\"ms\":%.4f,\"%s\":%lld}\n", name, milliseconds, detail_key, detail);
    }
    else
    {
        _snprintf_s(line, _TRUNCATE, "{\"event\":\"%s\",\"ms\":%.4f}\n", name, milliseconds);
    }
    fputs(line, file_);
    fflush(file_);
}

void PerfLog::Raw(const std::string& line)
{
    if (file_)
    {
        fputs(line.c_str(), file_);
        fputs("\n", file_);
        fflush(file_);
    }
}

MainWindow::MainWindow(const AppOptions& options, ApplicationEnvironment environment)
    : options_(options), environment_(std::move(environment))
{
    engine::BackendFactory factory = [this] {
        engine::WorkloadOptions workload;
        workload.force_setter_failure_thread = options_.dev_fail_setter;
        return engine::MakeWorkloadBackend(workload);
    };
    controller_ = std::make_unique<engine::RunController>(std::move(factory), this);
    config_ = engine::MakeDefaultRun();
    saved_config_ = config_;
    if (!environment_.settings_path.empty())
    {
        if (auto loaded = LoadUiSettings(environment_.settings_path))
        {
            settings_ = loaded.value();
        }
        else
        {
            settings_problem_ = std::move(loaded).error();
        }
    }
    if (!options_.dev_perf_log.empty())
    {
        perf_.Open(options_.dev_perf_log);
    }
}

MainWindow::~MainWindow()
{
    window_alive_ = false;
    // controller joins coordinator, keep it alive while a run is busy
    controller_.reset();
}

void MainWindow::RegisterClasses(HINSTANCE instance)
{
    WNDCLASSEXW window_class = {sizeof(window_class)};
    window_class.lpfnWndProc = &MainWindow::WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    window_class.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    window_class.lpszClassName = kMainWindowClass;
    RegisterClassExW(&window_class);
}

bool MainWindow::Create(int show_command)
{
    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    // set mode before creating windows for dark popup menus and scroll bars
    const win32ui::ThemePreset* preset = nullptr;
    if (settings_.theme == win32ui::ThemeMode::custom)
    {
        LoadThemePresets();
        preset = win32ui::FindThemePreset(theme_presets_, settings_.theme_preset);
    }
    win32ui::SetThemeMode(settings_.theme, preset ? &preset->colors : nullptr);
    CreateWindowExW(WS_EX_CONTROLPARENT, kMainWindowClass, L"ThreadSim", style, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_)
    {
        return false;
    }
    if (!options_.config_path.empty())
    {
        OpenDocument(options_.config_path);
    }
    BOOL cloak = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &cloak, sizeof(cloak));
    RestorePlacement(show_command);
    UpdateWindow(hwnd_);
    cloak = FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &cloak, sizeof(cloak));
    if (!environment_.topology_warning.empty())
    {
        SetStatusMessage(environment_.topology_warning);
    }
    if (settings_.show_all_threads)
    {
        ShowAllThreads(true);
    }
    SetFocus(table_.hwnd());
    if (settings_problem_)
    {
        const platform::Error problem = std::move(*settings_problem_);
        settings_problem_.reset();
        ShowError(hwnd_, L"Read settings", L"The settings file could not be read, the default settings are used.", problem);
    }
    ShowThemePresetsProblem();
    return true;
}

void MainWindow::RestorePlacement(int show_command)
{
    WINDOWPLACEMENT placement = {sizeof(placement)};
    GetWindowPlacement(hwnd_, &placement);
    if (settings_.has_window)
    {
        RECT rect = settings_.window;
        if (settings_.window_dpi != dpi_ && settings_.window_dpi != 0)
        {
            rect.right = rect.left + MulDiv(rect.right - rect.left, static_cast<int>(dpi_), static_cast<int>(settings_.window_dpi));
            rect.bottom = rect.top + MulDiv(rect.bottom - rect.top, static_cast<int>(dpi_), static_cast<int>(settings_.window_dpi));
        }
        win32ui::ClampToWorkArea(&rect);
        placement.rcNormalPosition = rect;
    }
    else
    {
        MONITORINFO info = {sizeof(info)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &info);
        const int width = std::min<int>(win32ui::Scale(1240, dpi_), info.rcWork.right - info.rcWork.left);
        const int height = std::min<int>(win32ui::Scale(780, dpi_), info.rcWork.bottom - info.rcWork.top);
        placement.rcNormalPosition.left = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
        placement.rcNormalPosition.top = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
        placement.rcNormalPosition.right = placement.rcNormalPosition.left + width;
        placement.rcNormalPosition.bottom = placement.rcNormalPosition.top + height;
    }
    placement.showCmd = settings_.has_window && settings_.maximized ? SW_SHOWMAXIMIZED : (show_command == SW_SHOWMINIMIZED || show_command == SW_MINIMIZE ? SW_SHOWNORMAL : show_command);
    if (placement.showCmd == SW_SHOWDEFAULT || placement.showCmd == SW_HIDE)
    {
        placement.showCmd = SW_SHOWNORMAL;
    }
    SetWindowPlacement(hwnd_, &placement);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
    {
        auto* self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self)
    {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    if (message == WM_NCDESTROY)
    {
        self->window_alive_ = false;
        self->post_target_.store(nullptr, std::memory_order_release);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return self->Handle(message, wparam, lparam);
}

bool MainWindow::NotifySnapshotReady() noexcept
{
    // engine thread must use the atomic copy, not hwnd_
    const HWND target = post_target_.load(std::memory_order_acquire);
    return window_alive_.load() && target && PostMessageW(target, kSnapshotMessage, 0, 0) != FALSE;
}

LRESULT MainWindow::Handle(UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateChildren();
        return 0;
    case WM_SIZE:
        Layout();
        // sample other threads again as soon as shown
        if ((wparam == SIZE_MINIMIZED) != other_sampling_paused_)
        {
            other_sampling_paused_ = wparam == SIZE_MINIMIZED;
            SampleOtherThreads();
        }
        return 0;
    case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            const UINT dpi = dpi_ ? dpi_ : win32ui::WindowDpi(hwnd_);
            info->ptMinTrackSize.x = win32ui::Scale(720, dpi);
            info->ptMinTrackSize.y = win32ui::Scale(520, dpi);
            return 0;
        }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        win32ui::PaintBuffered(hwnd_);
        if (!first_paint_logged_)
        {
            first_paint_logged_ = true;
            perf_.Event("first_paint_since_process_start", MsSinceProcessStart());
        }
        return 0;
    case WM_PRINTCLIENT:
        {
            RECT client = {};
            GetClientRect(hwnd_, &client);
            FillRect(reinterpret_cast<HDC>(wparam), &client, win32ui::ThemeBrush(win32ui::ThemeFill::background));
            return 0;
        }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        return reinterpret_cast<LRESULT>(win32ui::ThemeControlColor(message, reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam)));
    case WM_NCPAINT:
    case WM_NCACTIVATE:
        {
            const LRESULT result = DefWindowProcW(hwnd_, message, wparam, lparam);
            menu_.bar().PaintSeparator();
            return result;
        }
    case WM_MEASUREITEM:
        if (menu_.bar().Measure(reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)))
        {
            return TRUE;
        }
        break;
    case WM_DRAWITEM:
        if (menu_.bar().Draw(reinterpret_cast<const DRAWITEMSTRUCT*>(lparam)))
        {
            return TRUE;
        }
        break;
    case WM_INITMENUPOPUP:
        {
            // Windows tracks window menu state itself
            if (HIWORD(lparam))
            {
                break;
            }
            struct PopupState
            {
                const MainWindow* window;
                std::vector<uint32_t> selected;
            };
            PopupState state = {this, table_.SelectedThreads()};
            win32ui::MenuHooks hooks;
            hooks.context = &state;
            hooks.enabled = [](void* context, int id) {
                const auto* popup = static_cast<const PopupState*>(context);
                return popup->window->CommandEnabled(id, popup->selected);
            };
            hooks.checked = [](void* context, int id) {
                const auto* popup = static_cast<const PopupState*>(context);
                return popup->window->CommandChecked(id, popup->selected);
            };
            hooks.text = [](void* context, int id, std::wstring* text) {
                *text = static_cast<const PopupState*>(context)->window->CommandText(id);
                return !text->empty();
            };
            win32ui::UpdateMenuPopup(reinterpret_cast<HMENU>(wparam), hooks);
            return 0;
        }
    case WM_COMMAND:
        {
            const int id = LOWORD(wparam);
            if (id == IDOK || id == IDCANCEL)
            {
                return 0;
            }
            const int64_t start = platform::QpcClock::Now();
            // table accelerators apply only while it has focus
            const bool from_accelerator = HIWORD(wparam) == 1 && lparam == 0;
            if (from_accelerator && (id == cmd::kThreadRemove || id == cmd::kThreadRename || id == cmd::kEditCopy || id == cmd::kEditSelectAll) && GetFocus() != table_.hwnd())
            {
                return 0;
            }
            OnCommand(id);
            perf_.Event("command", ElapsedMs(start), "id", id);
            return 0;
        }
    case WM_NOTIFY:
        {
            auto* header = reinterpret_cast<NMHDR*>(lparam);
            LRESULT result = 0;
            if (table_.OnNotify(header, &result))
            {
                return result;
            }
            if (header->hwndFrom == table_.hwnd() && (header->code == LVN_ITEMCHANGED || header->code == LVN_ODSTATECHANGED))
            {
                // virtual list reports range selection as one LVN_ODSTATECHANGED
                UINT changed = 0;
                if (header->code == LVN_ITEMCHANGED)
                {
                    const auto* change = reinterpret_cast<NMLISTVIEW*>(lparam);
                    changed = (change->uChanged & LVIF_STATE) ? change->uOldState ^ change->uNewState : 0;
                }
                else
                {
                    const auto* change = reinterpret_cast<NMLVODSTATECHANGE*>(lparam);
                    changed = change->uOldState ^ change->uNewState;
                }
                if ((changed & (LVIS_SELECTED | LVIS_FOCUSED)) && !selection_update_pending_ && PostMessageW(hwnd_, kSelectionMessage, 0, 0))
                {
                    selection_update_pending_ = true;
                }
                return 0;
            }
            if (header->hwndFrom == table_.hwnd() && header->code == NM_DBLCLK)
            {
                // PreTranslate takes check box clicks before the list, other threads are read only
                if (reinterpret_cast<NMITEMACTIVATE*>(lparam)->iItem >= 0 && table_.FocusedThread() >= 0)
                {
                    OnCommand(cmd::kThreadDetails);
                }
                return 0;
            }
            if (header->hwndFrom == table_.hwnd() && header->code == LVN_BEGINLABELEDITW)
            {
                const int thread = table_.FocusedThread();
                const bool allowed = thread >= 0 && ThreadSettingEditable();
                if (allowed)
                {
                    if (HWND edit = ListView_GetEditControl(table_.hwnd()))
                    {
                        SetWindowTextW(edit, config_.threads[static_cast<size_t>(thread)].name.c_str());
                        SendMessageW(edit, EM_SETSEL, 0, -1);
                        SendMessageW(edit, EM_LIMITTEXT, engine::kMaxNameLength, 0);
                        win32ui::AddSelectAllShortcut(edit);
                    }
                }
                return allowed ? FALSE : TRUE;
            }
            if (header->hwndFrom == table_.hwnd() && header->code == LVN_ENDLABELEDITW)
            {
                const auto* edit = reinterpret_cast<NMLVDISPINFOW*>(lparam);
                if (edit->item.pszText)
                {
                    RenameThread(table_.FocusedThread(), edit->item.pszText);
                }
                return FALSE;
            }
            if (LRESULT toolbar_result = 0; win32ui::ToolbarNotify(header, &toolbar_result))
            {
                return toolbar_result;
            }
            if (header->hwndFrom == status_ && header->code == NM_DBLCLK)
            {
                const auto* click = reinterpret_cast<NMMOUSE*>(lparam);
                if (click->dwItemSpec == 3)
                {
                    FocusFirstIssue();
                }
                return 0;
            }
            break;
        }
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wparam) == table_.hwnd())
        {
            POINT point = {};
            const int row = win32ui::ContextMenuRow(table_.hwnd(), lparam, &point);
            if (row >= 0 && ListView_GetItemState(table_.hwnd(), row, LVIS_SELECTED) == 0)
            {
                ListView_SetItemState(table_.hwnd(), -1, 0, LVIS_SELECTED);
                ListView_SetItemState(table_.hwnd(), row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            ShowContextMenu(point);
            return 0;
        }
        break;
    case kSnapshotMessage:
        OnSnapshotReady();
        return 0;
    case kSelectionMessage:
        // skip if OnSelectionChanged ran explicitly after posting
        if (selection_update_pending_)
        {
            OnSelectionChanged();
        }
        return 0;
    case kRunFailedMessage:
        if (!close_pending_ && snapshot_ && snapshot_->generation == static_cast<uint64_t>(lparam) && snapshot_->failure)
        {
            const platform::Error failure = *snapshot_->failure;
            ShowError(hwnd_, L"Run failed", L"The run failed.", failure);
        }
        return 0;
    case WM_TIMER:
        if (wparam == kReapTimer)
        {
            if (controller_->TryReap())
            {
                if (close_pending_ && win32ui::ModalDepth() > 0)
                {
                    // keep timer until modal depth reaches zero, then destroy the window
                    win32ui::CancelOpenDialogs();
                    return 0;
                }
                KillTimer(hwnd_, kReapTimer);
                KillTimer(hwnd_, kPollTimer);
                if (close_pending_)
                {
                    SaveSettings();
                    DestroyWindow(hwnd_);
                    return 0;
                }
                SampleOtherThreads();
                UpdateCommandStates();
                UpdateStatus();
            }
            return 0;
        }
        if (wparam == kApplyTimer)
        {
            KillTimer(hwnd_, kApplyTimer);
            apply_timer_armed_ = false;
            OnSnapshot();
            return 0;
        }
        if (wparam == kPollTimer)
        {
            OnSnapshotReady();
            if (!RunBusy())
            {
                KillTimer(hwnd_, kPollTimer);
            }
            return 0;
        }
        if (wparam == kOtherThreadsTimer)
        {
            SampleOtherThreads();
            return 0;
        }
        if (wparam == kCloseRetryTimer)
        {
            if (win32ui::ModalDepth() == 0)
            {
                KillTimer(hwnd_, kCloseRetryTimer);
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE)
        {
            HWND focus = GetFocus();
            if (focus && IsChild(hwnd_, focus))
            {
                SetPropW(hwnd_, kLastFocusProp, focus);
            }
        }
        else
        {
            HWND last = static_cast<HWND>(GetPropW(hwnd_, kLastFocusProp));
            if (last && IsWindow(last) && IsChild(hwnd_, last) && IsWindowVisible(last) && IsWindowEnabled(last))
            {
                SetFocus(last);
                return 0;
            }
        }
        break;
    case WM_SETTINGCHANGE:
        if (wparam == SPI_SETNONCLIENTMETRICS)
        {
            ReplaceFonts(dpi_);
            ApplyTheme();
            Layout();
        }
        else if (win32ui::ThemeMessage(message, lparam))
        {
            ApplyTheme();
        }
        break;
    case WM_SYSCOLORCHANGE:
    case WM_THEMECHANGED:
        if (win32ui::ThemeMessage(message, lparam))
        {
            ApplyTheme();
        }
        break;
    case WM_DPICHANGED:
        if (!win32ui::DpiOverride())
        {
            OnDpiChanged(HIWORD(wparam), reinterpret_cast<const RECT*>(lparam));
        }
        return 0;
    case WM_CLOSE:
        RequestClose();
        return 0;
    case WM_DESTROY:
        RemovePropW(hwnd_, kLastFocusProp);
        KillTimer(hwnd_, kReapTimer);
        KillTimer(hwnd_, kPollTimer);
        KillTimer(hwnd_, kApplyTimer);
        KillTimer(hwnd_, kCloseRetryTimer);
        KillTimer(hwnd_, kOtherThreadsTimer);
        window_alive_ = false;
        post_target_.store(nullptr, std::memory_order_release);
        perf_.Raw("{\"event\":\"exit\",\"snapshots_applied\":" + std::to_string(snapshots_applied_) + ",\"snapshots_stale\":" + std::to_string(snapshots_stale_) + ",\"published\":" + std::to_string(controller_->mailbox().published()) + ",\"notified\":" + std::to_string(controller_->mailbox().notified()) + ",\"max_apply_ms\":" + std::to_string(snapshot_apply_max_ms_) + "}");
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

bool MainWindow::RunBusy() const
{
    return controller_->busy();
}

bool MainWindow::CommandEnabled(int id) const
{
    return CommandEnabled(id, table_.SelectedThreads());
}

bool MainWindow::CommandEnabled(int id, const std::vector<uint32_t>& selected) const
{
    const bool busy = RunBusy();
    const bool active = snapshot_ && snapshot_->generation == ui_generation_ && !snapshot_->final && (snapshot_->state == engine::RunState::kActive || snapshot_->state == engine::RunState::kStarting);
    auto any_state = [&](std::initializer_list<engine::ThreadRunState> states) {
        if (!snapshot_)
        {
            return false;
        }
        for (uint32_t thread : selected)
        {
            if (thread < snapshot_->threads.size() && std::find(states.begin(), states.end(), snapshot_->threads[thread].state) != states.end())
            {
                return true;
            }
        }
        return false;
    };
    switch (id)
    {
    case cmd::kFileNew:
    case cmd::kFileOpen:
        return !busy;
    case cmd::kFileSave:
    case cmd::kFileSaveAs:
        return true;
    case cmd::kFileExport:
        return snapshot_ != nullptr && snapshot_->final;
    case cmd::kEditCopy:
        return table_.SelectedCount() > 0;
    case cmd::kEditCopyIds:
    case cmd::kEditSelectAll:
        return !config_.threads.empty();
    case cmd::kThreadDetails:
        return !selected.empty();
    case cmd::kThreadAdd:
        return !busy && config_.threads.size() < engine::kMaxThreads;
    case cmd::kThreadDuplicate:
        return !busy && selected.size() == 1 && config_.threads.size() < engine::kMaxThreads;
    case cmd::kThreadRemove:
        return !busy && !selected.empty() && config_.threads.size() > selected.size();
    case cmd::kThreadMoveUp:
        return !busy && selected.size() == 1 && selected[0] > 0;
    case cmd::kThreadMoveDown:
        return !busy && selected.size() == 1 && selected[0] + 1 < config_.threads.size();
    case cmd::kThreadRename:
        return ThreadSettingEditable(selected) && selected.size() == 1;
    case cmd::kThreadActive:
    case cmd::kThreadActivityCustom:
    case cmd::kThreadSharedMutex:
        return ThreadSettingEditable(selected);
    case cmd::kThreadBoost:
    case cmd::kThreadAffinity:
    case cmd::kThreadIdeal:
        return ThreadPolicyEditable(selected);
    case cmd::kThreadCpuSets:
        return ThreadPolicyEditable(selected) && environment_.topology.cpu_sets_known;
    case cmd::kRunStart:
        return !busy && !validation_.HasErrors() && !config_.threads.empty();
    case cmd::kRunDurationUnlimited:
    case cmd::kRunDurationCustom:
    case cmd::kViewUpdateSpeedCustom:
        return !busy;
    // process changes during a run go into the run's live change log
    case cmd::kProcessAffinity:
    case cmd::kProcessCpuSets:
        return !busy || RunLive();
    case cmd::kRunSuspend:
        return active && any_state({engine::ThreadRunState::kPending, engine::ThreadRunState::kWorking, engine::ThreadRunState::kResting});
    case cmd::kRunResume:
        return active && any_state({engine::ThreadRunState::kSuspended, engine::ThreadRunState::kSuspending});
    case cmd::kRunStopSelected:
        return active && any_state({engine::ThreadRunState::kWorking, engine::ThreadRunState::kResting, engine::ThreadRunState::kSuspended, engine::ThreadRunState::kSuspending, engine::ThreadRunState::kPending});
    case cmd::kRunStopAll:
        return busy && !(snapshot_ && snapshot_->state == engine::RunState::kStopping);
    default:
        if (id >= cmd::kThreadPriorityFirst && id <= cmd::kThreadPriorityLast)
        {
            return ThreadPolicyEditable(selected);
        }
        if (id >= cmd::kThreadActivityFirst && id <= cmd::kThreadActivityLast)
        {
            return ThreadSettingEditable(selected);
        }
        if (id >= cmd::kProcessPriorityFirst && id <= cmd::kProcessPriorityLast)
        {
            return !busy || RunLive();
        }
        if ((id >= cmd::kRunDurationFirst && id <= cmd::kRunDurationLast) || (id >= cmd::kViewUpdateSpeedFirst && id <= cmd::kViewUpdateSpeedLast))
        {
            return !busy;
        }
        return true;
    }
}

int MainWindow::RepresentativeThread(const std::vector<uint32_t>& selected) const
{
    const int focused = table_.FocusedThread();
    if (focused >= 0 && std::find(selected.begin(), selected.end(), static_cast<uint32_t>(focused)) != selected.end())
    {
        return focused;
    }
    return selected.empty() || selected.front() >= config_.threads.size() ? -1 : static_cast<int>(selected.front());
}

bool MainWindow::CommandChecked(int id) const
{
    return CommandChecked(id, table_.SelectedThreads());
}

bool MainWindow::CommandChecked(int id, const std::vector<uint32_t>& selected) const
{
    const int representative = RepresentativeThread(selected);
    const engine::ThreadConfig* thread = representative >= 0 ? &config_.threads[static_cast<size_t>(representative)] : nullptr;
    const engine::ThreadAffinityConfig affinity_config = thread ? thread->affinity : engine::ThreadAffinityConfig{};
    if (id >= cmd::kThreadPriorityFirst && id <= cmd::kThreadPriorityLast)
    {
        return thread && thread->scheduling.relative_priority == engine::kAllRelativeThreadPriorities[id - cmd::kThreadPriorityFirst];
    }
    if (id >= cmd::kThreadActivityFirst && id <= cmd::kThreadActivityLast)
    {
        return thread && engine::ActivityLevelOf(thread->activity) == static_cast<engine::ActivityLevel>(id - cmd::kThreadActivityFirst);
    }
    if (id >= cmd::kRunDurationFirst && id <= cmd::kRunDurationLast)
    {
        return config_.duration_s == kDurationPresets[id - cmd::kRunDurationFirst];
    }
    if (id >= cmd::kProcessPriorityFirst && id <= cmd::kProcessPriorityLast)
    {
        return GetPriorityClass(GetCurrentProcess()) == kPriorityClasses[id - cmd::kProcessPriorityFirst];
    }
    if (id >= cmd::kViewUpdateSpeedFirst && id <= cmd::kViewUpdateSpeedLast)
    {
        return config_.sample_interval_ms == kUpdateSpeedPresets[id - cmd::kViewUpdateSpeedFirst];
    }
    switch (id)
    {
    case cmd::kRunDurationUnlimited:
        return !config_.duration_s;
    case cmd::kRunDurationCustom:
        return config_.duration_s && std::ranges::find(kDurationPresets, *config_.duration_s) == std::end(kDurationPresets);
    case cmd::kViewUpdateSpeedCustom:
        return std::ranges::find(kUpdateSpeedPresets, config_.sample_interval_ms) == std::end(kUpdateSpeedPresets);
    case cmd::kThreadActive:
        return thread && thread->active;
    case cmd::kThreadActivityCustom:
        return thread && engine::ActivityLevelOf(thread->activity) == engine::ActivityLevel::kCustom;
    case cmd::kThreadBoost:
        return thread && thread->scheduling.priority_boost_enabled;
    case cmd::kThreadSharedMutex:
        return thread && thread->uses_shared_mutex;
    case cmd::kThreadAffinity:
        return thread && affinity_config.mode == engine::AffinityMode::kGroupAffinity;
    case cmd::kThreadCpuSets:
        return thread && affinity_config.mode == engine::AffinityMode::kCpuSets;
    case cmd::kThreadIdeal:
        return thread && affinity_config.ideal_processor_enabled;
    case cmd::kThemeSystem:
        return win32ui::CurrentThemeMode() == win32ui::ThemeMode::system;
    case cmd::kThemeLight:
        return win32ui::CurrentThemeMode() == win32ui::ThemeMode::light;
    case cmd::kThemeDark:
        return win32ui::CurrentThemeMode() == win32ui::ThemeMode::dark;
    case cmd::kThemeCustom:
        return win32ui::CurrentThemeMode() == win32ui::ThemeMode::custom;
    case cmd::kViewShowAllThreads:
        return settings_.show_all_threads;
    case cmd::kViewGridLines:
        return win32ui::AllListGridLines();
    case cmd::kViewToolbar:
        return settings_.show_toolbar;
    case cmd::kViewStatusBar:
        return settings_.show_status_bar;
    default:
        return false;
    }
}

std::wstring MainWindow::CommandText(int id) const
{
    switch (id)
    {
    case cmd::kRunDurationCustom:
        return CustomValueMenuText(CommandChecked(id, {}) ? DescribeDuration(config_.duration_s) : std::wstring());
    case cmd::kViewUpdateSpeedCustom:
        return CustomValueMenuText(CommandChecked(id, {}) ? DescribeInterval(config_.sample_interval_ms) : std::wstring());
    default:
        return {};
    }
}

void MainWindow::UpdateCommandStates()
{
    const std::vector<uint32_t> selected = table_.SelectedThreads();
    for (const auto& button : kToolButtons)
    {
        if (button.command)
        {
            win32ui::EnableToolbarButton(toolbar_, button.command, CommandEnabled(button.command, selected));
        }
    }
}

void MainWindow::SetStatusMessage(std::wstring text)
{
    win32ui::ShowStatusMessage(status_, text);
    UpdateStatus();
}

void MainWindow::UpdateStatus()
{
    std::wstring state = L"Idle";
    std::wstring process_cpu;
    if (snapshot_ && snapshot_->generation == ui_generation_)
    {
        state = engine::RunStateLabel(snapshot_->state);
        const std::wstring elapsed = platform::FormatFixed(static_cast<double>(snapshot_->elapsed_us) / 1e6, 1);
        if (snapshot_->state == engine::RunState::kActive || snapshot_->state == engine::RunState::kStopping)
        {
            state += L" " + elapsed + (snapshot_->requested_duration_s ? L" of " + std::to_wstring(*snapshot_->requested_duration_s) : std::wstring()) + L" s";
            // share of all active logical processors, like Task Manager
            const uint32_t processors = snapshot_->topology ? snapshot_->topology->ActiveProcessorCount() : 0;
            if (snapshot_->process && snapshot_->process->cpu_percent_one_logical_processor && processors > 0)
            {
                process_cpu = L", CPU " + platform::FormatFixed(*snapshot_->process->cpu_percent_one_logical_processor / processors, 1) + L" %";
            }
        }
        // setup failure never reaches the start, so no duration is available
        else if (engine::IsTerminal(snapshot_->state) && snapshot_->origin_qpc != 0 && snapshot_->elapsed_us > 0)
        {
            state += L" after " + elapsed + L" s";
        }
    }
    if (close_pending_)
    {
        state = L"Stopping before exit";
    }
    const size_t selected = table_.SelectedCount();
    const auto active = std::count_if(config_.threads.begin(), config_.threads.end(), [](const engine::ThreadConfig& thread) { return thread.active; });
    const size_t others = table_.OtherCount();
    const std::wstring threads = std::to_wstring(config_.threads.size()) + (config_.threads.size() == 1 ? L" thread, " : L" threads, ") + std::to_wstring(active) + L" active" +
                                 (others ? L", " + std::to_wstring(others) + L" other" : L"") + (selected > 1 ? L", " + std::to_wstring(selected) + L" selected" : L"");
    const std::wstring process = L"PID " + std::to_wstring(GetCurrentProcessId()) + L", " + PriorityClassName(GetPriorityClass(GetCurrentProcess())) + process_cpu;
    // show validation issues first, otherwise last message, name one error and count the rest
    std::wstring issues = win32ui::StatusMessage(status_);
    if (validation_.ErrorCount() == 1)
    {
        const auto error = std::find_if(validation_.issues.begin(), validation_.issues.end(), [](const engine::ValidationIssue& issue) { return issue.severity == engine::IssueSeverity::kError; });
        const bool named = error->scope == engine::IssueScope::kThread && error->thread >= 0 && static_cast<size_t>(error->thread) < config_.threads.size();
        issues = (named ? config_.threads[static_cast<size_t>(error->thread)].name + L": " : std::wstring()) + error->message;
    }
    else if (validation_.HasErrors())
    {
        issues = std::to_wstring(validation_.ErrorCount()) + L" errors block Start";
    }
    else if (validation_.WarningCount() > 0 && issues.empty())
    {
        issues = std::to_wstring(validation_.WarningCount()) + (validation_.WarningCount() == 1 ? L" warning" : L" warnings");
    }
    if (win32ui::DpiOverride())
    {
        issues = L"Synthetic DPI " + std::to_wstring(win32ui::DpiOverride()) + (issues.empty() ? L"" : L", " + issues);
    }
    const std::wstring parts[] = {state, threads, process, issues};
    for (int index = 0; index < static_cast<int>(std::size(parts)); ++index)
    {
        win32ui::SetStatusText(status_, index, parts[index]);
    }
}

std::wstring MainWindow::DocumentName() const
{
    if (!document_path_.empty())
    {
        return FileNameOf(document_path_);
    }
    return config_.name.empty() ? L"Untitled" : config_.name;
}

void MainWindow::UpdateTitle()
{
    static const std::wstring app = [] {
        std::wstring text = L"ThreadSim V" THREAD_SIM_VERSION_TEXT;
        if (win32ui::IsProcessTrustedInstaller())
        {
            text += L" - [TrustedInstaller]";
        }
        else if (win32ui::IsProcessSystem())
        {
            text += L" - [SYSTEM]";
        }
        else if (win32ui::IsProcessElevated())
        {
            text += L" - [Administrator]";
        }
        return text;
    }();
    const std::wstring title = DocumentName() + (config_ == saved_config_ ? L"" : L"*") + L" - " + app;
    if (GetText(hwnd_) != title)
    {
        SetWindowTextW(hwnd_, title.c_str());
    }
}

void MainWindow::Revalidate()
{
    const auto process = platform::QueryProcessSettings();
    validation_ = engine::Validate(config_, environment_.topology, process ? process.value() : platform::ProcessSettings{});
    if (!process)
    {
        validation_.issues.push_back({engine::IssueSeverity::kError, engine::IssueScope::kRun, -1, engine::Field::kNone, process.error().Describe()});
    }
}

void MainWindow::OnConfigChanged(bool structural)
{
    Revalidate();
    if (snapshot_ && !RunBusy())
    {
        results_stale_ = true;
    }
    table_.SetData(&config_, results_stale_ ? nullptr : snapshot_);
    if (structural)
    {
        table_.RefreshAll();
    }
    else
    {
        table_.RefreshVisible();
    }
    UpdateDetails();
    UpdateTitle();
    UpdateCommandStates();
    UpdateStatus();
    FollowSampleInterval();
}

void MainWindow::OnSelectionChanged()
{
    selection_update_pending_ = false;
    UpdateCommandStates();
    UpdateStatus();
}

void MainWindow::ResetThreadKeys()
{
    thread_keys_.clear();
    for (size_t index = 0; index < config_.threads.size(); ++index)
    {
        thread_keys_.push_back(next_thread_key_++);
    }
}

int MainWindow::RunIndexOfRow(size_t row) const
{
    if (row >= thread_keys_.size())
    {
        return -1;
    }
    const auto found = std::find(run_keys_.begin(), run_keys_.end(), thread_keys_[row]);
    return found == run_keys_.end() ? -1 : static_cast<int>(found - run_keys_.begin());
}

void MainWindow::ShowDetails()
{
    const int row = RepresentativeThread(table_.SelectedThreads());
    if (row < 0 || static_cast<size_t>(row) >= thread_keys_.size())
    {
        return;
    }
    const uint64_t key = thread_keys_[static_cast<size_t>(row)];
    std::erase_if(details_, [](const auto& window) { return !window->window(); });
    for (const auto& window : details_)
    {
        if (window->thread_key() == key)
        {
            ShowWindow(window->window(), SW_SHOWNORMAL);
            SetForegroundWindow(window->window());
            return;
        }
    }
    auto window = std::make_unique<DetailsWindow>(key);
    if (!window->Open(hwnd_))
    {
        return;
    }
    details_.push_back(std::move(window));
    UpdateDetails();
}

void MainWindow::UpdateDetails()
{
    std::erase_if(details_, [](const auto& window) { return !window->window(); });
    for (auto& window : details_)
    {
        const auto row = std::find(thread_keys_.begin(), thread_keys_.end(), window->thread_key());
        if (row == thread_keys_.end())
        {
            // thread was removed or replaced by a new config
            DestroyWindow(window->window());
            continue;
        }
        const size_t index = static_cast<size_t>(row - thread_keys_.begin());
        const int run_index = RunIndexOfRow(index);
        const bool in_run = snapshot_ && run_index >= 0 && static_cast<size_t>(run_index) < snapshot_->threads.size() && static_cast<size_t>(run_index) < run_config_.threads.size();
        // show results with requested values from the config used by the run
        const engine::ThreadConfig& requested = in_run && results_stale_ ? run_config_.threads[static_cast<size_t>(run_index)] : config_.threads[index];
        window->Show(requested, in_run ? snapshot_.get() : nullptr, in_run ? &snapshot_->threads[static_cast<size_t>(run_index)] : nullptr);
    }
    std::erase_if(details_, [](const auto& window) { return !window->window(); });
}

std::wstring MainWindow::RunTipText() const
{
    if (!snapshot_ || snapshot_->run_id.empty())
    {
        return {};
    }
    const engine::RunSnapshot& run = *snapshot_;
    std::wstring text = L"Run " + run.run_id;
    if (!run.started_utc.empty())
    {
        text += L"\nStarted " + run.started_utc + (run.ended_utc.empty() ? L"" : L", ended " + run.ended_utc) + L" (UTC)";
    }
    text += L"\nSample interval " + std::to_wstring(run.sample_interval_ms) + L" ms";
    if (run.last_sample_interval_us)
    {
        text += L", last " + platform::FormatFixed(static_cast<double>(*run.last_sample_interval_us) / 1000.0, 1) + L" ms";
    }
    if (run.failure)
    {
        text += L"\nFailed: " + run.failure->Describe();
    }
    if (results_stale_)
    {
        text += L"\nThe config changed after this run, the table shows no results, Details keep the run's values";
    }
    return text;
}

bool MainWindow::PreTranslate(MSG* message)
{
    if (!hwnd_)
    {
        return false;
    }
    HWND root = GetAncestor(message->hwnd, GA_ROOT);
    if (root != hwnd_)
    {
        // modeless detail windows need keyboard navigation from this loop
        for (const auto& window : details_)
        {
            if (window->window() == root)
            {
                return IsDialogMessageW(root, message) != FALSE;
            }
        }
        return false;
    }

    if ((message->message == WM_LBUTTONDOWN || message->message == WM_LBUTTONDBLCLK) && message->hwnd == table_.hwnd())
    {
        const int thread = table_.CheckBoxAt({GET_X_LPARAM(message->lParam), GET_Y_LPARAM(message->lParam)});
        if (thread >= 0 && ThreadSettingEditable({static_cast<uint32_t>(thread)}))
        {
            SetFocus(table_.hwnd());
            ToggleThreadActive({static_cast<uint32_t>(thread)});
            return true;
        }
    }

    if (message->message == WM_KEYDOWN && message->wParam == VK_SPACE && message->hwnd == table_.hwnd() && !(GetKeyState(VK_CONTROL) & 0x8000) &&
        !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && !win32ui::ListTypeSearchActive(table_.hwnd()))
    {
        OnCommand(cmd::kThreadActive);
        return true;
    }
    if (win32ui::TranslateShortcuts(hwnd_, menu_.shortcuts(), message, ShouldBypassAccelerator))
    {
        return true;
    }

    if (message->message == WM_KEYDOWN && message->wParam == VK_F6 && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000))
    {
        SetFocus(GetFocus() == toolbar_ || !IsWindowVisible(toolbar_) ? table_.hwnd() : toolbar_);
        return true;
    }
    if (message->message == WM_KEYDOWN && message->wParam == VK_TAB)
    {
        if (IsDialogMessageW(hwnd_, message))
        {
            return true;
        }
    }
    return false;
}

void MainWindow::OnCommand(int id)
{
    if (!CommandEnabled(id))
    {
        return;
    }
    switch (id)
    {
    case cmd::kFileNew:
        NewDocument();
        break;
    case cmd::kFileOpen:
        if (ConfirmDiscard())
        {
            if (const auto path = FileDialog(hwnd_, false, L"Open config", L"ThreadSim config (*.jsonc, *.json)", L"*.jsonc;*.json", L"jsonc", L"", settings_.last_directory))
            {
                OpenDocument(*path);
            }
        }
        break;
    case cmd::kFileSave:
        SaveDocument(false);
        break;
    case cmd::kFileSaveAs:
        SaveDocument(true);
        break;
    case cmd::kFileExport:
        ExportResults();
        break;
    case cmd::kFileExit:
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    case cmd::kEditCopy:
        win32ui::CopyTextToClipboard(hwnd_, win32ui::SelectedRowsText(table_.hwnd()));
        SetStatusMessage(L"Copied " + std::to_wstring(table_.SelectedCount()) + (table_.SelectedCount() == 1 ? L" row" : L" rows"));
        break;
    case cmd::kEditCopyIds:
        win32ui::CopyTextToClipboard(hwnd_, table_.ThreadIdsText());
        SetStatusMessage(L"Copied TIDs");
        break;
    case cmd::kEditSelectAll:
        table_.SelectAll();
        break;
    case cmd::kThreadAdd:
        AddThread(false);
        break;
    case cmd::kThreadDuplicate:
        AddThread(true);
        break;
    case cmd::kThreadRemove:
        RemoveThreads();
        break;
    case cmd::kThreadMoveUp:
        MoveThread(-1);
        break;
    case cmd::kThreadMoveDown:
        MoveThread(1);
        break;
    case cmd::kThreadRename:
        table_.BeginRename(table_.FocusedThread());
        break;
    case cmd::kThreadActive:
        ToggleThreadActive(table_.SelectedThreads());
        break;
    case cmd::kThreadActivityCustom:
        EditThreadActivity();
        break;
    case cmd::kThreadDetails:
        ShowDetails();
        break;
    case cmd::kThreadBoost:
        ToggleThreadPriorityBoost();
        break;
    case cmd::kThreadSharedMutex:
        ToggleThreadSharedMutex();
        break;
    case cmd::kThreadAffinity:
    case cmd::kThreadCpuSets:
    case cmd::kThreadIdeal:
        EditThreadAffinity(id);
        break;
    case cmd::kRunStart:
        StartRun();
        break;
    case cmd::kRunSuspend:
        controller_->RequestSuspend(table_.SelectedThreads());
        break;
    case cmd::kRunResume:
        controller_->RequestResume(table_.SelectedThreads());
        break;
    case cmd::kRunStopSelected:
        controller_->RequestStop(table_.SelectedThreads());
        break;
    case cmd::kRunStopAll:
        controller_->RequestStopAll();
        SetStatusMessage(L"Stopping all threads");
        break;
    case cmd::kRunDurationCustom:
    case cmd::kViewUpdateSpeedCustom:
        if (ShowRunValueDialog(hwnd_, &config_, id == cmd::kRunDurationCustom ? engine::Field::kRunDuration : engine::Field::kSampleInterval) && hwnd_)
        {
            OnConfigChanged(false);
        }
        break;
    case cmd::kThemeSystem:
    case cmd::kThemeLight:
    case cmd::kThemeDark:
        settings_.theme = id == cmd::kThemeSystem ? win32ui::ThemeMode::system : id == cmd::kThemeLight ? win32ui::ThemeMode::light
                                                                                                        : win32ui::ThemeMode::dark;
        win32ui::SetThemeMode(settings_.theme);
        ApplyTheme();
        SaveSettings();
        break;
    case cmd::kThemeCustom:
        ApplyThemePreset(settings_.theme_preset);
        SaveSettings();
        break;
    case cmd::kThemePresets:
        ShowThemePresets();
        break;
    case cmd::kViewTopology:
        {
            PickerOptions options;
            options.mode = PickerMode::kBrowse;
            options.title = L"Processor topology";
            ShowProcessorPicker(hwnd_, options, environment_.topology, {});
            break;
        }
    case cmd::kViewResetLayout:
        table_.ResetColumns(dpi_);
        Layout();
        break;
    case cmd::kViewRefresh:
        RefreshEnvironment();
        // during runs, request a coordinator sample without waiting for the interval
        if (RunBusy())
        {
            controller_->RequestSample();
        }
        else
        {
            SampleOtherThreads();
        }
        table_.RefreshAll();
        break;
    case cmd::kViewShowAllThreads:
        ShowAllThreads(!settings_.show_all_threads);
        SaveSettings();
        break;
    case cmd::kViewGridLines:
        win32ui::SetAllListGridLines(!win32ui::AllListGridLines());
        settings_.show_grid_lines = win32ui::AllListGridLines();
        SaveSettings();
        break;
    case cmd::kViewToolbar:
        settings_.show_toolbar = !settings_.show_toolbar;
        ShowBars();
        SaveSettings();
        break;
    case cmd::kViewStatusBar:
        settings_.show_status_bar = !settings_.show_status_bar;
        ShowBars();
        SaveSettings();
        break;
    case cmd::kHelpAbout:
        ShowAbout();
        break;
    case cmd::kRunDurationUnlimited:
        config_.duration_s.reset();
        OnConfigChanged(false);
        break;
    case cmd::kProcessAffinity:
    case cmd::kProcessCpuSets:
        EditProcessPlacement(id);
        break;
    default:
        if (id >= cmd::kThreadPriorityFirst && id <= cmd::kThreadPriorityLast)
        {
            SetThreadRelativePriority(engine::kAllRelativeThreadPriorities[id - cmd::kThreadPriorityFirst]);
        }
        else if (id >= cmd::kThreadActivityFirst && id <= cmd::kThreadActivityLast)
        {
            SetThreadActivityLevel(static_cast<engine::ActivityLevel>(id - cmd::kThreadActivityFirst));
        }
        else if (id >= cmd::kRunDurationFirst && id <= cmd::kRunDurationLast)
        {
            config_.duration_s = kDurationPresets[id - cmd::kRunDurationFirst];
            OnConfigChanged(false);
        }
        else if (id >= cmd::kViewUpdateSpeedFirst && id <= cmd::kViewUpdateSpeedLast)
        {
            config_.sample_interval_ms = kUpdateSpeedPresets[id - cmd::kViewUpdateSpeedFirst];
            OnConfigChanged(false);
        }
        else if (id >= cmd::kProcessPriorityFirst && id <= cmd::kProcessPriorityLast)
        {
            SetProcessPriorityClass(kPriorityClasses[id - cmd::kProcessPriorityFirst]);
        }
#if defined(THREAD_SIM_DEV)
        if (id >= cmd::kDevSimulateDpiFirst && id <= cmd::kDevSimulateDpiLast)
        {
            const UINT dpis[] = {96, 120, 144, 192};
            const UINT dpi = dpis[id - cmd::kDevSimulateDpiFirst];
            RECT window = {};
            GetWindowRect(hwnd_, &window);
            RECT suggested = {window.left, window.top, window.left + MulDiv(window.right - window.left, static_cast<int>(dpi), static_cast<int>(dpi_)), window.top + MulDiv(window.bottom - window.top, static_cast<int>(dpi), static_cast<int>(dpi_))};
            win32ui::ClampToWorkArea(&suggested);
            win32ui::SetDpiOverride(dpi);
            OnDpiChanged(dpi, &suggested);
            UpdateStatus();
        }
#endif
        break;
    }
    UpdateCommandStates();
    UpdateStatus();
}

void MainWindow::StartRun()
{
    const int64_t start = platform::QpcClock::Now();
    RefreshEnvironment();
    platform::Environment environment = platform::QueryEnvironment();
    engine::RunConfig config = config_;
    const platform::Status status = controller_->Start(config, environment_.topology, std::move(environment));
    if (!status)
    {
        ShowError(hwnd_, L"Start run", L"The run could not be started.", status.error());
        return;
    }
    // replace result config only after the new run starts
    run_config_ = std::move(config);
    run_keys_ = thread_keys_;
    ui_generation_ = controller_->current_generation();
    last_sequence_ = 0;
    results_stale_ = false;
    snapshot_.reset();
    // mailbox keeps the newest snapshot if notification is lost
    SetTimer(hwnd_, kPollTimer, 250, nullptr);
    perf_.Event("start_request", ElapsedMs(start), "generation", static_cast<long long>(ui_generation_));
    UpdateCommandStates();
    UpdateStatus();
}

void MainWindow::OnSnapshotReady()
{
    // leaving the snapshot untaken suppresses notifications, timer takes the latest
    const double since = ElapsedMs(last_apply_qpc_);
    if (since >= kMinSnapshotApplyMs)
    {
        OnSnapshot();
    }
    else if (!apply_timer_armed_)
    {
        apply_timer_armed_ = true;
        SetTimer(hwnd_, kApplyTimer, static_cast<UINT>(kMinSnapshotApplyMs - since) + 1, nullptr);
    }
}

void MainWindow::OnSnapshot()
{
    const int64_t start = platform::QpcClock::Now();
    last_apply_qpc_ = start;
    std::shared_ptr<const engine::RunSnapshot> snapshot = controller_->TakeLatest();
    if (!snapshot)
    {
        return;
    }
    if (snapshot->generation != ui_generation_)
    {
        ++snapshots_stale_;
        return;
    }
    if (snapshot->sequence <= last_sequence_)
    {
        return;
    }
    last_sequence_ = snapshot->sequence;
    const bool was_final = snapshot_ && snapshot_->final;
    snapshot_ = std::move(snapshot);
    table_.SetData(&config_, snapshot_);
    // coordinator samples other threads with run threads, none before the first sample
    if (settings_.show_all_threads && !snapshot_->other_threads.empty())
    {
        table_.SetOtherThreads(std::shared_ptr<const std::vector<engine::OtherThreadSnapshot>>(snapshot_, &snapshot_->other_threads));
    }
    table_.RefreshVisible();
    UpdateDetails();
    if (snapshot_->final && !was_final)
    {
        OnRunFinished();
    }
    UpdateCommandStates();
    UpdateStatus();
    ++snapshots_applied_;
    const double elapsed = ElapsedMs(start);
    snapshot_apply_max_ms_ = std::max(snapshot_apply_max_ms_, elapsed);
    perf_.Event("snapshot_apply", elapsed, "rows", static_cast<long long>(snapshot_->threads.size()));
}

void MainWindow::OnRunFinished()
{
    if (!controller_->TryReap())
    {
        SetTimer(hwnd_, kReapTimer, 20, nullptr);
    }
    else if (close_pending_)
    {
        SaveSettings();
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return;
    }
    else
    {
        SampleOtherThreads();
    }
    if (!close_pending_ && snapshot_->state == engine::RunState::kFailed && snapshot_->failure)
    {
        SetStatusMessage(L"Run failed: " + snapshot_->failure->Describe());
        // show after applying this snapshot, outside snapshot handling
        PostMessageW(hwnd_, kRunFailedMessage, 0, static_cast<LPARAM>(ui_generation_));
    }
    else if (!close_pending_)
    {
        SetStatusMessage(L"Run " + snapshot_->run_id + L" completed");
    }
}

void MainWindow::AddThread(bool duplicate)
{
    const std::vector<uint32_t> selected = table_.SelectedThreads();
    engine::ThreadConfig thread = duplicate && !selected.empty() ? config_.threads[selected.front()] : engine::MakeDefaultThread(config_.threads.size());
    thread.name = duplicate ? thread.name + L" copy" : engine::NextThreadName(config_);
    const size_t insert_at = duplicate && !selected.empty() ? selected.front() + 1 : config_.threads.size();
    config_.threads.insert(config_.threads.begin() + static_cast<ptrdiff_t>(insert_at), std::move(thread));
    thread_keys_.insert(thread_keys_.begin() + static_cast<ptrdiff_t>(insert_at), next_thread_key_++);
    OnConfigChanged(true);
    table_.SelectThread(static_cast<int>(insert_at), true);
    OnSelectionChanged();
}

void MainWindow::RemoveThreads()
{
    std::vector<uint32_t> selected = table_.SelectedThreads();
    if (selected.empty())
    {
        return;
    }
    for (auto it = selected.rbegin(); it != selected.rend(); ++it)
    {
        config_.threads.erase(config_.threads.begin() + *it);
        thread_keys_.erase(thread_keys_.begin() + *it);
    }
    OnConfigChanged(true);
    const int next = std::min<int>(static_cast<int>(selected.front()), static_cast<int>(config_.threads.size()) - 1);
    if (next >= 0)
    {
        table_.SelectThread(next, true);
    }
    OnSelectionChanged();
}

void MainWindow::MoveThread(int delta)
{
    const std::vector<uint32_t> selected = table_.SelectedThreads();
    if (selected.size() != 1)
    {
        return;
    }
    const int from = static_cast<int>(selected.front());
    const int to = from + delta;
    if (to < 0 || to >= static_cast<int>(config_.threads.size()))
    {
        return;
    }
    std::swap(config_.threads[static_cast<size_t>(from)], config_.threads[static_cast<size_t>(to)]);
    std::swap(thread_keys_[static_cast<size_t>(from)], thread_keys_[static_cast<size_t>(to)]);
    OnConfigChanged(true);
    table_.SelectThread(to, true);
    OnSelectionChanged();
}

HMENU MainWindow::BuildPriorityMenu(const std::vector<uint32_t>& selected) const
{
    HMENU menu = CreatePopupMenu();
    for (size_t index = 0; index < std::size(engine::kAllRelativeThreadPriorities); ++index)
    {
        const int id = cmd::kThreadPriorityFirst + static_cast<int>(index);
        const std::wstring text = engine::DescribeRelativePriority(static_cast<int32_t>(engine::kAllRelativeThreadPriorities[index]));
        MENUITEMINFOW item = {sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_STATE;
        item.fType = MFT_STRING | MFT_RADIOCHECK;
        item.fState = (CommandEnabled(id, selected) ? MFS_ENABLED : MFS_GRAYED) | (CommandChecked(id, selected) ? MFS_CHECKED : MFS_UNCHECKED);
        item.wID = static_cast<UINT>(id);
        item.dwTypeData = const_cast<wchar_t*>(text.c_str());
        InsertMenuItemW(menu, static_cast<UINT>(index), TRUE, &item);
    }
    return menu;
}

HMENU MainWindow::BuildActivityMenu(const std::vector<uint32_t>& selected) const
{
    HMENU menu = CreatePopupMenu();
    for (int id = cmd::kThreadActivityFirst; id <= cmd::kThreadActivityCustom; ++id)
    {
        if (id == cmd::kThreadActivityCustom)
        {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        const std::wstring text = ActivityMenuText(id);
        MENUITEMINFOW item = {sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_STATE;
        item.fType = MFT_STRING | MFT_RADIOCHECK;
        item.fState = (CommandEnabled(id, selected) ? MFS_ENABLED : MFS_GRAYED) | (CommandChecked(id, selected) ? MFS_CHECKED : MFS_UNCHECKED);
        item.wID = static_cast<UINT>(id);
        item.dwTypeData = const_cast<wchar_t*>(text.c_str());
        InsertMenuItemW(menu, static_cast<UINT>(GetMenuItemCount(menu)), TRUE, &item);
    }
    return menu;
}

void MainWindow::ShowContextMenu(POINT screen)
{
    HMENU menu = CreatePopupMenu();
    const std::vector<uint32_t> selected = table_.SelectedThreads();
    auto add = [&](int id, const wchar_t* text) {
        win32ui::AppendMenuItem(menu, MF_STRING | (CommandEnabled(id, selected) ? 0 : MF_GRAYED) | (CommandChecked(id, selected) ? MF_CHECKED : 0), static_cast<UINT_PTR>(id), text, ShortcutText(id));
    };
    add(cmd::kEditCopy, L"&Copy");
    add(cmd::kEditCopyIds, L"Copy &TIDs");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(cmd::kThreadRename, L"Re&name");
    add(cmd::kThreadActive, L"Activ&e");
    AppendMenuW(menu, MF_POPUP | (ThreadSettingEditable(selected) ? 0 : MF_GRAYED), reinterpret_cast<UINT_PTR>(BuildActivityMenu(selected)), L"Activit&y");
    AppendMenuW(menu, MF_POPUP | (ThreadSettingEditable(selected) ? 0 : MF_GRAYED), reinterpret_cast<UINT_PTR>(BuildPriorityMenu(selected)), L"&Priority");
    add(cmd::kThreadBoost, L"Priority &boost");
    add(cmd::kThreadSharedMutex, L"Shared mute&x");
    add(cmd::kThreadAffinity, L"A&ffinity...");
    add(cmd::kThreadCpuSets, L"CPU &Sets...");
    add(cmd::kThreadIdeal, L"&Ideal processor...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(cmd::kRunSuspend, L"S&uspend");
    add(cmd::kRunResume, L"&Resume");
    add(cmd::kRunStopSelected, L"St&op");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(cmd::kThreadAdd, L"&Add");
    add(cmd::kThreadDuplicate, L"Dup&licate");
    add(cmd::kThreadRemove, L"Re&move");
    add(cmd::kThreadMoveUp, L"Mo&ve up");
    add(cmd::kThreadMoveDown, L"Move do&wn");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(cmd::kThreadDetails, L"&Details");
    const int command = win32ui::TrackContextMenu(menu, screen, hwnd_);
    if (command && hwnd_)
    {
        OnCommand(command);
    }
}

std::wstring MainWindow::ToolTipText(int command) const
{
    if (command == cmd::kRunStart)
    {
        return L"Start, " + (config_.duration_s ? DescribeDuration(*config_.duration_s) : std::wstring(L"unlimited")) + L", samples every " + DescribeInterval(config_.sample_interval_ms);
    }
    const auto button = std::ranges::find(kToolButtons, command, &ToolButton::command);
    return button != std::end(kToolButtons) && button->tip ? button->tip : L"";
}

void MainWindow::ShowDurationMenu(const RECT& button)
{
    // menu bar owns the popup, WM_INITMENUPOPUP updates it like Run > Duration
    const int command = win32ui::TrackDropdownMenu(menu_.duration_menu(), button, hwnd_);
    if (command && hwnd_)
    {
        OnCommand(command);
    }
}

bool MainWindow::ThreadSettingEditable() const
{
    return ThreadSettingEditable(table_.SelectedThreads());
}

bool MainWindow::ThreadSettingEditable(const std::vector<uint32_t>& selected) const
{
    if (selected.empty() || RunBusy())
    {
        return false;
    }
    for (uint32_t thread : selected)
    {
        if (thread >= config_.threads.size())
        {
            return false;
        }
    }
    return true;
}

bool MainWindow::RunLive() const
{
    return snapshot_ && snapshot_->generation == ui_generation_ && !snapshot_->final &&
           (snapshot_->state == engine::RunState::kStarting || snapshot_->state == engine::RunState::kActive);
}

bool MainWindow::ThreadPolicyEditable(const std::vector<uint32_t>& selected) const
{
    if (!RunBusy())
    {
        return ThreadSettingEditable(selected);
    }
    // threads added or removed before the run would shift indexes, they are blocked while busy
    if (selected.empty() || !RunLive() || config_.threads.size() != run_config_.threads.size())
    {
        return false;
    }
    for (uint32_t thread : selected)
    {
        if (thread >= run_config_.threads.size() || !run_config_.threads[thread].active)
        {
            return false;
        }
    }
    return true;
}

void MainWindow::ApplyLivePolicy(const std::vector<uint32_t>& threads)
{
    if (!RunLive())
    {
        return;
    }
    for (uint32_t thread : threads)
    {
        if (thread < config_.threads.size())
        {
            controller_->ApplyThreadPolicy(thread, config_.threads[thread].scheduling, config_.threads[thread].affinity);
        }
    }
    controller_->RequestSample();
}

void MainWindow::SetThreadRelativePriority(engine::RelativeThreadPriority relative_priority)
{
    const std::vector<uint32_t> selected = table_.SelectedThreads();
    for (uint32_t thread : selected)
    {
        config_.threads[thread].scheduling.relative_priority = relative_priority;
    }
    OnConfigChanged(false);
    ApplyLivePolicy(selected);
}

void MainWindow::ToggleThreadPriorityBoost()
{
    const std::vector<uint32_t> selected = table_.SelectedThreads();
    const int lead = RepresentativeThread(selected);
    const bool enable = lead < 0 || !config_.threads[static_cast<size_t>(lead)].scheduling.priority_boost_enabled;
    for (uint32_t thread : selected)
    {
        config_.threads[thread].scheduling.priority_boost_enabled = enable;
    }
    OnConfigChanged(false);
    ApplyLivePolicy(selected);
}

void MainWindow::ToggleThreadSharedMutex()
{
    const std::vector<uint32_t> selected = table_.SelectedThreads();
    const int lead = RepresentativeThread(selected);
    const bool use = lead < 0 || !config_.threads[static_cast<size_t>(lead)].uses_shared_mutex;
    for (uint32_t thread : selected)
    {
        config_.threads[thread].uses_shared_mutex = use;
    }
    OnConfigChanged(false);
}

void MainWindow::ToggleThreadActive(const std::vector<uint32_t>& threads)
{
    if (threads.empty())
    {
        return;
    }
    const int lead = RepresentativeThread(threads);
    if (lead < 0)
    {
        return;
    }
    const bool active = !config_.threads[static_cast<size_t>(lead)].active;
    for (uint32_t thread : threads)
    {
        config_.threads[thread].active = active;
    }
    table_.RedrawCheckBoxes(threads);
    OnConfigChanged(false);
}

void MainWindow::SetThreadActivityLevel(engine::ActivityLevel level)
{
    for (uint32_t thread : table_.SelectedThreads())
    {
        config_.threads[thread].activity.work_percent = engine::ActivityLevelWork(level);
    }
    OnConfigChanged(false);
}

void MainWindow::EditThreadActivity(engine::Field focus)
{
    const std::vector<uint32_t> selected = table_.SelectedThreads();
    const int focused = RepresentativeThread(selected);
    if (focused < 0)
    {
        return;
    }
    if (ShowActivityDialog(hwnd_, &config_, selected, focused, focus) && hwnd_)
    {
        OnConfigChanged(false);
    }
}

void MainWindow::RenameThread(int thread, std::wstring name)
{
    if (thread < 0 || static_cast<size_t>(thread) >= config_.threads.size() || !ThreadSettingEditable())
    {
        return;
    }
    name = platform::Trim(name);
    if (name.empty() || name == config_.threads[static_cast<size_t>(thread)].name)
    {
        return;
    }
    config_.threads[static_cast<size_t>(thread)].name = std::move(name);
    OnConfigChanged(false);
}

void MainWindow::EditThreadAffinity(int command)
{
    const std::vector<uint32_t> selected = table_.SelectedThreads();
    const int focused = RepresentativeThread(selected);
    if (focused < 0)
    {
        return;
    }
    const engine::ThreadAffinityConfig& current = config_.threads[static_cast<size_t>(focused)].affinity;
    PickerOptions options;
    options.mode = command == cmd::kThreadAffinity ? PickerMode::kGroupAffinity : command == cmd::kThreadCpuSets ? PickerMode::kCpuSets
                                                                                                                 : PickerMode::kSingleProcessor;
    const std::wstring scope = selected.size() == 1 ? config_.threads[static_cast<size_t>(focused)].name : std::to_wstring(selected.size()) + L" threads";
    options.title = (command == cmd::kThreadAffinity ? L"Affinity of " : command == cmd::kThreadCpuSets ? L"CPU Sets of "
                                                                                                        : L"Ideal processor of ") +
                    scope;
    // Windows cannot clear an ideal processor, a running thread keeps one
    options.allow_none = command == cmd::kThreadIdeal && !RunLive();
    PickerSelection initial;
    initial.group = current.group_affinity.group;
    initial.mask = current.mode == engine::AffinityMode::kGroupAffinity ? current.group_affinity.mask : 0;
    initial.selected_cpu_set_ids = current.mode == engine::AffinityMode::kCpuSets ? current.selected_cpu_set_ids : std::vector<uint32_t>{};
    initial.processor = current.ideal_processor;
    initial.has_processor = current.ideal_processor_enabled;
    // apply selection to a config copy for dialog validation and OK
    auto apply = [command, selected](engine::RunConfig& config, const PickerSelection& picked) {
        for (uint32_t thread : selected)
        {
            engine::ThreadAffinityConfig& affinity_config = config.threads[thread].affinity;
            if (command == cmd::kThreadAffinity)
            {
                affinity_config.group_affinity.group = picked.group;
                affinity_config.group_affinity.mask = picked.mask;
                if (picked.mask != 0)
                {
                    affinity_config.mode = engine::AffinityMode::kGroupAffinity;
                }
                else if (affinity_config.mode == engine::AffinityMode::kGroupAffinity)
                {
                    affinity_config.mode = engine::AffinityMode::kDefault;
                }
            }
            else if (command == cmd::kThreadCpuSets)
            {
                affinity_config.selected_cpu_set_ids = picked.selected_cpu_set_ids;
                if (!picked.selected_cpu_set_ids.empty())
                {
                    affinity_config.mode = engine::AffinityMode::kCpuSets;
                }
                else if (affinity_config.mode == engine::AffinityMode::kCpuSets)
                {
                    affinity_config.mode = engine::AffinityMode::kDefault;
                }
            }
            else
            {
                affinity_config.ideal_processor_enabled = picked.has_processor;
                if (picked.has_processor)
                {
                    affinity_config.ideal_processor = picked.processor;
                }
            }
        }
    };
    options.validate = [this, apply, selected](const PickerSelection& picked) {
        engine::RunConfig copy = config_;
        apply(copy, picked);
        std::vector<PickerMessage> messages;
        const auto process = platform::QueryProcessSettings();
        for (const auto& issue : engine::Validate(copy, environment_.topology, process ? process.value() : platform::ProcessSettings{}).issues)
        {
            if (issue.scope != engine::IssueScope::kThread || std::find(selected.begin(), selected.end(), static_cast<uint32_t>(issue.thread)) == selected.end())
            {
                continue;
            }
            switch (issue.field)
            {
            case engine::Field::kGroupAffinityGroup:
            case engine::Field::kGroupAffinityMask:
            case engine::Field::kSelectedCpuSets:
            case engine::Field::kIdealProcessor:
                break;
            default:
                continue;
            }
            const std::wstring text = (selected.size() > 1 ? copy.threads[static_cast<size_t>(issue.thread)].name + L": " : L"") + issue.message;
            const bool error = issue.severity == engine::IssueSeverity::kError;
            if (std::none_of(messages.begin(), messages.end(), [&](const PickerMessage& existing) { return existing.text == text; }))
            {
                messages.push_back({error, text});
            }
        }
        return messages;
    };
    const auto picked = ShowProcessorPicker(hwnd_, options, environment_.topology, initial);
    if (!picked || !hwnd_)
    {
        return;
    }
    apply(config_, *picked);
    OnConfigChanged(false);
    ApplyLivePolicy(selected);
}

void MainWindow::FocusFirstIssue()
{
    for (const auto& issue : validation_.issues)
    {
        if (issue.severity != engine::IssueSeverity::kError)
        {
            continue;
        }
        if (issue.scope == engine::IssueScope::kRun)
        {
            // dialogs show their own message
            if (issue.field == engine::Field::kRunDuration || issue.field == engine::Field::kSampleInterval)
            {
                OnCommand(issue.field == engine::Field::kRunDuration ? cmd::kRunDurationCustom : cmd::kViewUpdateSpeedCustom);
                return;
            }
            if (issue.field == engine::Field::kThreadActive)
            {
                const int thread = RepresentativeThread(table_.SelectedThreads());
                table_.SelectThread(thread >= 0 ? thread : 0, thread < 0);
                SetFocus(table_.hwnd());
            }
            return;
        }
        if (issue.scope == engine::IssueScope::kThread && issue.thread >= 0)
        {
            table_.SelectThread(issue.thread, true);
            OnSelectionChanged();
            switch (issue.field)
            {
            case engine::Field::kThreadName:
                table_.BeginRename(issue.thread);
                return;
            case engine::Field::kGroupAffinityGroup:
            case engine::Field::kGroupAffinityMask:
                EditThreadAffinity(cmd::kThreadAffinity);
                return;
            case engine::Field::kSelectedCpuSets:
                EditThreadAffinity(cmd::kThreadCpuSets);
                return;
            case engine::Field::kIdealProcessor:
                EditThreadAffinity(cmd::kThreadIdeal);
                return;
            case engine::Field::kWorkPercent:
            case engine::Field::kPeriod:
            case engine::Field::kWaitableTimerKind:
                EditThreadActivity(issue.field);
                return;
            default:
                return;
            }
        }
    }
}

void MainWindow::ShowAbout()
{
    ui::ShowAbout(hwnd_, L"About ThreadSim", {
                                                 L"© nohuto 2026",
                                                 L"Repository: <a href=\"https://github.com/nohuto/thread-sim\">https://github.com/nohuto/thread-sim</a>",
                                                 L"Discord: <a href=\"https://discord.noverse.dev\">https://discord.noverse.dev</a>",
                                                 L"Website: <a href=\"https://www.noverse.dev/\">https://www.noverse.dev/</a>",
                                                 L"Email: <a href=\"mailto:contact@noverse.dev\">contact@noverse.dev</a>",
                                             });
}

void MainWindow::SetProcessPriorityClass(DWORD priority_class)
{
    const platform::Status status = platform::SetProcessPriorityClassValue(priority_class);
    if (RunLive())
    {
        controller_->RecordProcessChange(L"priority_class", PriorityClassName(priority_class), status);
    }
    if (!status)
    {
        ShowError(hwnd_, L"Priority Class", L"The priority class could not be set.", status.error());
        return;
    }
    // without SeIncreaseBasePriorityPrivilege, Windows substitutes High for Realtime
    const DWORD actual = GetPriorityClass(GetCurrentProcess());
    if (actual != priority_class)
    {
        SetStatusMessage(L"Windows set " + PriorityClassName(actual) + L", " + PriorityClassName(priority_class) + L" needs SeIncreaseBasePriorityPrivilege");
    }
}

void MainWindow::EditProcessPlacement(int command)
{
    const auto current = platform::QueryProcessSettings();
    const wchar_t* title = command == cmd::kProcessAffinity ? L"Process affinity" : L"Process CPU Sets";
    if (!current)
    {
        ShowError(hwnd_, title, L"The process settings could not be read.", current.error());
        return;
    }
    PickerOptions options;
    options.mode = command == cmd::kProcessAffinity ? PickerMode::kGroupAffinity : PickerMode::kCpuSets;
    options.title = title;
    PickerSelection initial;
    initial.group = current->group;
    initial.mask = current->affinity;
    initial.selected_cpu_set_ids = current->default_cpu_set_ids;
    const auto picked = ShowProcessorPicker(hwnd_, options, environment_.topology, initial);
    if (!picked || !hwnd_)
    {
        return;
    }
    if (command == cmd::kProcessAffinity && picked->group != current->group)
    {
        SetStatusMessage(L"The process affinity applies to its primary group " + std::to_wstring(current->group));
        return;
    }
    // empty process selection restores every processor in its group
    const platform::Status status = command == cmd::kProcessAffinity ? platform::SetProcessAffinity(picked->mask ? picked->mask : current->system_affinity)
                                                                     : platform::SetProcessDefaultCpuSetIds(picked->selected_cpu_set_ids);
    if (RunLive())
    {
        controller_->RecordProcessChange(command == cmd::kProcessAffinity ? L"process_affinity" : L"process_default_cpu_sets", command == cmd::kProcessAffinity ? platform::DescribeGroupAffinity(current->group, picked->mask ? picked->mask : current->system_affinity) : DescribeCpuSetIds(picked->selected_cpu_set_ids), status);
    }
    if (!status)
    {
        ShowError(hwnd_, title, L"The setting could not be applied.", status.error());
        return;
    }
    // CPU Set flags identify process defaults, validate thread affinities against process affinity
    RefreshEnvironment();
}

void MainWindow::RefreshEnvironment()
{
    // CPU Set flags, process defaults and affinity may change while the app runs
    if (auto fresh = platform::QueryTopology())
    {
        environment_.topology = std::move(fresh).value();
    }
    Revalidate();
}

void MainWindow::ShowAllThreads(bool show)
{
    settings_.show_all_threads = show;
    controller_->SetSampleOtherThreads(show);
    if (!show)
    {
        KillTimer(hwnd_, kOtherThreadsTimer);
        other_threads_interval_ms_ = 0;
        table_.SetOtherThreads(nullptr);
        table_.RefreshVisible();
        UpdateStatus();
        return;
    }
    if (RunBusy())
    {
        controller_->RequestSample();
        return;
    }
    SampleOtherThreads();
}

void MainWindow::SampleOtherThreads()
{
    if (!settings_.show_all_threads || RunBusy() || other_sampling_paused_)
    {
        KillTimer(hwnd_, kOtherThreadsTimer);
        other_threads_interval_ms_ = 0;
        return;
    }
    // without a run, all process threads count as other threads
    const int64_t start = platform::QpcClock::Now();
    if (auto sampled = other_sampler_.Sample({}))
    {
        perf_.Event("other_threads_query", ElapsedMs(start), "nt_query_us", other_sampler_.last_query_ticks() * 1000000 / platform::QpcClock::Frequency());
        table_.SetOtherThreads(std::make_shared<const std::vector<engine::OtherThreadSnapshot>>(std::move(sampled).value()));
        table_.RefreshVisible(true);
        UpdateStatus();
        perf_.Event("other_threads_sample", ElapsedMs(start));
    }
    else
    {
        SetStatusMessage(L"The other threads could not be sampled: " + sampled.error().Describe());
    }
    other_threads_interval_ms_ = std::max(config_.sample_interval_ms, kIdleOtherThreadsMs);
    SetTimer(hwnd_, kOtherThreadsTimer, other_threads_interval_ms_, nullptr);
}

void MainWindow::FollowSampleInterval()
{
    if (other_threads_interval_ms_ != 0 && other_threads_interval_ms_ != std::max(config_.sample_interval_ms, kIdleOtherThreadsMs))
    {
        SampleOtherThreads();
    }
}

void MainWindow::SaveSettings()
{
    if (environment_.settings_path.empty() || !hwnd_)
    {
        return;
    }
    WINDOWPLACEMENT placement = {sizeof(placement)};
    if (GetWindowPlacement(hwnd_, &placement))
    {
        settings_.has_window = true;
        settings_.window = placement.rcNormalPosition;
        settings_.window_dpi = win32ui::DpiOverride() ? GetDpiForWindow(hwnd_) : dpi_;
        settings_.maximized = placement.showCmd == SW_SHOWMAXIMIZED || IsZoomed(hwnd_);
    }
    if (win32ui::DpiOverride())
    {
        (void)SaveUiSettings(environment_.settings_path, settings_);
        return;
    }
    settings_.column_widths_dip = table_.ColumnWidthsDip(dpi_);
    (void)SaveUiSettings(environment_.settings_path, settings_);
}

} // namespace thread_sim::frame

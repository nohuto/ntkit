// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/details_window.h"

#include "frame/control_ids.h"
#include "frame/formatting.h"

#include "ui/controls.h"
#include "ui/theme.h"

#include "engine/validation.h"

#include "platform/text.h"

#include "resource.h"

#include <win32ui/dialog/dialog_layout.h>
#include <win32ui/dialog/dialog_metrics.h>
#include <win32ui/list_view/list_view.h>
#include <win32ui/window_metrics.h>

#include <algorithm>

namespace thread_sim::frame
{

using namespace ui;

namespace
{

// Windows readback or reason the setting failed
std::wstring Outcome(const engine::SettingOutcome& outcome, std::wstring_view readback)
{
    if (outcome.attempted && !outcome.succeeded)
    {
        return L"Failed: " + (outcome.error ? outcome.error->Describe() : std::wstring(L"unknown error"));
    }
    std::wstring text(readback);
    if (outcome.attempted && outcome.error)
    {
        text += (text.empty() ? L"" : L" ") + std::wstring(L"(") + outcome.error->Describe() + L")";
    }
    return text;
}

constexpr const wchar_t* kColumnTitles[] = {L"Property", L"Config", L"Read back", L"Value"};

std::wstring Seconds(uint64_t value_100ns)
{
    return platform::FormatFixed(static_cast<double>(value_100ns) / 1e7, 3) + L" s";
}

std::wstring Latency(const engine::LatencySummary& summary)
{
    return L"p50 " + std::to_wstring(summary.p50_us) + L" us, p99 " + std::to_wstring(summary.p99_us) + L" us, max " + std::to_wstring(summary.max_us) + L" us";
}

// highest priority first with share of reads, below 100 % activity also shows mean work window offset
std::wstring RunningPriorityText(const engine::RunningPriority& priority, bool windows)
{
    uint64_t total = 0;
    for (uint64_t reads : priority.reads)
    {
        total += reads;
    }
    std::wstring text;
    for (size_t level = priority.reads.size(); level-- > 0;)
    {
        const uint64_t reads = priority.reads[level];
        if (reads == 0)
        {
            continue;
        }
        text += (text.empty() ? L"" : L", ") + std::to_wstring(level) + L": " + platform::FormatFixed(100.0 * static_cast<double>(reads) / static_cast<double>(total), 1) + L" %";
        if (windows)
        {
            text += L" at " + std::to_wstring(priority.window_offset_us_sum[level] / reads / 1000) + L" ms";
        }
    }
    return text;
}

} // namespace

static_assert(IDC_DETAILS_LIST == ids::kDetailsList);

bool DetailsWindow::Open(HWND owner)
{
    return OpenModeless(owner, MAKEINTRESOURCEW(IDD_DETAILS));
}

void DetailsWindow::OnCreate()
{
    min_width_dip_ = 420;
    min_height_dip_ = 240;
    list_ = GetDlgItem(hwnd(), ids::kDetailsList);
    for (int index = 0; index < 4; ++index)
    {
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(kColumnTitles[index]);
        column.cx = Px(150);
        ListView_InsertColumn(list_, index, &column);
    }
    win32ui::AttachDialogListView(list_, ListHooks());
    win32ui::ShowListGridButton(list_);
}

void DetailsWindow::OnLayout(int width, int height)
{
    using namespace win32ui::metrics;
    if (!list_)
    {
        return;
    }
    const int margin = Px(kDialogContentMargin);
    // bottom margin leaves room for the size grip
    const int bottom = height - std::max(margin, win32ui::SystemMetricForDpi(SM_CYHSCROLL, dpi()));
    win32ui::Place(list_, margin, margin, std::max(0, width - 2 * margin), std::max(Px(60), bottom - margin));
    FitColumns();
}

void DetailsWindow::FitColumns()
{
    RECT client = {};
    GetClientRect(list_, &client);
    // narrow columns get their widest text, wider columns share remaining space
    // extra room goes to Value, keeping columns side by side
    const int usable = std::max(Px(200), static_cast<int>(client.right) - win32ui::SystemMetricForDpi(SM_CXVSCROLL, dpi()));
    std::array<int, 4> order = {0, 1, 2, 3};
    std::sort(order.begin(), order.end(), [&](int a, int b) { return wanted_[static_cast<size_t>(a)] < wanted_[static_cast<size_t>(b)]; });
    std::array<int, 4> widths = {};
    int remaining = usable;
    for (size_t position = 0; position < order.size(); ++position)
    {
        const size_t column = static_cast<size_t>(order[position]);
        const int share = remaining / static_cast<int>(order.size() - position);
        widths[column] = std::max(Px(60), std::min(std::max(wanted_[column], Px(60)), share));
        remaining -= widths[column];
    }
    widths[3] += std::max(0, remaining);
    for (int column = 0; column < 4; ++column)
    {
        const int fitted = widths[static_cast<size_t>(column)];
        const int current = ListView_GetColumnWidth(list_, column);
        // width 0 columns stay hidden
        if (current > 0 && current != fitted)
        {
            ListView_SetColumnWidth(list_, column, fitted);
        }
    }
}

void DetailsWindow::Show(const engine::ThreadConfig& requested, const engine::RunSnapshot* run, const engine::ThreadSnapshot* observed)
{
    if (!hwnd())
    {
        return;
    }
    const std::wstring title = L"Details of " + requested.name;
    if (title != title_)
    {
        title_ = title;
        SetWindowTextW(hwnd(), title.c_str());
    }
    const uint32_t build = run && run->environment ? run->environment->windows_build : 0;
    const engine::AppliedSettings applied = observed ? observed->applied : engine::AppliedSettings{};
    const engine::ThreadSample* sample = observed && observed->sample ? &*observed->sample : nullptr;
    std::vector<Row> rows;
    rows.push_back({L"TID", L"", L"", observed && observed->thread_id ? std::to_wstring(observed->thread_id) : std::wstring()});
    rows.push_back({L"State", L"", L"", observed ? (sample && sample->error ? L"Query failed: " + sample->error->Describe() : ThreadStateText(*observed, build)) : std::wstring()});
    std::wstring priorities;
    if (sample && sample->base_priority)
    {
        priorities = L"Base " + std::to_wstring(*sample->base_priority);
    }
    if (sample && sample->current_priority)
    {
        priorities += (priorities.empty() ? L"" : L", ") + std::wstring(L"current ") + std::to_wstring(*sample->current_priority);
    }
    rows.push_back({L"Priority", engine::DescribeRelativePriority(static_cast<int32_t>(requested.scheduling.relative_priority)), Outcome(applied.relative_priority, applied.relative_priority_readback ? engine::DescribeRelativePriority(*applied.relative_priority_readback) : L""), priorities});
    rows.push_back({L"Priority boost", requested.scheduling.priority_boost_enabled ? L"On" : L"Off", Outcome(applied.priority_boost, applied.priority_boost_disabled_readback ? (*applied.priority_boost_disabled_readback ? L"Off" : L"On") : L""), L""});
    rows.push_back({L"Running priority", L"", L"", observed && observed->running_priority ? RunningPriorityText(*observed->running_priority, requested.activity.work_percent < 100) : std::wstring()});
    rows.push_back({L"Shared mutex", requested.uses_shared_mutex ? L"Yes" : L"", L"", observed && observed->mutex_wait ? L"Wait " + Latency(*observed->mutex_wait) : std::wstring()});
    const engine::ThreadAffinityConfig& affinity_config = requested.affinity;
    const std::wstring affinity = affinity_config.mode == engine::AffinityMode::kGroupAffinity ? platform::DescribeGroupAffinity(affinity_config.group_affinity.group, affinity_config.group_affinity.mask) : L"";
    const std::wstring group_affinity_readback = applied.group_affinity_readback ? platform::DescribeGroupAffinity(applied.group_affinity_readback->group, applied.group_affinity_readback->mask) : L"";
    rows.push_back({L"Affinity", affinity, Outcome(applied.group_affinity, group_affinity_readback), L""});
    rows.push_back({L"CPU Sets", affinity_config.mode == engine::AffinityMode::kCpuSets ? DescribeCpuSetIds(affinity_config.selected_cpu_set_ids) : L"", Outcome(applied.selected_cpu_sets, applied.selected_cpu_set_ids_readback ? (applied.selected_cpu_set_ids_readback->empty() ? std::wstring(L"none") : DescribeCpuSetIds(*applied.selected_cpu_set_ids_readback)) : L""), L""});
    rows.push_back({L"Ideal processor", affinity_config.ideal_processor_enabled ? platform::DescribeProcessor(affinity_config.ideal_processor) : L"", Outcome(applied.ideal_processor, applied.ideal_processor_readback ? platform::DescribeProcessor(*applied.ideal_processor_readback) : L""), L""});
    const engine::ActivityRelevance relevance = engine::RelevanceOf(requested.activity);
    rows.push_back({L"Activity", DescribeActivity(requested.activity), L"", L""});
    rows.push_back({L"Timer", relevance.waitable_timer_kind ? engine::WaitableTimerKindLabel(requested.activity.waitable_timer_kind) : L"", Outcome(applied.waitable_timer, applied.obtained_waitable_timer_kind ? engine::WaitableTimerKindLabel(*applied.obtained_waitable_timer_kind) : L""), L""});
    if (observed)
    {
        const auto cycle_seconds = engine::CpuSecondsFromCycles(*run, *observed);
        rows.push_back({L"CPU time", L"", L"", cycle_seconds ? platform::FormatFixed(*cycle_seconds, 3) + L" s" : std::wstring()});
        rows.push_back({L"User, kernel time", L"", L"", observed->user_time_100ns && observed->kernel_time_100ns ? Seconds(*observed->user_time_100ns) + L", " + Seconds(*observed->kernel_time_100ns) : std::wstring()});
        rows.push_back({L"CPU %", L"", L"", observed->cpu_percent_one_logical_processor ? platform::FormatFixed(*observed->cpu_percent_one_logical_processor, 1) + L" %" : std::wstring()});
        rows.push_back({L"Cycles", L"", L"", observed->cycles_total ? platform::FormatUnsigned(*observed->cycles_total) : std::wstring()});
        rows.push_back({L"Context switches", L"", L"", sample && sample->context_switches ? platform::FormatUnsigned(*sample->context_switches) : std::wstring()});
        if (relevance.period)
        {
            rows.push_back({L"Missed periods", L"", L"", observed->missed_periods ? std::to_wstring(*observed->missed_periods) : std::wstring()});
            rows.push_back({L"Overruns", L"", L"", observed->overruns ? std::to_wstring(*observed->overruns) : std::wstring()});
            rows.push_back({L"Wake lateness", L"", L"", observed->wake_lateness ? Latency(*observed->wake_lateness) : std::wstring()});
        }
        if (observed->error)
        {
            rows.push_back({L"Error", L"", L"", observed->error->Describe()});
        }
    }
    SetRows(std::move(rows));
}

int CALLBACK DetailsWindow::CompareRows(LPARAM left, LPARAM right, int column, void* context)
{
    const auto* self = static_cast<const DetailsWindow*>(context);
    if (static_cast<size_t>(left) >= self->rows_.size() || static_cast<size_t>(right) >= self->rows_.size() || column < 0 || column >= 4)
    {
        return 0;
    }
    return CompareCellText(self->rows_[static_cast<size_t>(left)][static_cast<size_t>(column)], self->rows_[static_cast<size_t>(right)][static_cast<size_t>(column)]);
}

void DetailsWindow::SetRows(std::vector<Row> rows)
{
    bool resort = false;
    const bool refilled = ListView_GetItemCount(list_) != static_cast<int>(rows.size());
    if (refilled)
    {
        // observed rows appear on first run, refill immediately
        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list_);
        rows_ = std::move(rows);
        for (size_t row = 0; row < rows_.size(); ++row)
        {
            LVITEMW item = {};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = static_cast<int>(row);
            item.pszText = const_cast<wchar_t*>(rows_[row][0].c_str());
            item.lParam = static_cast<LPARAM>(row);
            ListView_InsertItem(list_, &item);
            for (int column = 1; column < 4; ++column)
            {
                ListView_SetItemText(list_, static_cast<int>(row), column, const_cast<wchar_t*>(rows_[row][static_cast<size_t>(column)].c_str()));
            }
        }
        resort = sort_column_ >= 0;
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list_, nullptr, FALSE);
    }
    else
    {
        for (size_t row = 0; row < rows.size(); ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                if (rows[row][static_cast<size_t>(column)] == rows_[row][static_cast<size_t>(column)])
                {
                    continue;
                }
                const int item = sort_column_ >= 0 ? win32ui::FindListViewItemByData(list_, static_cast<LPARAM>(row)) : static_cast<int>(row);
                ListView_SetItemText(list_, item, column, const_cast<wchar_t*>(rows[row][static_cast<size_t>(column)].c_str()));
                resort = resort || column == sort_column_;
            }
        }
        rows_ = std::move(rows);
    }
    if (resort)
    {
        win32ui::SortListViewItems(list_, sort_column_, false, &sort_column_, &sort_ascending_, &sort_earlier_, &DetailsWindow::CompareRows, this);
    }
    // only grow widths while rows stay, changing values must not move columns
    std::array<int, 4> wanted = refilled ? std::array<int, 4>{} : wanted_;
    for (size_t column = 0; column < 4; ++column)
    {
        wanted[column] = std::max(wanted[column], ListView_GetStringWidth(list_, kColumnTitles[column]) + Px(24));
        for (const auto& row : rows_)
        {
            wanted[column] = std::max(wanted[column], ListView_GetStringWidth(list_, row[column].c_str()) + Px(16));
        }
    }
    if (wanted != wanted_)
    {
        wanted_ = wanted;
        FitColumns();
    }
    if (refilled)
    {
        // new rows widen the window to fit text, within monitor bounds
        RECT window = {};
        GetWindowRect(hwnd(), &window);
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(hwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
        RECT client = {};
        GetClientRect(hwnd(), &client);
        const int frame = (window.right - window.left) - client.right;
        int needed = 2 * Px(win32ui::metrics::kDialogContentMargin) + win32ui::SystemMetricForDpi(SM_CXVSCROLL, dpi()) + Px(4);
        for (int value : wanted_)
        {
            needed += std::max(value, Px(60));
        }
        needed = std::min<int>(needed, monitor.rcWork.right - monitor.rcWork.left - frame);
        if (win32ui::GrowDialogWidth(hwnd(), needed))
        {
            GetWindowRect(hwnd(), &window);
            win32ui::ClampToWorkArea(&window);
            SetWindowPos(hwnd(), nullptr, window.left, window.top, window.right - window.left, window.bottom - window.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

LRESULT DetailsWindow::OnNotify(NMHDR* header, bool* handled)
{
    if (header->hwndFrom == list_ && header->code == LVN_COLUMNCLICK)
    {
        win32ui::SortListViewItems(list_, reinterpret_cast<NMLISTVIEW*>(header)->iSubItem, true, &sort_column_, &sort_ascending_, &sort_earlier_, &DetailsWindow::CompareRows, this);
        *handled = true;
        return 0;
    }
    *handled = false;
    return 0;
}

} // namespace thread_sim::frame

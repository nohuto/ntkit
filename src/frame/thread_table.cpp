// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/thread_table.h"

#include "frame/formatting.h"

#include "ui/controls.h"
#include "ui/theme.h"

#include "engine/validation.h"

#include "platform/text.h"

#include <win32ui/system_api.h>
#include <win32ui/window_metrics.h>

#include <algorithm>
#include <numeric>

namespace thread_sim::frame
{

using namespace ui;

namespace
{

struct ColumnSpec
{
    const wchar_t* title;
    int width_dip;
    bool right;
};

constexpr ColumnSpec kColumns[] = {
    {L"Name", 140, false},
    {L"#", 36, true},
    {L"TID", 60, true},
    {L"State", 170, false},
    {L"Activity", 260, false},
    {L"Priority", 140, false},
    {L"Base priority", 84, true},
    {L"Current priority", 96, true},
    {L"Priority boost", 90, false},
    {L"Affinity", 220, false},
    {L"CPU Sets", 130, false},
    {L"Ideal processor", 140, false},
    {L"Shared mutex", 90, false},
    {L"CPU %", 64, true},
    {L"CPU time (s)", 84, true},
    {L"Context switches", 104, true},
};
static_assert(std::size(kColumns) == static_cast<size_t>(TableColumn::kCount));

constexpr size_t kColumnCount = static_cast<size_t>(TableColumn::kCount);

std::wstring Percent(double value)
{
    return platform::FormatFixed(value, 1);
}

std::wstring ActivityText(const engine::ActivityConfig& activity)
{
    return DescribeActivity(activity) + (engine::RelevanceOf(activity).waitable_timer_kind && activity.waitable_timer_kind == engine::WaitableTimerKind::kHighResolution ? L", high resolution timer" : L"");
}

std::wstring Clean(std::wstring text)
{
    std::replace_if(text.begin(), text.end(), [](wchar_t ch) { return ch == L'\t' || ch == L'\r' || ch == L'\n'; }, L' ');
    return text;
}

// other threads have no config, show Windows readback
std::wstring OtherText(const engine::OtherThreadSnapshot& thread, TableColumn column)
{
    const engine::ThreadSample* sample = thread.sample && !thread.sample->previous && !thread.sample->error ? &*thread.sample : nullptr;
    switch (column)
    {
    case TableColumn::kName:
        return thread.name;
    case TableColumn::kThreadId:
        return std::to_wstring(thread.thread_id);
    case TableColumn::kState:
        return sample ? engine::DescribeThreadState(*sample, win32ui::OsVersion().dwBuildNumber) : std::wstring();
    case TableColumn::kRelativePriority:
        return thread.relative_priority ? engine::DescribeRelativePriority(*thread.relative_priority) : std::wstring();
    case TableColumn::kBasePriority:
        return sample && sample->base_priority ? std::to_wstring(*sample->base_priority) : std::wstring();
    case TableColumn::kCurrentPriority:
        return sample && sample->current_priority ? std::to_wstring(*sample->current_priority) : std::wstring();
    case TableColumn::kPriorityBoost:
        return thread.priority_boost_disabled ? (*thread.priority_boost_disabled ? L"Off" : L"On") : std::wstring();
    case TableColumn::kGroupAffinity:
        return thread.group_affinity ? platform::DescribeGroupAffinity(thread.group_affinity->group, thread.group_affinity->mask) : std::wstring();
    case TableColumn::kSelectedCpuSets:
        return DescribeCpuSetIds(thread.selected_cpu_set_ids);
    case TableColumn::kIdealProcessor:
        return thread.ideal_processor ? platform::DescribeProcessor(*thread.ideal_processor) : std::wstring();
    case TableColumn::kCpuPercent:
        return thread.cpu_percent_one_logical_processor ? Percent(*thread.cpu_percent_one_logical_processor) : std::wstring();
    case TableColumn::kCpuTime:
        // use GetThreadTimes without a run counter rate to convert cycles
        return thread.user_time_100ns && thread.kernel_time_100ns ? platform::FormatFixed(static_cast<double>(*thread.user_time_100ns + *thread.kernel_time_100ns) / 1e7, 3) : std::wstring();
    case TableColumn::kContextSwitches:
        return sample && sample->context_switches ? platform::FormatUnsigned(*sample->context_switches) : std::wstring();
    default:
        return {};
    }
}

} // namespace

HWND ThreadTable::Create(HWND parent, int id)
{
    list_ = CreateWindowExW(0, WC_LISTVIEWW, L"Threads", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_EDITLABELS, 0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    // copy and text search read row cells directly, bypassing the 512-character LVN_GETDISPINFO buffer
    hooks_ = *ListHooks();
    hooks_.context = this;
    hooks_.cell_text = [](void* context, HWND, int row, int subitem, std::wstring* text) {
        if (subitem < 0 || static_cast<size_t>(subitem) >= kColumnCount)
        {
            return false;
        }
        *text = static_cast<const ThreadTable*>(context)->RowText(row, static_cast<TableColumn>(subitem));
        return true;
    };
    win32ui::AttachListView(list_, &hooks_, LVS_EX_CHECKBOXES);
    win32ui::ShowListGridButton(list_);
    ListView_SetCallbackMask(list_, LVIS_STATEIMAGEMASK);
    const UINT dpi = win32ui::WindowDpi(parent);
    std::vector<int> widths;
    for (const ColumnSpec& column : kColumns)
    {
        widths.push_back(win32ui::Scale(column.width_dip, dpi));
    }
    ApplyColumns(widths, dpi);
    ApplyColumnOrder();
    return list_;
}

void ThreadTable::ApplyColumns(const std::vector<int>& widths, UINT dpi)
{
    const std::vector<int> defaults = DefaultWidths(dpi);
    std::vector<win32ui::ListColumn> columns(kColumnCount);
    for (size_t index = 0; index < kColumnCount; ++index)
    {
        win32ui::ListColumn& column = columns[index];
        column.title = kColumns[index].title;
        column.format = kColumns[index].right && index != 0 ? LVCFMT_RIGHT : LVCFMT_LEFT;
        column.default_width_dip = win32ui::Unscale(defaults[index], dpi);
        column.width_dip = widths[index] > 0 ? win32ui::Unscale(widths[index], dpi) : column.default_width_dip;
        column.visible = widths[index] > 0;
    }
    win32ui::SetListColumns(list_, columns);
    for (size_t index = 0; index < kColumnCount; ++index)
    {
        ListView_SetColumnWidth(list_, static_cast<int>(index), widths[index]);
    }
}

void ThreadTable::ApplyColumnOrder()
{
    std::vector<int> order(kColumnCount);
    std::iota(order.begin(), order.end(), 0);
    std::swap(order[0], order[static_cast<size_t>(TableColumn::kIndex)]);
    ListView_SetColumnOrderArray(list_, static_cast<int>(order.size()), order.data());
}

void ThreadTable::SetData(const engine::RunConfig* config, std::shared_ptr<const engine::RunSnapshot> snapshot)
{
    config_ = config;
    snapshot_ = std::move(snapshot);
}

void ThreadTable::SetOtherThreads(std::shared_ptr<const std::vector<engine::OtherThreadSnapshot>> threads)
{
    const size_t count = threads ? threads->size() : 0;
    bool same = count == other_view_.size() && (!others_) == (!threads);
    for (size_t index = 0; same && index < count; ++index)
    {
        same = (*threads)[index].thread_id == (*others_)[index].thread_id;
    }
    if (!same && !others_moved_)
    {
        // rows still point into the old list
        KeepOtherSelection();
    }
    others_moved_ = others_moved_ || !same;
    others_ = std::move(threads);
}

void ThreadTable::KeepOtherSelection()
{
    kept_selected_others_.clear();
    for (int row : SelectedRows(list_))
    {
        if (const engine::OtherThreadSnapshot* other = OtherAt(row))
        {
            kept_selected_others_.push_back(other->thread_id);
        }
    }
    const engine::OtherThreadSnapshot* focused = OtherAt(ListView_GetNextItem(list_, -1, LVNI_FOCUSED));
    kept_focused_other_ = focused ? focused->thread_id : 0;
}

void ThreadTable::RefreshAll(bool others_only)
{
    const std::vector<uint32_t> selected = SelectedThreads();
    const int focused = FocusedThread();
    // preserve other thread selection by TID
    if (!others_moved_)
    {
        KeepOtherSelection();
    }
    const size_t threads = config_ ? config_->threads.size() : 0;
    // configured threads keep row order, including after rename
    others_only = others_only && view_.size() == threads;
    if (!others_only)
    {
        view_.resize(threads);
        std::iota(view_.begin(), view_.end(), 0);
    }
    other_view_.resize(others_ ? others_->size() : 0);
    std::iota(other_view_.begin(), other_view_.end(), 0);
    others_moved_ = false;
    if (sort_column_ >= 0)
    {
        Resort(others_only);
    }
    const int count = static_cast<int>(view_.size() + other_view_.size());
    ListView_SetItemCountEx(list_, count, LVSICF_NOSCROLL | (others_only ? LVSICF_NOINVALIDATEALL : 0));
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    for (uint32_t thread : selected)
    {
        const int row = RowOf(static_cast<int>(thread));
        if (row >= 0)
        {
            ListView_SetItemState(list_, row, LVIS_SELECTED, LVIS_SELECTED);
        }
    }
    for (size_t index = 0; index < other_view_.size(); ++index)
    {
        const uint32_t id = (*others_)[static_cast<size_t>(other_view_[index])].thread_id;
        const int row = static_cast<int>(view_.size() + index);
        if (std::find(kept_selected_others_.begin(), kept_selected_others_.end(), id) != kept_selected_others_.end())
        {
            ListView_SetItemState(list_, row, LVIS_SELECTED, LVIS_SELECTED);
        }
        if (kept_focused_other_ && id == kept_focused_other_)
        {
            ListView_SetItemState(list_, row, LVIS_FOCUSED, LVIS_FOCUSED);
        }
    }
    if (const int row = RowOf(focused); row >= 0)
    {
        ListView_SetItemState(list_, row, LVIS_FOCUSED, LVIS_FOCUSED);
    }
    if (!others_only)
    {
        shown_.assign(static_cast<size_t>(count), CellTexts{});
        InvalidateRect(list_, nullptr, FALSE);
        return;
    }

    shown_.resize(view_.size());
    shown_.resize(static_cast<size_t>(count));
    RECT area = {};
    GetClientRect(list_, &area);
    RECT row = {};
    if (!view_.empty() && ListView_GetItemRect(list_, static_cast<int>(view_.size()) - 1, &row, LVIR_BOUNDS))
    {
        area.top = std::max(area.top, row.bottom);
    }
    if (area.top < area.bottom)
    {
        InvalidateRect(list_, &area, FALSE);
    }
}

void ThreadTable::RefreshVisible(bool others_only)
{
    if (others_moved_)
    {
        RefreshAll(others_only);
        return;
    }
    const int top = ListView_GetTopIndex(list_);
    const int page = ListView_GetCountPerPage(list_);
    const int count = ListView_GetItemCount(list_);
    if (count <= 0)
    {
        return;
    }
    const int last = std::min(count - 1, top + page);
    // other threads follow configured threads in every sort order
    const int first = others_only ? std::max(top, static_cast<int>(view_.size())) : top;
    if (first > last)
    {
        return;
    }
    if (shown_.size() != static_cast<size_t>(count))
    {
        ListView_RedrawItems(list_, first, last);
        return;
    }
    // run updates change few numeric cells, invalidate only changed cells
    ReadColumnEdges();
    for (int row = first; row <= last; ++row)
    {
        RECT bounds = {};
        if (!ListView_GetItemRect(list_, row, &bounds, LVIR_BOUNDS))
        {
            continue;
        }
        for (size_t column = 0; column < kColumnCount; ++column)
        {
            if (RowText(row, static_cast<TableColumn>(column)) != shown_[static_cast<size_t>(row)][column])
            {
                const RECT cell = {column_edges_[column].first, bounds.top, column_edges_[column].second, bounds.bottom};
                InvalidateRect(list_, &cell, FALSE);
            }
        }
    }
}

void ThreadTable::ReadColumnEdges()
{
    for (size_t column = 0; column < kColumnCount; ++column)
    {
        RECT rect = {};
        // hidden column has no cells to repaint
        win32ui::ListColumnRect(list_, static_cast<int>(column), &rect);
        column_edges_[column] = {rect.left, rect.right};
    }
}

int ThreadTable::RowOf(int thread) const
{
    for (size_t row = 0; row < view_.size(); ++row)
    {
        if (view_[row] == thread)
        {
            return static_cast<int>(row);
        }
    }
    return -1;
}

int ThreadTable::ThreadAt(int row) const
{
    return row >= 0 && static_cast<size_t>(row) < view_.size() ? view_[static_cast<size_t>(row)] : -1;
}

const engine::OtherThreadSnapshot* ThreadTable::OtherAt(int row) const
{
    if (!others_ || row < 0 || static_cast<size_t>(row) < view_.size())
    {
        return nullptr;
    }
    const size_t index = static_cast<size_t>(row) - view_.size();
    // rows may point past a shortened thread list until refresh
    if (index >= other_view_.size() || static_cast<size_t>(other_view_[index]) >= others_->size())
    {
        return nullptr;
    }
    return &(*others_)[static_cast<size_t>(other_view_[index])];
}

std::wstring ThreadTable::RowText(int row, TableColumn column) const
{
    const engine::OtherThreadSnapshot* other = OtherAt(row);
    return other ? OtherText(*other, column) : CellText(ThreadAt(row), column);
}

const engine::ThreadSnapshot* ThreadTable::Observed(int thread) const
{
    if (!snapshot_ || thread < 0 || static_cast<size_t>(thread) >= snapshot_->threads.size())
    {
        return nullptr;
    }
    return &snapshot_->threads[static_cast<size_t>(thread)];
}

std::wstring ThreadTable::CellText(int thread, TableColumn column) const
{
    if (!config_ || thread < 0 || static_cast<size_t>(thread) >= config_->threads.size())
    {
        return {};
    }
    const engine::ThreadConfig& config = config_->threads[static_cast<size_t>(thread)];
    const engine::ThreadSnapshot* observed = Observed(thread);
    const engine::ThreadSample* sample = observed && observed->sample ? &*observed->sample : nullptr;
    switch (column)
    {
    case TableColumn::kName:
        return config.name;
    case TableColumn::kIndex:
        return std::to_wstring(thread + 1);
    case TableColumn::kThreadId:
        return observed && observed->thread_id ? std::to_wstring(observed->thread_id) : std::wstring();
    case TableColumn::kState:
        return observed ? ThreadStateText(*observed, snapshot_->environment ? snapshot_->environment->windows_build : 0) : std::wstring();
    case TableColumn::kRelativePriority:
        return engine::DescribeRelativePriority(static_cast<int32_t>(config.scheduling.relative_priority));
    case TableColumn::kBasePriority:
        return sample && sample->base_priority ? std::to_wstring(*sample->base_priority) : std::wstring();
    case TableColumn::kCurrentPriority:
        return sample && sample->current_priority ? std::to_wstring(*sample->current_priority) : std::wstring();
    case TableColumn::kPriorityBoost:
        return config.scheduling.priority_boost_enabled ? L"On" : L"Off";
    case TableColumn::kGroupAffinity:
        {
            const engine::ThreadAffinityConfig& affinity_config = config.affinity;
            return affinity_config.mode == engine::AffinityMode::kGroupAffinity ? platform::DescribeGroupAffinity(affinity_config.group_affinity.group, affinity_config.group_affinity.mask) : std::wstring();
        }
    case TableColumn::kSelectedCpuSets:
        {
            const engine::ThreadAffinityConfig& affinity_config = config.affinity;
            return affinity_config.mode == engine::AffinityMode::kCpuSets ? DescribeCpuSetIds(affinity_config.selected_cpu_set_ids) : std::wstring();
        }
    case TableColumn::kIdealProcessor:
        {
            const engine::ThreadAffinityConfig& affinity_config = config.affinity;
            return affinity_config.ideal_processor_enabled ? platform::DescribeProcessor(affinity_config.ideal_processor) : std::wstring();
        }
    case TableColumn::kActivity:
        return ActivityText(config.activity);
    case TableColumn::kSharedMutex:
        return config.uses_shared_mutex ? L"Yes" : L"";
    case TableColumn::kCpuPercent:
        return observed && observed->cpu_percent_one_logical_processor ? Percent(*observed->cpu_percent_one_logical_processor) : std::wstring();
    case TableColumn::kCpuTime:
        if (const auto seconds = observed ? engine::CpuSecondsFromCycles(*snapshot_, *observed) : std::nullopt)
        {
            return platform::FormatFixed(*seconds, 3);
        }
        return {};
    case TableColumn::kContextSwitches:
        return sample && sample->context_switches ? platform::FormatUnsigned(*sample->context_switches) : std::wstring();
    default:
        return {};
    }
}

void ThreadTable::Resort(bool others_only)
{
    if (sort_column_ < 0)
    {
        return;
    }
    // sort configured and other threads separately
    const auto sort = [&](std::vector<int>& rows, auto&& text) {
        std::vector<CellTexts> keys(rows.size());
        for (size_t item = 0; item < keys.size(); ++item)
        {
            for (size_t column = 0; column < kColumnCount; ++column)
            {
                keys[item][column] = text(item, static_cast<TableColumn>(column));
            }
        }
        std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
            const auto& left = keys[static_cast<size_t>(a)];
            const auto& right = keys[static_cast<size_t>(b)];
            const int result = win32ui::CompareSorted(sort_column_, sort_ascending_, sort_earlier_, [&](int column) {
                return CompareCellText(left[static_cast<size_t>(column)], right[static_cast<size_t>(column)]);
            });
            return result < 0;
        });
    };
    if (!others_only)
    {
        sort(view_, [&](size_t thread, TableColumn column) { return CellText(static_cast<int>(thread), column); });
    }
    sort(other_view_, [&](size_t other, TableColumn column) { return OtherText((*others_)[other], column); });
}

bool ThreadTable::OnNotify(NMHDR* header, LRESULT* result)
{
    if (!header || header->hwndFrom != list_)
    {
        return false;
    }
    switch (header->code)
    {
    case LVN_GETDISPINFOW:
        {
            auto* info = reinterpret_cast<NMLVDISPINFOW*>(header);
            if (info->item.mask & LVIF_TEXT)
            {
                const std::wstring text = RowText(info->item.iItem, static_cast<TableColumn>(info->item.iSubItem));
                wcsncpy_s(text_, text.c_str(), _TRUNCATE);
                info->item.pszText = text_;
            }
            if (info->item.mask & LVIF_STATE)
            {
                const int thread = ThreadAt(info->item.iItem);
                const bool active = config_ && thread >= 0 && static_cast<size_t>(thread) < config_->threads.size() && config_->threads[static_cast<size_t>(thread)].active;
                info->item.state = INDEXTOSTATEIMAGEMASK(thread < 0 ? 0 : active ? 2
                                                                                 : 1);
                info->item.stateMask = LVIS_STATEIMAGEMASK;
            }
            *result = 0;
            return true;
        }
    case LVN_COLUMNCLICK:
        {
            win32ui::UpdateListSortState(reinterpret_cast<NMLISTVIEW*>(header)->iSubItem, true, &sort_column_, &sort_ascending_, &sort_earlier_);
            win32ui::UpdateListViewSort(list_, sort_column_, sort_ascending_);
            RefreshAll();
            *result = 0;
            return true;
        }
    case NM_CUSTOMDRAW:
        *result = win32ui::DrawListGrid(list_, reinterpret_cast<NMLVCUSTOMDRAW*>(header), CustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(header)));
        return true;
    default:
        return false;
    }
}

LRESULT ThreadTable::CustomDraw(NMLVCUSTOMDRAW* draw)
{
    const int row = static_cast<int>(draw->nmcd.dwItemSpec);
    // gray other threads because they are read only
    const COLORREF greyed = win32ui::CurrentThemeColors().muted_text;
    switch (draw->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        ReadColumnEdges();
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        if (OtherAt(row))
        {
            draw->clrText = greyed;
        }
        if (!ListView_GetItemRect(list_, row, &row_bounds_, LVIR_BOUNDS))
        {
            row_bounds_ = {};
        }
        return CDRF_NOTIFYSUBITEMDRAW | (OtherAt(row) ? CDRF_NEWFONT : 0);
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        {
            const size_t index = static_cast<size_t>(draw->iSubItem);
            if (index < kColumnCount && row_bounds_.bottom > row_bounds_.top)
            {
                const RECT cell = {column_edges_[index].first, row_bounds_.top, column_edges_[index].second, row_bounds_.bottom};
                if (!RectVisible(draw->nmcd.hdc, &cell))
                {
                    return CDRF_SKIPDEFAULT;
                }
                // record painted cells here
                if (row >= 0 && static_cast<size_t>(row) < shown_.size())
                {
                    shown_[static_cast<size_t>(row)][index] = RowText(row, static_cast<TableColumn>(index));
                }
            }
            if (OtherAt(row))
            {
                draw->clrText = greyed;
                return CDRF_NEWFONT;
            }
            return CDRF_DODEFAULT;
        }
    default:
        return CDRF_DODEFAULT;
    }
}

std::vector<uint32_t> ThreadTable::SelectedThreads() const
{
    std::vector<uint32_t> threads;
    for (int row : SelectedRows(list_))
    {
        const int thread = ThreadAt(row);
        if (thread >= 0)
        {
            threads.push_back(static_cast<uint32_t>(thread));
        }
    }
    std::sort(threads.begin(), threads.end());
    return threads;
}

size_t ThreadTable::SelectedCount() const
{
    return list_ ? ListView_GetSelectedCount(list_) : 0;
}

int ThreadTable::FocusedThread() const
{
    if (!list_)
    {
        return -1;
    }
    return ThreadAt(ListView_GetNextItem(list_, -1, LVNI_FOCUSED));
}

void ThreadTable::SelectThread(int thread, bool exclusive)
{
    const int row = RowOf(thread);
    if (row < 0)
    {
        return;
    }
    if (exclusive)
    {
        ListView_SetItemState(list_, -1, 0, LVIS_SELECTED);
    }
    ListView_SetItemState(list_, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list_, row, FALSE);
}

int ThreadTable::CheckBoxAt(POINT point) const
{
    LVHITTESTINFO hit = {};
    hit.pt = point;
    const int row = ListView_HitTest(list_, &hit);
    return row >= 0 && (hit.flags & LVHT_ONITEM) == LVHT_ONITEMSTATEICON ? ThreadAt(row) : -1;
}

void ThreadTable::RedrawCheckBoxes(const std::vector<uint32_t>& threads)
{
    ReadColumnEdges();
    const auto& name = column_edges_[static_cast<size_t>(TableColumn::kName)];
    for (uint32_t thread : threads)
    {
        RECT bounds = {};
        if (const int row = RowOf(static_cast<int>(thread)); row >= 0 && ListView_GetItemRect(list_, row, &bounds, LVIR_BOUNDS))
        {
            const RECT cell = {name.first, bounds.top, name.second, bounds.bottom};
            InvalidateRect(list_, &cell, FALSE);
        }
    }
}

void ThreadTable::SelectAll()
{
    ListView_SetItemState(list_, -1, LVIS_SELECTED, LVIS_SELECTED);
}

void ThreadTable::BeginRename(int thread)
{
    const int row = RowOf(thread);
    if (row >= 0)
    {
        SetFocus(list_);
        ListView_EnsureVisible(list_, row, FALSE);
        ListView_EditLabel(list_, row);
    }
}

std::wstring ThreadTable::ThreadIdsText() const
{
    std::wstring text = L"Name\tTID\r\n";
    std::vector<int> rows = SelectedRows(list_);
    if (rows.empty())
    {
        rows.resize(view_.size() + other_view_.size());
        std::iota(rows.begin(), rows.end(), 0);
    }
    for (int row : rows)
    {
        text += Clean(RowText(row, TableColumn::kName)) + L"\t" + RowText(row, TableColumn::kThreadId) + L"\r\n";
    }
    return text;
}

std::vector<int> ThreadTable::ColumnWidthsDip(UINT dpi) const
{
    std::vector<int> widths;
    for (size_t column = 0; column < kColumnCount; ++column)
    {
        widths.push_back(win32ui::Unscale(ListView_GetColumnWidth(list_, static_cast<int>(column)), dpi));
    }
    return widths;
}

void ThreadTable::SetColumnWidths(const std::vector<int>& widths_dip, UINT dpi)
{
    // saved widths for different columns dont apply
    if (widths_dip.size() != kColumnCount)
    {
        ResetColumns(dpi);
        return;
    }
    std::vector<int> widths;
    for (const int width : widths_dip)
    {
        widths.push_back(win32ui::Scale(std::clamp(width, 0, 2000), dpi));
    }
    ApplyColumns(widths, dpi);
}

std::vector<int> ThreadTable::DefaultWidths(UINT dpi) const
{
    std::vector<int> widths;
    for (size_t column = 0; column < kColumnCount; ++column)
    {
        int width = std::max(win32ui::Scale(kColumns[column].width_dip, dpi), ListView_GetStringWidth(list_, kColumns[column].title) + win32ui::Scale(24, dpi));
        if (static_cast<TableColumn>(column) == TableColumn::kActivity)
        {
            // measure longest activity labels below 100 % so wide fonts dont truncate timer kinds
            for (const uint32_t work : {25u, 50u, 75u, 99u})
            {
                const engine::ActivityConfig longest = {work, 10000, engine::WaitableTimerKind::kHighResolution};
                width = std::max(width, ListView_GetStringWidth(list_, ActivityText(longest).c_str()) + win32ui::Scale(24, dpi));
            }
        }
        widths.push_back(width);
    }
    return widths;
}

void ThreadTable::ResetColumns(UINT dpi)
{
    ApplyColumns(DefaultWidths(dpi), dpi);
    ApplyColumnOrder();
}

void ThreadTable::Rescale(UINT old_dpi, UINT new_dpi)
{
    for (size_t column = 0; column < kColumnCount; ++column)
    {
        ListView_SetColumnWidth(list_, static_cast<int>(column), MulDiv(ListView_GetColumnWidth(list_, static_cast<int>(column)), static_cast<int>(new_dpi), static_cast<int>(old_dpi)));
    }
}

} // namespace thread_sim::frame

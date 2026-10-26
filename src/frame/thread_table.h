// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/config.h"
#include "engine/snapshot.h"

#include <win32ui/list_view/list_view.h>
#include <win32ui/list_view/sort.h>

#include <windows.h>

#include <commctrl.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace thread_sim::frame
{

// name is subitem 0 for inline rename, display order puts # first
enum class TableColumn : int
{
    kName = 0,
    kIndex,
    kThreadId,
    kState,
    kActivity,
    kRelativePriority,
    kBasePriority,
    kCurrentPriority,
    kPriorityBoost,
    kGroupAffinity,
    kSelectedCpuSets,
    kIdealProcessor,
    kSharedMutex,
    kCpuPercent,
    kCpuTime,
    kContextSwitches,
    kCount,
};

// virtual list of configured threads with values from their run
// other process threads follow as read only gray rows
class ThreadTable
{
  public:
    HWND Create(HWND parent, int id);
    HWND hwnd() const noexcept
    {
        return list_;
    }
    // snapshot may be null, shown only when its rows belong to config
    void SetData(const engine::RunConfig* config, std::shared_ptr<const engine::RunSnapshot> snapshot);
    // null hides other threads after refresh
    void SetOtherThreads(std::shared_ptr<const std::vector<engine::OtherThreadSnapshot>> threads);
    size_t OtherCount() const noexcept
    {
        return other_view_.size();
    }
    // others_only between runs keeps configured rows unchanged
    void RefreshAll(bool others_only = false);
    void RefreshVisible(bool others_only = false);
    bool OnNotify(NMHDR* header, LRESULT* result);
    // configured threads only, other threads have no config index
    std::vector<uint32_t> SelectedThreads() const;
    size_t SelectedCount() const;
    int FocusedThread() const;
    void SelectThread(int thread, bool exclusive);
    void SelectAll();
    void BeginRename(int thread);
    // names and TIDs of selected rows, all rows if none selected
    std::wstring ThreadIdsText() const;
    std::vector<int> ColumnWidthsDip(UINT dpi) const;
    void SetColumnWidths(const std::vector<int>& widths_dip, UINT dpi);
    // default widths widened to fit headers in the list font
    void ResetColumns(UINT dpi);
    void Rescale(UINT old_dpi, UINT new_dpi);
    std::wstring CellText(int thread, TableColumn column) const;
    // thread check box at point in list coordinates, -1 for none
    int CheckBoxAt(POINT point) const;
    // repaints check boxes, active changes dont affect cell text
    void RedrawCheckBoxes(const std::vector<uint32_t>& threads);

  private:
    int RowOf(int thread) const;
    int ThreadAt(int row) const;
    const engine::OtherThreadSnapshot* OtherAt(int row) const;
    std::wstring RowText(int row, TableColumn column) const;
    // preserve other thread selection and focus by TID across row rebuilds
    void KeepOtherSelection();
    void Resort(bool others_only);
    const engine::ThreadSnapshot* Observed(int thread) const;
    LRESULT CustomDraw(NMLVCUSTOMDRAW* draw);
    // column edges from the header, in list coordinates
    void ReadColumnEdges();
    void ApplyColumnOrder();
    // default pixel widths fitting headers and longest activity text
    std::vector<int> DefaultWidths(UINT dpi) const;
    // column widths in pixels, 0 hides, defaults are restored by Reset columns
    void ApplyColumns(const std::vector<int>& widths, UINT dpi);

    using CellTexts = std::array<std::wstring, static_cast<size_t>(TableColumn::kCount)>;

    HWND list_ = nullptr;
    // app list hooks with table cell text
    win32ui::ListViewHooks hooks_;
    const engine::RunConfig* config_ = nullptr;
    std::shared_ptr<const engine::RunSnapshot> snapshot_;
    std::shared_ptr<const std::vector<engine::OtherThreadSnapshot>> others_;
    std::vector<int> view_;       // row to thread index
    std::vector<int> other_view_; // row after configured threads mapped to others_
    bool others_moved_ = false;   // thread list changed, requires full row refresh
    std::vector<uint32_t> kept_selected_others_;
    uint32_t kept_focused_other_ = 0;
    int sort_column_ = -1;
    bool sort_ascending_ = true;
    win32ui::SortKeys sort_earlier_;
    mutable wchar_t text_[512] = {};
    std::vector<CellTexts> shown_; // cell text per row at last refresh
    std::array<std::pair<int, int>, static_cast<size_t>(TableColumn::kCount)> column_edges_ = {};
    RECT row_bounds_ = {};
};

} // namespace thread_sim::frame

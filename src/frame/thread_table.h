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

class ThreadTable
{
  public:
    HWND Create(HWND parent, int id);
    HWND hwnd() const noexcept
    {
        return list_;
    }
    void SetData(const engine::RunConfig* config, std::shared_ptr<const engine::RunSnapshot> snapshot);
    void SetOtherThreads(std::shared_ptr<const std::vector<engine::OtherThreadSnapshot>> threads);
    size_t OtherCount() const noexcept
    {
        return other_view_.size();
    }
    void RefreshAll(bool others_only = false);
    void RefreshVisible(bool others_only = false);
    bool OnNotify(NMHDR* header, LRESULT* result);
    std::vector<uint32_t> SelectedThreads() const;
    size_t SelectedCount() const;
    int FocusedThread() const;
    void SelectThread(int thread, bool exclusive);
    void SelectAll();
    void BeginRename(int thread);
    std::wstring ThreadIdsText() const;
    std::vector<int> ColumnWidthsDip(UINT dpi) const;
    void SetColumnWidths(const std::vector<int>& widths_dip, UINT dpi);
    void ResetColumns(UINT dpi);
    void Rescale(UINT old_dpi, UINT new_dpi);
    std::wstring CellText(int thread, TableColumn column) const;
    int CheckBoxAt(POINT point) const;
    void RedrawCheckBoxes(const std::vector<uint32_t>& threads);

  private:
    int RowOf(int thread) const;
    int ThreadAt(int row) const;
    const engine::OtherThreadSnapshot* OtherAt(int row) const;
    std::wstring RowText(int row, TableColumn column) const;
    void KeepOtherSelection();
    void Resort(bool others_only);
    const engine::ThreadSnapshot* Observed(int thread) const;
    LRESULT CustomDraw(NMLVCUSTOMDRAW* draw);
    void ReadColumnEdges();
    void ApplyColumnOrder();
    std::vector<int> DefaultWidths(UINT dpi) const;
    void ApplyColumns(const std::vector<int>& widths, UINT dpi);

    using CellTexts = std::array<std::wstring, static_cast<size_t>(TableColumn::kCount)>;

    HWND list_ = nullptr;
    // app list hooks with table cell text
    win32ui::ListViewHooks hooks_;
    const engine::RunConfig* config_ = nullptr;
    std::shared_ptr<const engine::RunSnapshot> snapshot_;
    std::shared_ptr<const std::vector<engine::OtherThreadSnapshot>> others_;
    std::vector<int> view_;
    std::vector<int> other_view_;
    bool others_moved_ = false; // thread list changed, requires full row refresh
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

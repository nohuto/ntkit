// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ui/dialog.h"

#include "engine/config.h"
#include "engine/snapshot.h"

#include <win32ui/list_view/sort.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace thread_sim::frame
{

// live config, readback and measurements for one thread
class DetailsWindow final : public win32ui::DialogWindow
{
  public:
    // thread_key identifies the thread across main window reorders
    explicit DetailsWindow(uint64_t thread_key)
        : thread_key_(thread_key)
    {
    }
    uint64_t thread_key() const noexcept
    {
        return thread_key_;
    }
    bool Open(HWND owner);
    // run and observed are null before the first thread run
    void Show(const engine::ThreadConfig& requested, const engine::RunSnapshot* run, const engine::ThreadSnapshot* observed);

  protected:
    void OnCreate() override;
    void OnLayout(int width, int height) override;
    LRESULT OnNotify(NMHDR* header, bool* handled) override;
    HWND initial_focus() const override
    {
        return list_;
    }

  private:
    using Row = std::array<std::wstring, 4>;

    void SetRows(std::vector<Row> rows);
    void FitColumns();
    static int CALLBACK CompareRows(LPARAM left, LPARAM right, int column, void* context);

    uint64_t thread_key_ = 0;
    std::wstring title_;
    HWND list_ = nullptr;
    std::vector<Row> rows_;
    std::array<int, 4> wanted_ = {};
    int sort_column_ = -1;
    bool sort_ascending_ = true;
    win32ui::SortKeys sort_earlier_;
};

} // namespace thread_sim::frame

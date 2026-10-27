// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/processor_picker.h"

#include "frame/control_ids.h"

#include "ui/controls.h"
#include "ui/dialog.h"
#include "ui/theme.h"

#include "platform/text.h"

#include "resource.h"

#include <win32ui/dialog/combo_box.h>
#include <win32ui/dialog/dialog_layout.h>
#include <win32ui/dialog/dialog_metrics.h>
#include <win32ui/list_view/list_view.h>
#include <win32ui/list_view/sort.h>

#include <algorithm>

namespace thread_sim::frame
{

using namespace ui;

namespace
{

std::wstring IndexText(uint32_t value)
{
    return value == platform::kUnknownIndex ? std::wstring() : std::to_wstring(value);
}

int TextHeight(HWND control, HFONT font, int width)
{
    const std::wstring text = GetText(control);
    if (text.empty() || width <= 0)
    {
        return 0;
    }
    RECT rect = {0, 0, width, 0};
    HDC dc = GetDC(control);
    HGDIOBJ old = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(control, dc);
    return rect.bottom;
}

static_assert(IDC_PICKER_LIST == ids::kPickerList && IDC_PICKER_GROUP_COMBO == ids::kPickerGroupCombo && IDC_PICKER_SELECT_ALL == ids::kPickerSelectAll && IDC_PICKER_CLEAR == ids::kPickerClear && IDC_PICKER_VALIDATION == ids::kPickerValidation && IDC_PICKER_WARNINGS == ids::kPickerWarnings);

class PickerDialog final : public win32ui::DialogWindow
{
  public:
    PickerDialog(const PickerOptions& options, const platform::Topology& topology, const PickerSelection& initial)
        : options_(options), mode_(options.mode), topology_(topology), selection_(initial)
    {
    }

    PickerSelection selection() const
    {
        return selection_;
    }

  protected:
    void OnCreate() override
    {
        min_width_dip_ = fonts().Grow(480);
        min_height_dip_ = fonts().Grow(300);
        SetWindowTextW(hwnd(), options_.title.c_str());
        const bool multi = mode_ == PickerMode::kGroupAffinity || mode_ == PickerMode::kCpuSets;
        // remove unused controls from visibility and tab order
        const auto keep = [&](int id, bool used) {
            HWND control = GetDlgItem(hwnd(), id);
            if (!used)
            {
                DestroyWindow(control);
                return HWND{};
            }
            return control;
        };
        const bool groups = mode_ == PickerMode::kGroupAffinity && topology_.groups.size() > 1;
        group_label_ = keep(IDC_PICKER_GROUP_LABEL, groups);
        group_ = keep(ids::kPickerGroupCombo, groups);
        if (group_)
        {
            SetAccessibleName(group_, L"Processor group");
            for (const auto& group : topology_.groups)
            {
                const std::wstring text = L"Group " + std::to_wstring(group.index) + L", " + std::to_wstring(group.active_processor_count) + L" active processors";
                SendMessageW(group_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            }
        }
        list_ = GetDlgItem(hwnd(), ids::kPickerList);
        if (!multi)
        {
            SetWindowLongPtrW(list_, GWL_STYLE, GetWindowLongPtrW(list_, GWL_STYLE) | LVS_SINGLESEL);
        }
        // apply theme before enabling check boxes so their images use the right style
        win32ui::ApplyThemeToWindow(list_);
        win32ui::AttachDialogListView(list_, ListHooks(), multi ? LVS_EX_CHECKBOXES : 0);
        win32ui::ShowListGridButton(list_);
        errors_ = GetDlgItem(hwnd(), ids::kPickerValidation);
        SetTextRole(errors_, TextRole::error);
        warnings_ = GetDlgItem(hwnd(), ids::kPickerWarnings);
        select_all_ = keep(ids::kPickerSelectAll, multi);
        clear_ = keep(ids::kPickerClear, multi || options_.allow_none);
        ok_ = GetDlgItem(hwnd(), IDOK);
        cancel_ = keep(IDCANCEL, mode_ != PickerMode::kBrowse);
        if (mode_ == PickerMode::kBrowse)
        {
            SetWindowTextW(ok_, L"Close");
        }
        int group_index = 0;
        for (size_t index = 0; index < topology_.groups.size(); ++index)
        {
            if (topology_.groups[index].index == selection_.group)
            {
                group_index = static_cast<int>(index);
            }
        }
        if (group_)
        {
            SendMessageW(group_, CB_SETCURSEL, static_cast<WPARAM>(group_index), 0);
        }
        Populate();
    }

    void OnLayout(int width, int height) override
    {
        using namespace win32ui::metrics;
        const int margin = Px(kDialogContentMargin);
        const int control = TextPx(kControlHeight);
        const int gap = Px(kOptionGap);
        int top = margin;
        if (group_)
        {
            const int label = win32ui::TextFitWidth(group_label_);
            win32ui::Place(group_label_, margin, top + (control - TextPx(kDetailLineHeight)) / 2, label, TextPx(kDetailLineHeight));
            win32ui::Place(group_, margin + label + gap, top, std::max(Px(240), win32ui::ComboFitWidth(group_)), Px(300));
            top += control + gap;
        }
        const int button_height = TextPx(kButtonHeight);
        const int buttons_top = height - Px(kDialogButtonBottomMargin) - button_height;
        const int text_width = width - 2 * margin;
        const int error_height = TextHeight(errors_, fonts().ui(), text_width);
        const int warning_height = TextHeight(warnings_, fonts().ui(), text_width);
        int bottom = buttons_top - Px(kBlockGap);
        if (warning_height > 0)
        {
            bottom -= warning_height;
            win32ui::Place(warnings_, margin, bottom, text_width, warning_height);
            bottom -= Px(kRowGap);
        }
        SetVisible(warnings_, warning_height > 0);
        if (error_height > 0)
        {
            bottom -= error_height;
            win32ui::Place(errors_, margin, bottom, text_width, error_height);
            bottom -= Px(kRowGap);
        }
        SetVisible(errors_, error_height > 0);
        bottom -= gap;
        win32ui::Place(list_, margin, top, text_width, std::max<int>(Px(60), bottom - top));
        int x = margin;
        for (HWND button : {select_all_, clear_})
        {
            if (button)
            {
                const int button_width = std::max(Px(kButtonMinWidth), win32ui::TextFitWidth(button));
                win32ui::Place(button, x, buttons_top, button_width, button_height);
                x += button_width + Px(kButtonGap);
            }
        }
        win32ui::PlaceButtonRow({ok_, cancel_}, width - Px(kDialogButtonRightMargin), buttons_top, Px(kButtonMinWidth), button_height, Px(kButtonGap));
    }

    bool OnCommand(int id, int code, HWND) override
    {
        switch (id)
        {
        case ids::kPickerGroupCombo:
            if (code == CBN_SELCHANGE)
            {
                const int index = static_cast<int>(SendMessageW(group_, CB_GETCURSEL, 0, 0));
                if (index >= 0)
                {
                    selection_.group = topology_.groups[static_cast<size_t>(index)].index;
                    selection_.mask = 0;
                    Populate();
                }
            }
            return true;
        case ids::kPickerSelectAll:
        case ids::kPickerClear:
            if (code == BN_CLICKED)
            {
                updating_ = true;
                if (mode_ == PickerMode::kSingleProcessor)
                {
                    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED);
                }
                const int count = mode_ == PickerMode::kSingleProcessor ? 0 : ListView_GetItemCount(list_);
                for (int row = 0; row < count; ++row)
                {
                    ListView_SetCheckState(list_, row, id == ids::kPickerSelectAll);
                }
                updating_ = false;
                Collect();
            }
            return true;
        case IDOK:
            if (code == BN_CLICKED || code == 0)
            {
                Collect();
                if (mode_ == PickerMode::kSingleProcessor && !selection_.has_processor && !options_.allow_none)
                {
                    return true;
                }
                End(IDOK);
            }
            return true;
        default:
            return false;
        }
    }

    LRESULT OnNotify(NMHDR* header, bool* handled) override
    {
        *handled = false;
        if (header->hwndFrom != list_)
        {
            return 0;
        }
        if (header->code == LVN_ITEMCHANGED && !updating_)
        {
            const auto* change = reinterpret_cast<const NMLISTVIEW*>(header);
            const UINT relevant = mode_ == PickerMode::kSingleProcessor ? LVIS_SELECTED : mode_ == PickerMode::kBrowse ? 0
                                                                                                                       : LVIS_STATEIMAGEMASK;
            if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & relevant))
            {
                Collect();
            }
        }
        else if (header->code == LVN_COLUMNCLICK)
        {
            win32ui::SortListViewItems(list_, reinterpret_cast<NMLISTVIEW*>(header)->iSubItem, true, &sort_column_, &sort_ascending_, &sort_earlier_, &PickerDialog::Compare, this);
            *handled = true;
        }
        else if (header->code == NM_DBLCLK && mode_ == PickerMode::kSingleProcessor)
        {
            Collect();
            if (selection_.has_processor)
            {
                End(IDOK);
            }
            *handled = true;
        }
        return 0;
    }

  private:
    static int CALLBACK Compare(LPARAM left, LPARAM right, int column, void* context)
    {
        const auto* self = static_cast<const PickerDialog*>(context);
        const auto& a = self->cells_[static_cast<size_t>(left)];
        const auto& b = self->cells_[static_cast<size_t>(right)];
        if (column < 0 || static_cast<size_t>(column) >= a.size() || static_cast<size_t>(column) >= b.size())
        {
            return 0;
        }
        return CompareCellText(a[static_cast<size_t>(column)], b[static_cast<size_t>(column)]);
    }

    void Columns(std::initializer_list<std::pair<const wchar_t*, int>> columns)
    {
        while (ListView_DeleteColumn(list_, 0))
        {
        }
        int index = 0;
        for (const auto& [title, width] : columns)
        {
            LVCOLUMNW column = {};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<wchar_t*>(title);
            column.cx = Px(width);
            ListView_InsertColumn(list_, index++, &column);
        }
        sort_column_ = -1;
        sort_earlier_.clear();
        win32ui::UpdateListViewSort(list_, -1, true);
    }

    void AddRow(std::vector<std::wstring> cells, size_t source)
    {
        const int row = ListView_GetItemCount(list_);
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(cells.front().c_str());
        item.lParam = static_cast<LPARAM>(cells_.size());
        ListView_InsertItem(list_, &item);
        for (size_t column = 1; column < cells.size(); ++column)
        {
            ListView_SetItemText(list_, row, static_cast<int>(column), const_cast<wchar_t*>(cells[column].c_str()));
        }
        cells_.push_back(std::move(cells));
        source_.push_back(source);
    }

    size_t SourceOf(int row) const
    {
        LPARAM data = 0;
        return win32ui::ListViewItemData(list_, row, &data) ? source_[static_cast<size_t>(data)] : 0;
    }

    std::wstring CpuSetFlags(const platform::CpuSetInfo& set) const
    {
        std::wstring flags;
        auto add = [&](const wchar_t* flag) { flags += (flags.empty() ? L"" : L", ") + std::wstring(flag); };
        if (set.parked)
        {
            add(L"Parked");
        }
        if (set.allocated)
        {
            add(set.allocated_to_target_process ? L"AllocatedToTargetProcess" : L"Allocated");
        }
        if (set.realtime)
        {
            add(L"RealTime");
        }
        if (std::find(topology_.process_default_cpu_set_ids.begin(), topology_.process_default_cpu_set_ids.end(), set.id) != topology_.process_default_cpu_set_ids.end())
        {
            add(L"process default");
        }
        return flags;
    }

    void Populate()
    {
        updating_ = true;
        ListView_DeleteAllItems(list_);
        cells_.clear();
        source_.clear();
        if (mode_ == PickerMode::kCpuSets)
        {
            Columns({{L"CPU Set ID", 90}, {L"Processor", 130}, {L"Core", 50}, {L"Last level cache", 100}, {L"NUMA node", 76}, {L"Efficiency class", 100}, {L"Scheduling class", 104}, {L"Flags", 180}});
            for (size_t index = 0; index < topology_.cpu_sets.size(); ++index)
            {
                const auto& set = topology_.cpu_sets[index];
                AddRow({platform::FormatHex(set.id), platform::DescribeProcessor({set.group, set.logical_processor_index}), std::to_wstring(set.core_index), std::to_wstring(set.last_level_cache_index), std::to_wstring(set.numa_node_index), std::to_wstring(set.efficiency_class), std::to_wstring(set.scheduling_class), CpuSetFlags(set)}, index);
                const bool checked = std::find(selection_.selected_cpu_set_ids.begin(), selection_.selected_cpu_set_ids.end(), set.id) != selection_.selected_cpu_set_ids.end();
                ListView_SetCheckState(list_, ListView_GetItemCount(list_) - 1, checked);
            }
        }
        else
        {
            Columns({{L"Processor", 130}, {L"Core", 50}, {L"SMT", 44}, {L"L2 cache", 64}, {L"L3 cache", 64}, {L"NUMA node", 76}, {L"Efficiency class", 100}, {L"CPU Set ID", 84}});
            for (size_t index = 0; index < topology_.processors.size(); ++index)
            {
                const auto& processor = topology_.processors[index];
                if (mode_ == PickerMode::kGroupAffinity && processor.id.group != selection_.group)
                {
                    continue;
                }
                const platform::CoreInfo* core = processor.core_record_index != platform::kUnknownIndex && processor.core_record_index < topology_.cores.size() ? &topology_.cores[processor.core_record_index] : nullptr;
                AddRow({platform::DescribeProcessor(processor.id), IndexText(processor.core_record_index), core ? (core->smt ? L"Yes" : L"No") : L"", IndexText(processor.l2_cache_record_index), IndexText(processor.l3_cache_record_index), IndexText(processor.numa_node_number), core ? std::to_wstring(core->efficiency_class) : L"", processor.has_cpu_set ? platform::FormatHex(processor.cpu_set_id) : L""}, index);
                const int row = ListView_GetItemCount(list_) - 1;
                if (mode_ == PickerMode::kGroupAffinity)
                {
                    ListView_SetCheckState(list_, row, (selection_.mask & (1ull << processor.id.number)) != 0);
                }
                else if (mode_ == PickerMode::kSingleProcessor && selection_.has_processor && processor.id == selection_.processor)
                {
                    ListView_SetItemState(list_, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(list_, row, FALSE);
                }
            }
        }
        const std::vector<win32ui::ListColumn> columns = win32ui::ListColumns(list_);
        for (size_t column = 0; column < columns.size(); ++column)
        {
            if (columns[column].visible)
            {
                win32ui::FitListColumn(list_, static_cast<int>(column));
            }
        }
        updating_ = false;
        Collect();
    }

    void Collect()
    {
        const int count = ListView_GetItemCount(list_);
        if (mode_ == PickerMode::kGroupAffinity)
        {
            uint64_t mask = 0;
            for (int row = 0; row < count; ++row)
            {
                if (ListView_GetCheckState(list_, row))
                {
                    mask |= 1ull << topology_.processors[SourceOf(row)].id.number;
                }
            }
            selection_.mask = mask;
        }
        else if (mode_ == PickerMode::kCpuSets)
        {
            selection_.selected_cpu_set_ids.clear();
            for (int row = 0; row < count; ++row)
            {
                if (ListView_GetCheckState(list_, row))
                {
                    selection_.selected_cpu_set_ids.push_back(topology_.cpu_sets[SourceOf(row)].id);
                }
            }
            std::sort(selection_.selected_cpu_set_ids.begin(), selection_.selected_cpu_set_ids.end());
        }
        else if (mode_ == PickerMode::kSingleProcessor)
        {
            const int row = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
            selection_.has_processor = row >= 0;
            if (row >= 0)
            {
                selection_.processor = topology_.processors[SourceOf(row)].id;
            }
            if (ok_)
            {
                EnableWindow(ok_, row >= 0 || options_.allow_none);
            }
        }
        std::wstring errors;
        std::wstring warnings;
        if (options_.validate && mode_ != PickerMode::kBrowse)
        {
            for (const PickerMessage& message : options_.validate(selection_))
            {
                std::wstring& target = message.error ? errors : warnings;
                target += (target.empty() ? L"" : L"\n") + message.text;
            }
        }
        const bool changed = GetText(errors_) != errors || GetText(warnings_) != warnings;
        SetTextIfChanged(errors_, errors);
        SetTextIfChanged(warnings_, warnings);
        if (changed)
        {
            Relayout();
        }
    }

    PickerOptions options_;
    PickerMode mode_;
    const platform::Topology& topology_;
    PickerSelection selection_;
    HWND group_label_ = nullptr;
    HWND group_ = nullptr;
    HWND list_ = nullptr;
    HWND errors_ = nullptr;
    HWND warnings_ = nullptr;
    HWND select_all_ = nullptr;
    HWND clear_ = nullptr;
    HWND ok_ = nullptr;
    HWND cancel_ = nullptr;
    bool updating_ = false;
    std::vector<std::vector<std::wstring>> cells_;
    std::vector<size_t> source_;
    int sort_column_ = -1;
    bool sort_ascending_ = true;
    win32ui::SortKeys sort_earlier_;
};

} // namespace

#if defined(THREAD_SIM_DEV)
namespace
{
uint16_t g_dev_groups = 0;
}

void SetDevProcessorGroups(uint16_t count)
{
    g_dev_groups = count;
}
#endif

std::optional<PickerSelection> ShowProcessorPicker(HWND owner, const PickerOptions& options, const platform::Topology& topology, const PickerSelection& initial)
{
#if defined(THREAD_SIM_DEV)
    platform::Topology shown = topology;
    for (uint16_t group = static_cast<uint16_t>(topology.groups.size()); group < g_dev_groups && !topology.groups.empty(); ++group)
    {
        platform::ProcessorGroupInfo copy = topology.groups.front();
        copy.index = group;
        shown.groups.push_back(copy);
        for (const platform::LogicalProcessorInfo& processor : topology.processors)
        {
            if (processor.id.group == topology.groups.front().index)
            {
                platform::LogicalProcessorInfo added = processor;
                added.id.group = group;
                shown.processors.push_back(added);
            }
        }
    }
    PickerDialog dialog(options, shown, initial);
#else
    PickerDialog dialog(options, topology, initial);
#endif
    if (dialog.RunModal(owner, MAKEINTRESOURCEW(IDD_PROCESSOR_PICKER)) != IDOK || options.mode == PickerMode::kBrowse)
    {
        return std::nullopt;
    }
    return dialog.selection();
}

} // namespace thread_sim::frame

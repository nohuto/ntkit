// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/activity_dialog.h"

#include "frame/control_ids.h"

#include "ui/controls.h"
#include "ui/dialog.h"
#include "ui/theme.h"

#include "engine/validation.h"

#include "platform/text.h"

#include "resource.h"

#include <win32ui/dialog/combo_box.h>
#include <win32ui/dialog/dialog_layout.h>

#include <algorithm>
#include <string>

namespace thread_sim::frame
{

using namespace ui;

namespace
{

static_assert(IDC_ACTIVITY_WORK_EDIT == ids::kActivityWorkEdit && IDC_ACTIVITY_PERIOD_EDIT == ids::kActivityPeriodEdit && IDC_ACTIVITY_TIMER_COMBO == ids::kActivityTimerCombo);

class ActivityDialog final : public win32ui::DialogWindow
{
  public:
    ActivityDialog(const engine::RunConfig& config, int focused, engine::Field focus, std::wstring title)
        : activity_(config.threads[static_cast<size_t>(focused)].activity), focus_(focus), title_(std::move(title))
    {
    }

    const engine::ActivityConfig& activity() const
    {
        return activity_;
    }

  protected:
    HWND initial_focus() const override
    {
        return FieldControl(focus_);
    }

    void OnCreate() override
    {
        SetWindowTextW(hwnd(), title_.c_str());
        work_ = GetDlgItem(hwnd(), ids::kActivityWorkEdit);
        period_label_ = GetDlgItem(hwnd(), IDC_ACTIVITY_PERIOD_LABEL);
        period_ = GetDlgItem(hwnd(), ids::kActivityPeriodEdit);
        timer_label_ = GetDlgItem(hwnd(), IDC_ACTIVITY_TIMER_LABEL);
        timer_ = GetDlgItem(hwnd(), ids::kActivityTimerCombo);
        SetAccessibleName(work_, L"Work percent of each period");
        SetAccessibleName(period_, L"Period milliseconds");
        SetAccessibleName(timer_, L"Waitable timer kind");
        for (const auto kind : {engine::WaitableTimerKind::kDefault, engine::WaitableTimerKind::kHighResolution})
        {
            SendMessageW(timer_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(engine::WaitableTimerKindLabel(kind)));
        }
        // engine returns item text, combo fits the widest item
        RECT combo = {};
        GetWindowRect(timer_, &combo);
        SetWindowPos(timer_, nullptr, 0, 0, std::max<int>(combo.right - combo.left, win32ui::ComboFitWidth(timer_)), combo.bottom - combo.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        loading_ = true;
        SetTextIfChanged(work_, std::to_wstring(activity_.work_percent));
        SetTextIfChanged(period_, std::to_wstring(activity_.period_ms));
        SetComboIndex(timer_, static_cast<int>(activity_.waitable_timer_kind));
        loading_ = false;
        UpdateRelevance();
    }

    void OnLayout(int, int) override
    {
        win32ui::MatchComboHeights(work_, {timer_});
    }

    bool OnCommand(int id, int code, HWND control) override
    {
        if (loading_)
        {
            return false;
        }
        switch (id)
        {
        case ids::kActivityWorkEdit:
            return Number(code, control, &activity_.work_percent, engine::kMinWorkPercent, engine::kMaxWorkPercent);
        case ids::kActivityPeriodEdit:
            return Number(code, control, &activity_.period_ms, engine::kMinPeriodMs, engine::kMaxPeriodMs);
        case ids::kActivityTimerCombo:
            if (code == CBN_SELCHANGE && ComboIndex(control) >= 0)
            {
                activity_.waitable_timer_kind = static_cast<engine::WaitableTimerKind>(ComboIndex(control));
            }
            return true;
        case IDOK:
            if (code == BN_CLICKED || code == 0)
            {
                // Enter leaves focus in the field, so clamp here too
                Number(EN_KILLFOCUS, work_, &activity_.work_percent, engine::kMinWorkPercent, engine::kMaxWorkPercent);
                Number(EN_KILLFOCUS, period_, &activity_.period_ms, engine::kMinPeriodMs, engine::kMaxPeriodMs);
                End(IDOK);
            }
            return true;
        default:
            return false;
        }
    }

  private:
    // typed values apply immediately within range, leaving shows the clamped value
    // empty fields restore the last value
    bool Number(int code, HWND control, uint32_t* target, uint32_t minimum, uint32_t maximum)
    {
        if (code != EN_CHANGE && code != EN_KILLFOCUS)
        {
            return true;
        }
        const auto parsed = platform::ParseUnsigned(GetText(control));
        if (parsed)
        {
            *target = static_cast<uint32_t>(std::clamp<uint64_t>(*parsed, minimum, maximum));
        }
        if (code == EN_KILLFOCUS)
        {
            loading_ = true;
            SetTextIfChanged(control, std::to_wstring(*target));
            loading_ = false;
        }
        UpdateRelevance();
        return true;
    }

    // period and timer only apply when waiting between slices
    void UpdateRelevance()
    {
        const bool waits = engine::RelevanceOf(activity_).period;
        for (auto [label, field] : {std::pair{period_label_, period_}, std::pair{timer_label_, timer_}})
        {
            SetEnabled(field, waits);
            SetTextRole(label, waits ? TextRole::normal : TextRole::disabled);
        }
    }

    HWND FieldControl(engine::Field field) const
    {
        switch (field)
        {
        case engine::Field::kWorkPercent:
            return work_;
        case engine::Field::kPeriod:
            return period_;
        case engine::Field::kWaitableTimerKind:
            return timer_;
        default:
            return nullptr;
        }
    }

    engine::ActivityConfig activity_;
    engine::Field focus_;
    std::wstring title_;
    HWND work_ = nullptr;
    HWND period_label_ = nullptr, period_ = nullptr;
    HWND timer_label_ = nullptr, timer_ = nullptr;
    bool loading_ = false;
};

} // namespace

bool ShowActivityDialog(HWND owner, engine::RunConfig* config, const std::vector<uint32_t>& threads, int focused, engine::Field focus)
{
    if (threads.empty() || focused < 0 || static_cast<size_t>(focused) >= config->threads.size())
    {
        return false;
    }
    const std::wstring scope = threads.size() == 1 ? config->threads[static_cast<size_t>(focused)].name : std::to_wstring(threads.size()) + L" threads";
    ActivityDialog dialog(*config, focused, focus, L"Activity of " + scope);
    if (dialog.RunModal(owner, MAKEINTRESOURCEW(IDD_ACTIVITY)) != IDOK)
    {
        return false;
    }
    bool changed = false;
    for (uint32_t thread : threads)
    {
        changed = changed || config->threads[thread].activity != dialog.activity();
        config->threads[thread].activity = dialog.activity();
    }
    return changed;
}

} // namespace thread_sim::frame

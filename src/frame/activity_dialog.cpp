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
#include <tuple>

namespace thread_sim::frame
{

using namespace ui;

namespace
{

static_assert(IDC_ACTIVITY_WORK_EDIT == ids::kActivityWorkEdit && IDC_ACTIVITY_PERIOD_EDIT == ids::kActivityPeriodEdit && IDC_ACTIVITY_TIMER_COMBO == ids::kActivityTimerCombo && IDC_ACTIVITY_DELAY_EDIT == ids::kActivityDelayEdit && IDC_ACTIVITY_WAKE_COMBO == ids::kActivityWakeCombo);

class ActivityDialog final : public win32ui::DialogWindow
{
  public:
    // the period applies to every selected thread when any of them uses the shared mutex
    ActivityDialog(const engine::RunConfig& config, int focused, bool any_shared_mutex, engine::Field focus, std::wstring title)
        : activity_(config.threads[static_cast<size_t>(focused)].activity), uses_shared_mutex_(any_shared_mutex), focus_(focus), title_(std::move(title))
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
        delay_ = GetDlgItem(hwnd(), ids::kActivityDelayEdit);
        wake_label_ = GetDlgItem(hwnd(), IDC_ACTIVITY_WAKE_LABEL);
        wake_ = GetDlgItem(hwnd(), ids::kActivityWakeCombo);
        SetAccessibleName(work_, L"Work percent of each period");
        SetAccessibleName(period_, L"Period milliseconds");
        SetAccessibleName(timer_, L"Waitable timer kind");
        SetAccessibleName(delay_, L"Start delay milliseconds");
        SetAccessibleName(wake_, L"Wake source");
        for (const auto source : {engine::WakeSource::kTimer, engine::WakeSource::kEvent, engine::WakeSource::kWindowMessage})
        {
            SendMessageW(wake_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(engine::WakeSourceLabel(source)));
        }
        for (const auto kind : {engine::WaitableTimerKind::kDefault, engine::WaitableTimerKind::kHighResolution})
        {
            SendMessageW(timer_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(engine::WaitableTimerKindLabel(kind)));
        }
        // engine returns item text, combo fits the widest item
        RECT combo = {};
        GetWindowRect(timer_, &combo);
        SetWindowPos(timer_, nullptr, 0, 0, std::max<int>(combo.right - combo.left, win32ui::ComboFitWidth(timer_)), combo.bottom - combo.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(wake_, nullptr, 0, 0, std::max<int>(combo.right - combo.left, win32ui::ComboFitWidth(timer_)), combo.bottom - combo.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        loading_ = true;
        SetTextIfChanged(work_, std::to_wstring(activity_.work_percent));
        SetTextIfChanged(period_, std::to_wstring(activity_.period_ms));
        SetTextIfChanged(delay_, std::to_wstring(activity_.start_delay_ms));
        SetComboIndex(timer_, static_cast<int>(activity_.waitable_timer_kind));
        SetComboIndex(wake_, static_cast<int>(activity_.wake_source));
        loading_ = false;
        UpdateRelevance();
    }

    void OnLayout(int, int) override
    {
        win32ui::MatchComboHeights(work_, {timer_, wake_});
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
        case ids::kActivityDelayEdit:
            return Number(code, control, &activity_.start_delay_ms, 0, engine::kMaxStartDelayMs);
        case ids::kActivityTimerCombo:
            if (code == CBN_SELCHANGE && ComboIndex(control) >= 0)
            {
                activity_.waitable_timer_kind = static_cast<engine::WaitableTimerKind>(ComboIndex(control));
            }
            return true;
        case ids::kActivityWakeCombo:
            if (code == CBN_SELCHANGE && ComboIndex(control) >= 0)
            {
                activity_.wake_source = static_cast<engine::WakeSource>(ComboIndex(control));
                UpdateRelevance();
            }
            return true;
        case IDOK:
            if (code == BN_CLICKED || code == 0)
            {
                // Enter leaves focus in the field, so clamp here too
                Number(EN_KILLFOCUS, work_, &activity_.work_percent, engine::kMinWorkPercent, engine::kMaxWorkPercent);
                Number(EN_KILLFOCUS, period_, &activity_.period_ms, engine::kMinPeriodMs, engine::kMaxPeriodMs);
                Number(EN_KILLFOCUS, delay_, &activity_.start_delay_ms, 0, engine::kMaxStartDelayMs);
                End(IDOK);
            }
            return true;
        default:
            return false;
        }
    }

  private:
    // typed values apply immediately within range, leaving shows the clamped value
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

    // the period also bounds shared mutex ownership, the timer only applies when waiting between slices
    void UpdateRelevance()
    {
        const engine::ActivityRelevance relevance = engine::RelevanceOf(activity_, uses_shared_mutex_);
        for (auto [label, field, relevant] : {std::tuple{period_label_, period_, relevance.period}, std::tuple{wake_label_, wake_, relevance.wake_source}, std::tuple{timer_label_, timer_, relevance.waitable_timer_kind}})
        {
            SetEnabled(field, relevant);
            SetTextRole(label, relevant ? TextRole::normal : TextRole::disabled);
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
        case engine::Field::kStartDelay:
            return delay_;
        case engine::Field::kWakeSource:
            return wake_;
        default:
            return nullptr;
        }
    }

    engine::ActivityConfig activity_;
    bool uses_shared_mutex_ = false;
    engine::Field focus_;
    std::wstring title_;
    HWND work_ = nullptr;
    HWND period_label_ = nullptr, period_ = nullptr;
    HWND timer_label_ = nullptr, timer_ = nullptr;
    HWND delay_ = nullptr;
    HWND wake_label_ = nullptr, wake_ = nullptr;
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
    const bool any_shared_mutex = std::any_of(threads.begin(), threads.end(), [&](uint32_t thread) { return config->threads[thread].uses_shared_mutex; });
    ActivityDialog dialog(*config, focused, any_shared_mutex, focus, L"Activity of " + scope);
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

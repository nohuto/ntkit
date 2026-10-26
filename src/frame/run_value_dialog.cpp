// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/run_value_dialog.h"

#include "frame/control_ids.h"

#include "ui/controls.h"
#include "ui/dialog.h"
#include "ui/theme.h"

#include "platform/text.h"

#include "resource.h"

#include <algorithm>
#include <string>

namespace thread_sim::frame
{

using namespace ui;

namespace
{

static_assert(IDC_RUN_VALUE_EDIT == ids::kRunValueEdit);

class RunValueDialog final : public win32ui::DialogWindow
{
  public:
    RunValueDialog(const engine::RunConfig& config, engine::Field field)
        : config_(config), field_(field)
    {
    }

    const engine::RunConfig& config() const
    {
        return config_;
    }

  protected:
    HWND initial_focus() const override
    {
        return edit_;
    }

    void OnCreate() override
    {
        edit_ = GetDlgItem(hwnd(), ids::kRunValueEdit);
        SetAccessibleName(edit_, field_ == engine::Field::kRunDuration ? L"Run duration seconds" : L"Sample interval milliseconds");
        loading_ = true;
        SetTextIfChanged(edit_, std::to_wstring(*Value()));
        loading_ = false;
        SendMessageW(edit_, EM_SETSEL, 0, -1);
    }

    void OnLayout(int, int) override
    {
    }

    bool OnCommand(int id, int code, HWND) override
    {
        if (loading_)
        {
            return false;
        }
        switch (id)
        {
        case ids::kRunValueEdit:
            if (code == EN_KILLFOCUS)
            {
                Clamp();
            }
            return true;
        case IDOK:
            if (code == BN_CLICKED || code == 0)
            {
                // Enter leaves focus in the field, so clamp here too
                Clamp();
                End(IDOK);
            }
            return true;
        default:
            return false;
        }
    }

  private:
    uint32_t* Value()
    {
        if (field_ == engine::Field::kRunDuration)
        {
            if (!config_.duration_s)
            {
                config_.duration_s = 30;
            }
            return &*config_.duration_s;
        }
        return &config_.sample_interval_ms;
    }

    // show clamped values, restore last value for an empty field
    void Clamp()
    {
        const bool duration = field_ == engine::Field::kRunDuration;
        const auto parsed = platform::ParseUnsigned(GetText(edit_));
        if (parsed)
        {
            *Value() = static_cast<uint32_t>(std::clamp<uint64_t>(*parsed, duration ? engine::kMinRunDurationS : engine::kMinSampleIntervalMs, duration ? engine::kMaxRunDurationS : engine::kMaxSampleIntervalMs));
        }
        loading_ = true;
        SetTextIfChanged(edit_, std::to_wstring(*Value()));
        loading_ = false;
    }

    engine::RunConfig config_;
    engine::Field field_;
    HWND edit_ = nullptr;
    bool loading_ = false;
};

} // namespace

bool ShowRunValueDialog(HWND owner, engine::RunConfig* config, engine::Field field)
{
    if (field != engine::Field::kRunDuration && field != engine::Field::kSampleInterval)
    {
        return false;
    }
    RunValueDialog dialog(*config, field);
    if (dialog.RunModal(owner, MAKEINTRESOURCEW(field == engine::Field::kRunDuration ? IDD_RUN_DURATION : IDD_UPDATE_SPEED)) != IDOK)
    {
        return false;
    }
    const engine::RunConfig& edited = dialog.config();
    const bool changed = edited.duration_s != config->duration_s || edited.sample_interval_ms != config->sample_interval_ms;
    config->duration_s = edited.duration_s;
    config->sample_interval_ms = edited.sample_interval_ms;
    return changed;
}

} // namespace thread_sim::frame

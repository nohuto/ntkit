// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/run_controller.h"
#include "engine/thread_policy.h"

namespace thread_sim::engine
{

struct SampleMeasurement
{
    size_t threads = 0;
    int64_t sample_ticks = 0;
    int64_t readback_ticks = 0;
};

// test overrides, defaults use normal behavior
struct WorkloadOptions
{
    std::optional<size_t> force_setter_failure_thread;
    std::optional<size_t> force_timer_failure_thread;
    std::optional<size_t> force_suspend_failure_thread;
    std::optional<size_t> force_resume_failure_thread;
    PolicySetting forced_setting = PolicySetting::kRelativePriority;
    bool force_high_resolution_fallback = false;
    bool force_variable_counter = false;
    uint32_t setup_delay_ms = 0;
    std::function<void(const SampleMeasurement&)> sample_observer;
};

std::unique_ptr<Backend> MakeWorkloadBackend(const WorkloadOptions& options = {});

} // namespace thread_sim::engine

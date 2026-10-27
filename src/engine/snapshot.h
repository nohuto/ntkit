// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

// requested settings stay in RunConfig, OS outcomes in AppliedSettings
// unavailable measurements stay empty, never zero

#pragma once

#include "engine/config.h"

#include "platform/environment.h"
#include "platform/error.h"
#include "platform/topology.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace thread_sim::engine
{

enum class RunState : uint8_t
{
    kIdle = 0,
    kStarting,
    kActive,
    kStopping,
    kCompleted,
    kFailed,
};

// app thread lifecycle, separate from kernel thread state
enum class ThreadRunState : uint8_t
{
    kPending = 0,
    kWorking,
    kResting,
    kSuspending,
    kSuspended,
    kStopped,
    kFailed,
};

// _KTHREAD_STATE value reported by SYSTEM_THREAD_INFORMATION.ThreadState
inline constexpr uint32_t kThreadStateWaiting = 5;

// SYSTEM_THREAD_INFORMATION for one thread at sample_qpc
struct ThreadSample
{
    int64_t sample_qpc = 0;
    std::optional<uint32_t> thread_state; // _KTHREAD_STATE
    std::optional<uint32_t> wait_reason;  // _KWAIT_REASON, kept only while Waiting
    std::optional<int32_t> base_priority;
    std::optional<int32_t> current_priority;
    std::optional<uint32_t> context_switches;
    bool previous = false; // thread exited, last available sample
    std::optional<platform::Error> error;
};

struct SettingOutcome
{
    bool attempted = false;
    bool succeeded = false;
    std::optional<platform::Error> error;
};

struct AppliedSettings
{
    std::optional<platform::Error> readback_error;
    SettingOutcome relative_priority;
    std::optional<int32_t> relative_priority_readback; // GetThreadPriority, relative THREAD_PRIORITY_* value
    SettingOutcome priority_boost;
    std::optional<bool> priority_boost_disabled_readback; // GetThreadPriorityBoost, TRUE when boosts are disabled
    SettingOutcome group_affinity;
    SettingOutcome selected_cpu_sets;
    std::optional<platform::GroupAffinity> group_affinity_readback; // GetThreadGroupAffinity
    std::optional<std::vector<uint32_t>> selected_cpu_set_ids_readback;
    SettingOutcome ideal_processor;
    std::optional<ProcessorNumber> ideal_processor_readback;
    SettingOutcome waitable_timer;
    std::optional<WaitableTimerKind> obtained_waitable_timer_kind;
};

struct LatencySummary
{
    uint64_t count = 0;
    int64_t p50_us = 0;
    int64_t p99_us = 0;
    int64_t max_us = 0;
};

struct RunningPriority
{
    std::array<uint64_t, 32> reads{};
    std::array<uint64_t, 32> window_offset_us_sum{};
};

struct ThreadSnapshot
{
    uint32_t index = 0;
    bool active = true;
    uint32_t thread_id = 0; // 0 until created
    ThreadRunState state = ThreadRunState::kPending;
    std::optional<ThreadSample> sample;
    AppliedSettings applied;
    // cycles as a share of the last sample interval, 100 means one continuously busy logical processor
    std::optional<double> cpu_percent_one_logical_processor;
    std::optional<uint64_t> user_time_100ns;
    std::optional<uint64_t> kernel_time_100ns;
    std::optional<uint64_t> cycles_total; // QueryThreadCycleTime raw counter
    std::optional<uint64_t> cycles_delta;
    std::optional<uint64_t> work_batches;   // unavailable until the thread reports
    std::optional<uint32_t> missed_periods; // activities below 100 percent only
    std::optional<uint32_t> overruns;       // ^
    std::optional<LatencySummary> wake_lateness;
    std::optional<LatencySummary> mutex_wait;
    std::optional<RunningPriority> running_priority;
    std::optional<platform::Error> error;
    std::optional<std::wstring> warning;
};

struct ProcessSnapshot
{
    std::optional<double> cpu_percent_one_logical_processor;
    std::optional<uint32_t> thread_count;
    std::optional<int32_t> base_priority;
};

struct OtherThreadSnapshot
{
    uint32_t thread_id = 0;
    std::wstring name;
    std::optional<ThreadSample> sample;
    std::optional<int32_t> relative_priority;
    std::optional<bool> priority_boost_disabled;
    std::optional<platform::GroupAffinity> group_affinity;
    std::optional<ProcessorNumber> ideal_processor;
    std::vector<uint32_t> selected_cpu_set_ids;
    std::optional<double> cpu_percent_one_logical_processor;
    std::optional<uint64_t> cycles_total;
    std::optional<uint64_t> user_time_100ns;
    std::optional<uint64_t> kernel_time_100ns;
    std::optional<platform::Error> error;
};

struct RunSnapshot
{
    uint64_t generation = 0;
    uint64_t sequence = 0;
    RunState state = RunState::kIdle;
    bool final = false;
    std::wstring run_id;
    std::wstring started_utc; // ISO 8601
    std::wstring ended_utc;
    int64_t elapsed_us = 0;
    std::optional<uint32_t> requested_duration_s;
    uint32_t sample_interval_ms = 0;
    std::optional<int64_t> last_sample_interval_us; // measured, may differ from the request
    int64_t qpc_frequency = 0;
    // QueryThreadCycleTime units per second, measured against QPC during the run
    std::optional<double> cycle_counter_hz;
    int64_t origin_qpc = 0;
    int64_t end_qpc = 0;
    std::optional<platform::Error> failure;
    std::vector<ThreadSnapshot> threads;
    std::vector<OtherThreadSnapshot> other_threads;
    std::vector<std::wstring> notices;
    std::optional<ProcessSnapshot> process;
    std::shared_ptr<const platform::Topology> topology;
    std::shared_ptr<const platform::Environment> environment;
};

const wchar_t* RunStateLabel(RunState state);
const wchar_t* ThreadRunStateLabel(ThreadRunState state);
const wchar_t* ThreadRunStateKey(ThreadRunState state);
bool IsTerminal(RunState state);

// _KTHREAD_STATE name, nullptr if no supported build defines it
const wchar_t* ThreadStateName(uint32_t state);
// _KWAIT_REASON name for this Windows build, nullptr if undefined
const wchar_t* WaitReasonName(uint32_t reason, uint32_t build);
// "Running", "Waiting (WrDelayExecution)", "Unknown state (12)", "Waiting (unknown wait reason 43)"
std::wstring DescribeThreadState(const ThreadSample& sample, uint32_t build);
// "Below normal (-1)", unnamed THREAD_PRIORITY_* values show only their number
std::wstring DescribeRelativePriority(int32_t value);
// thread cycles converted using the run counter rate
std::optional<double> CpuSecondsFromCycles(const RunSnapshot& run, const ThreadSnapshot& thread);

} // namespace thread_sim::engine

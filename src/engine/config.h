// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

// requested settings stay separate from applied and observed values in snapshot.h

#pragma once

#include "platform/topology.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thread_sim::engine
{

using platform::ProcessorNumber;

inline constexpr uint32_t kConfigFormatVersion = 4;
inline constexpr size_t kMaxThreads = 256;
inline constexpr size_t kMaxNameLength = 64;
inline constexpr uint32_t kMinRunDurationS = 1;
inline constexpr uint32_t kMaxRunDurationS = 86400;
inline constexpr uint32_t kMinSampleIntervalMs = 100;
inline constexpr uint32_t kMaxSampleIntervalMs = 5000;
inline constexpr uint32_t kMinWorkPercent = 1;
inline constexpr uint32_t kMaxWorkPercent = 100;
inline constexpr uint32_t kMinPeriodMs = 1;
inline constexpr uint32_t kMaxPeriodMs = 10000;

// relative thread priority within the process priority class
enum class RelativeThreadPriority : int8_t
{
    kLowest = -2,
    kBelowNormal = -1,
    kNormal = 0,
    kAboveNormal = 1,
    kHighest = 2,
};

enum class AffinityMode : uint8_t
{
    kDefault = 0,
    kGroupAffinity,
    kCpuSets,
};

enum class WaitableTimerKind : uint8_t
{
    kDefault = 0,
    kHighResolution,
};

enum class ActivityLevel : uint8_t
{
    kLow,
    kMedium,
    kBusy,
    kMaximum,
    kCustom,
};

// periods start at run origin + k * period_ms, work fills work_percent of each period
// waitable timer waits for the next period, 100 percent runs without waits
struct ActivityConfig
{
    uint32_t work_percent = 100;
    uint32_t period_ms = 100;
    WaitableTimerKind waitable_timer_kind = WaitableTimerKind::kDefault;

    bool operator==(const ActivityConfig&) const = default;
};

struct SchedulingConfig
{
    RelativeThreadPriority relative_priority = RelativeThreadPriority::kNormal;
    bool priority_boost_enabled = true; // SetThreadPriorityBoost uses the inverse to disable boosts

    bool operator==(const SchedulingConfig&) const = default;
};

// ideal processor preference is independent of group affinity and CPU Sets
struct ThreadAffinityConfig
{
    AffinityMode mode = AffinityMode::kDefault;
    platform::GroupAffinity group_affinity;
    std::vector<uint32_t> selected_cpu_set_ids; // opaque IDs from GetSystemCpuSetInformation
    bool ideal_processor_enabled = false;
    ProcessorNumber ideal_processor;

    bool operator==(const ThreadAffinityConfig&) const = default;
};

struct ThreadConfig
{
    std::wstring name;
    bool active = true;
    bool uses_shared_mutex = false;
    SchedulingConfig scheduling;
    ThreadAffinityConfig affinity;
    ActivityConfig activity;

    bool operator==(const ThreadConfig&) const = default;
};

struct RunConfig
{
    std::wstring name;
    std::optional<uint32_t> duration_s;
    uint32_t sample_interval_ms = 500;
    std::vector<ThreadConfig> threads;

    bool operator==(const RunConfig&) const = default;
};

// persistence keys are stable format identifiers, not display labels
const wchar_t* RelativeThreadPriorityKey(RelativeThreadPriority value);
const wchar_t* AffinityModeKey(AffinityMode value);
const wchar_t* WaitableTimerKindLabel(WaitableTimerKind value);
const wchar_t* WaitableTimerKindKey(WaitableTimerKind value);
ActivityLevel ActivityLevelOf(const ActivityConfig& activity);
uint32_t ActivityLevelWork(ActivityLevel value);
const wchar_t* ActivityLevelLabel(ActivityLevel value);
const wchar_t* ActivityLevelKey(ActivityLevel value);

bool ParseRelativeThreadPriorityKey(std::wstring_view key, RelativeThreadPriority* out);
bool ParseAffinityModeKey(std::wstring_view key, AffinityMode* out);
bool ParseWaitableTimerKindKey(std::wstring_view key, WaitableTimerKind* out);

inline constexpr RelativeThreadPriority kAllRelativeThreadPriorities[] = {
    RelativeThreadPriority::kLowest,
    RelativeThreadPriority::kBelowNormal,
    RelativeThreadPriority::kNormal,
    RelativeThreadPriority::kAboveNormal,
    RelativeThreadPriority::kHighest,
};

ThreadConfig MakeDefaultThread(size_t index);
RunConfig MakeDefaultRun();
std::wstring NextThreadName(const RunConfig& run);

} // namespace thread_sim::engine

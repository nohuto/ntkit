// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/config.h"

#include "platform/topology.h"
#include "platform/process_settings.h"

#include <string>
#include <vector>

namespace thread_sim::engine
{

enum class IssueScope : uint8_t
{
    kRun = 0,
    kThread,
};

enum class IssueSeverity : uint8_t
{
    kError = 0, // blocks Start
    kWarning,
};

enum class Field : uint16_t
{
    kNone = 0,
    kRunDuration,
    kSampleInterval,
    kThreadCount,
    kThreadActive,
    kThreadName,
    kRelativePriority,
    kGroupAffinityGroup,
    kGroupAffinityMask,
    kSelectedCpuSets,
    kIdealProcessor,
    kPeriod,
    kWorkPercent,
    kWaitableTimerKind,
    kStartDelay,
    kWakeSource,
    kSharedMutex,
};

struct ValidationIssue
{
    IssueSeverity severity = IssueSeverity::kError;
    IssueScope scope = IssueScope::kRun;
    int thread = -1; // index into RunConfig::threads when scope == kThread
    Field field = Field::kNone;
    std::wstring message;
};

struct ValidationResult
{
    std::vector<ValidationIssue> issues;

    bool HasErrors() const noexcept;
    size_t ErrorCount() const noexcept;
    size_t WarningCount() const noexcept;
};

ValidationResult Validate(const RunConfig& run, const platform::Topology& topology, const platform::ProcessSettings& process);

struct ActivityRelevance
{
    bool period = false;
    bool waitable_timer_kind = false;
    bool wake_source = false;
};
ActivityRelevance RelevanceOf(const ActivityConfig& activity, bool uses_shared_mutex);

} // namespace thread_sim::engine

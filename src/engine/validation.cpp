// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/validation.h"

#include "platform/text.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <unordered_map>

namespace thread_sim::engine
{

namespace
{

class Collector
{
  public:
    explicit Collector(ValidationResult* result)
        : result_(result)
    {
    }
    void Run(Field field, std::wstring message, IssueSeverity severity = IssueSeverity::kError)
    {
        result_->issues.push_back({severity, IssueScope::kRun, -1, field, std::move(message)});
    }
    void Thread(int thread, Field field, std::wstring message, IssueSeverity severity = IssueSeverity::kError)
    {
        result_->issues.push_back({severity, IssueScope::kThread, thread, field, std::move(message)});
    }

  private:
    ValidationResult* result_;
};

bool ProcessorActive(const platform::Topology& topology, ProcessorNumber id)
{
    return topology.FindProcessor(id) != nullptr;
}

using CpuSetIndex = std::unordered_map<uint32_t, const platform::CpuSetInfo*>;

void ValidateThreadAffinity(size_t index, const ThreadAffinityConfig& affinity_config, const platform::Topology& topology, const CpuSetIndex& cpu_sets, const platform::ProcessSettings& process, Collector& out)
{
    const int thread = static_cast<int>(index);
    switch (affinity_config.mode)
    {
    case AffinityMode::kDefault:
        break;
    case AffinityMode::kGroupAffinity:
        {
            const auto* group = topology.FindGroup(affinity_config.group_affinity.group);
            if (!group)
            {
                out.Thread(thread, Field::kGroupAffinityGroup, L"Processor group " + std::to_wstring(affinity_config.group_affinity.group) + L" does not exist on this machine.");
                break;
            }
            if (affinity_config.group_affinity.mask == 0)
            {
                out.Thread(thread, Field::kGroupAffinityMask, L"Select at least one logical processor for group affinity.");
                break;
            }
            if (affinity_config.group_affinity.group != process.group || (affinity_config.group_affinity.mask & ~process.affinity) != 0)
            {
                out.Thread(thread, Field::kGroupAffinityMask, L"Outside the process affinity.");
            }
            const uint64_t outside = affinity_config.group_affinity.mask & ~group->active_processor_mask;
            if (outside != 0)
            {
                out.Thread(thread, Field::kGroupAffinityMask, L"Processors {" + platform::FormatMaskList(outside) + L"} are not active in group " + std::to_wstring(group->index) + L".");
            }
            break;
        }
    case AffinityMode::kCpuSets:
        {
            if (!topology.cpu_sets_known)
            {
                out.Thread(thread, Field::kSelectedCpuSets, L"CPU Set information is unavailable on this machine.");
                break;
            }
            if (affinity_config.selected_cpu_set_ids.empty())
            {
                out.Thread(thread, Field::kSelectedCpuSets, L"Select at least one CPU Set. To use process defaults, choose OS default.");
                break;
            }
            std::set<uint32_t> seen;
            for (uint32_t id : affinity_config.selected_cpu_set_ids)
            {
                const auto found = cpu_sets.find(id);
                if (found == cpu_sets.end())
                {
                    out.Thread(thread, Field::kSelectedCpuSets, L"CPU Set " + platform::FormatHex(id) + L" does not exist on this machine (stale selection).");
                    continue;
                }
                const auto* set = found->second;
                if (!seen.insert(id).second)
                {
                    out.Thread(thread, Field::kSelectedCpuSets, L"CPU Set " + platform::FormatHex(id) + L" is selected twice.");
                }
                if (set->allocated && !set->allocated_to_target_process)
                {
                    out.Thread(thread, Field::kSelectedCpuSets, L"CPU Set " + platform::FormatHex(id) + L" is reserved for another process. Actual placement may differ.", IssueSeverity::kWarning);
                }
                if (set->parked)
                {
                    out.Thread(thread, Field::kSelectedCpuSets, L"CPU Set " + platform::FormatHex(id) + L" is currently parked.", IssueSeverity::kWarning);
                }
            }
            break;
        }
    }

    if (affinity_config.ideal_processor_enabled)
    {
        const auto* group = topology.FindGroup(affinity_config.ideal_processor.group);
        if (!group)
        {
            out.Thread(thread, Field::kIdealProcessor, L"Ideal processor group " + std::to_wstring(affinity_config.ideal_processor.group) + L" does not exist.");
        }
        else if (!ProcessorActive(topology, affinity_config.ideal_processor))
        {
            out.Thread(thread, Field::kIdealProcessor, L"Ideal processor " + platform::FormatProcessorNumber(affinity_config.ideal_processor) + L" is not an active logical processor.");
        }
        else if (affinity_config.mode == AffinityMode::kGroupAffinity &&
                 (affinity_config.ideal_processor.group != affinity_config.group_affinity.group || (affinity_config.group_affinity.mask & (1ull << affinity_config.ideal_processor.number)) == 0))
        {
            out.Thread(thread, Field::kIdealProcessor, L"The ideal processor is outside the group affinity mask. This combination is contradictory.");
        }
        else if (affinity_config.mode == AffinityMode::kCpuSets)
        {
            const auto* processor = topology.FindProcessor(affinity_config.ideal_processor);
            bool covered = false;
            for (uint32_t id : affinity_config.selected_cpu_set_ids)
            {
                covered = covered || (processor && processor->has_cpu_set && processor->cpu_set_id == id);
            }
            if (!covered)
            {
                out.Thread(thread, Field::kIdealProcessor, L"The ideal processor is outside the selected CPU Sets. This combination is contradictory.");
            }
        }
    }
}

void ValidateActivity(size_t index, const ActivityConfig& activity, Collector& out)
{
    const int thread = static_cast<int>(index);
    if (activity.work_percent < kMinWorkPercent || activity.work_percent > kMaxWorkPercent)
    {
        out.Thread(thread, Field::kWorkPercent, L"Work must be between " + std::to_wstring(kMinWorkPercent) + L" and " + std::to_wstring(kMaxWorkPercent) + L" percent.");
    }
    if (activity.period_ms < kMinPeriodMs || activity.period_ms > kMaxPeriodMs)
    {
        out.Thread(thread, Field::kPeriod, L"Period must be between " + std::to_wstring(kMinPeriodMs) + L" and " + std::to_wstring(kMaxPeriodMs) + L" ms.");
    }
    if (activity.waitable_timer_kind != WaitableTimerKind::kDefault && activity.waitable_timer_kind != WaitableTimerKind::kHighResolution)
    {
        out.Thread(thread, Field::kWaitableTimerKind, L"Select a valid waitable timer kind.");
    }
    if (activity.start_delay_ms > kMaxStartDelayMs)
    {
        out.Thread(thread, Field::kStartDelay, L"Start delay must be between 0 and " + std::to_wstring(kMaxStartDelayMs) + L" ms.");
    }
    if (activity.wake_source != WakeSource::kTimer && activity.wake_source != WakeSource::kEvent && activity.wake_source != WakeSource::kWindowMessage)
    {
        out.Thread(thread, Field::kWakeSource, L"Select a valid wake source.");
    }
}

} // namespace

bool ValidationResult::HasErrors() const noexcept
{
    return ErrorCount() > 0;
}

size_t ValidationResult::ErrorCount() const noexcept
{
    size_t count = 0;
    for (const auto& issue : issues)
    {
        count += issue.severity == IssueSeverity::kError ? 1 : 0;
    }
    return count;
}

size_t ValidationResult::WarningCount() const noexcept
{
    return issues.size() - ErrorCount();
}

ActivityRelevance RelevanceOf(const ActivityConfig& activity, bool uses_shared_mutex)
{
    return {activity.work_percent < 100 || uses_shared_mutex, activity.work_percent < 100 && activity.wake_source == WakeSource::kTimer, activity.work_percent < 100};
}

ValidationResult Validate(const RunConfig& run, const platform::Topology& topology, const platform::ProcessSettings& process)
{
    ValidationResult result;
    Collector out(&result);

    if (run.duration_s && (*run.duration_s < kMinRunDurationS || *run.duration_s > kMaxRunDurationS))
    {
        out.Run(Field::kRunDuration, L"Run duration must be between 1 s and 24 h.");
    }
    if (run.sample_interval_ms < kMinSampleIntervalMs || run.sample_interval_ms > kMaxSampleIntervalMs)
    {
        out.Run(Field::kSampleInterval, L"Sample interval must be between " + std::to_wstring(kMinSampleIntervalMs) + L" and " + std::to_wstring(kMaxSampleIntervalMs) + L" ms.");
    }
    if (std::none_of(run.threads.begin(), run.threads.end(), [](const ThreadConfig& thread) { return thread.active; }))
    {
        out.Run(Field::kThreadActive, L"No thread is active.");
    }
    if (run.threads.empty())
    {
        out.Run(Field::kThreadCount, L"Add at least one thread.");
    }
    if (run.threads.size() > kMaxThreads)
    {
        out.Run(Field::kThreadCount, L"At most " + std::to_wstring(kMaxThreads) + L" threads are supported.");
    }

    if (std::count_if(run.threads.begin(), run.threads.end(), [](const ThreadConfig& thread) { return thread.active && thread.uses_shared_mutex; }) == 1)
    {
        out.Run(Field::kSharedMutex, L"Only one active thread uses the shared mutex, so it never waits for it.", IssueSeverity::kWarning);
    }

    CpuSetIndex cpu_sets;
    if (topology.cpu_sets_known && std::any_of(run.threads.begin(), run.threads.end(), [](const ThreadConfig& thread) { return thread.affinity.mode == AffinityMode::kCpuSets; }))
    {
        cpu_sets.reserve(topology.cpu_sets.size());
        for (const auto& set : topology.cpu_sets)
        {
            cpu_sets.emplace(set.id, &set);
        }
    }
    std::set<std::wstring> names;
    for (size_t index = 0; index < run.threads.size() && index < kMaxThreads; ++index)
    {
        const auto& thread = run.threads[index];
        const int number = static_cast<int>(index);
        const std::wstring name = platform::Trim(thread.name);
        if (name.empty())
        {
            out.Thread(number, Field::kThreadName, L"Enter a thread name.");
        }
        else if (thread.name.size() > kMaxNameLength)
        {
            out.Thread(number, Field::kThreadName, L"Thread names are limited to " + std::to_wstring(kMaxNameLength) + L" characters.");
        }
        else if (std::any_of(thread.name.begin(), thread.name.end(), [](wchar_t ch) { return ch < 0x20; }))
        {
            out.Thread(number, Field::kThreadName, L"Thread names cannot contain control characters.");
        }
        else if (!names.insert(name).second)
        {
            out.Thread(number, Field::kThreadName, L"Another thread already uses this name. Exported results would be ambiguous.", IssueSeverity::kWarning);
        }
        ValidateThreadAffinity(index, thread.affinity, topology, cpu_sets, process, out);
        if (std::find(std::begin(kAllRelativeThreadPriorities), std::end(kAllRelativeThreadPriorities), thread.scheduling.relative_priority) == std::end(kAllRelativeThreadPriorities))
        {
            out.Thread(number, Field::kRelativePriority, L"Select a valid relative thread priority.");
        }
        ValidateActivity(index, thread.activity, out);
    }
    return result;
}

} // namespace thread_sim::engine

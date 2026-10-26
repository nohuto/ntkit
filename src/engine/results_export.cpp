// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/results_export.h"

#include "engine/config_io.h"
#include "engine/validation.h"

#include "platform/file_io.h"
#include "platform/text.h"

#include <win32ui/json/writer.h>

namespace thread_sim::engine
{

namespace
{

using platform::FormatFixed;
using platform::FormatUnsigned;

// column names and units are stable, empty fields mean unavailable
constexpr const wchar_t* kColumns[] = {
    L"schema",
    L"run_id",
    L"requested_duration_s",
    L"thread_index",
    L"thread_name",
    L"active",
    L"thread_id",
    L"final_thread_state",
    L"thread_state",
    L"thread_state_name",
    L"wait_reason",
    L"wait_reason_name",
    L"base_priority",
    L"current_priority",
    L"context_switches",
    L"sample_is_previous",
    L"sample_error",
    L"requested_relative_priority",
    L"requested_relative_priority_value",
    L"relative_priority_set_status",
    L"relative_priority_readback_value",
    L"requested_priority_boost_enabled",
    L"priority_boost_set_status",
    L"priority_boost_disabled_readback",
    L"requested_affinity_mode",
    L"requested_group_affinity_group",
    L"requested_group_affinity_mask",
    L"requested_group_affinity_processors",
    L"requested_selected_cpu_set_ids",
    L"group_affinity_set_status",
    L"selected_cpu_sets_set_status",
    L"group_affinity_readback_group",
    L"group_affinity_readback_mask",
    L"group_affinity_readback_processors",
    L"selected_cpu_set_ids_readback",
    L"requested_ideal_processor_group",
    L"requested_ideal_processor_number",
    L"ideal_processor_set_status",
    L"ideal_processor_readback_group",
    L"ideal_processor_readback_number",
    L"requested_activity_level",
    L"requested_work_percent",
    L"requested_period_ms",
    L"requested_waitable_timer_kind",
    L"obtained_waitable_timer_kind",
    L"cpu_user_100ns",
    L"cpu_kernel_100ns",
    L"cpu_user_s",
    L"cpu_kernel_s",
    L"cpu_s_from_cycles",
    L"cpu_percent_one_logical_processor_last_interval",
    L"cycles_total",
    L"cycles_last_interval",
    L"work_batches",
    L"missed_periods",
    L"overruns",
    L"wake_lateness_count",
    L"wake_lateness_p50_us",
    L"wake_lateness_p99_us",
    L"wake_lateness_max_us",
    L"requested_shared_mutex",
    L"mutex_wait_count",
    L"mutex_wait_p50_us",
    L"mutex_wait_p99_us",
    L"mutex_wait_max_us",
    L"running_priority_reads",
    L"running_priority_min",
    L"running_priority_max",
    L"running_priority_levels",
    L"readback_error",
    L"warning",
    L"error",
};

struct PrioritySummary
{
    std::optional<uint64_t> reads;
    std::optional<uint32_t> min;
    std::optional<uint32_t> max;
    std::wstring levels;
};

PrioritySummary SummarizePriority(const std::optional<RunningPriority>& priority)
{
    PrioritySummary result;
    if (!priority)
    {
        return result;
    }
    for (uint32_t level = 0; level < priority->reads.size(); ++level)
    {
        const uint64_t reads = priority->reads[level];
        if (reads)
        {
            result.reads = result.reads.value_or(0) + reads;
            if (!result.min)
            {
                result.min = level;
            }
            result.max = level;
            result.levels += (result.levels.empty() ? L"" : L" ") + std::to_wstring(level) + L":" + std::to_wstring(reads);
        }
    }
    return result;
}

std::wstring OkText(const SettingOutcome& outcome)
{
    if (!outcome.attempted)
    {
        return L"not_attempted";
    }
    return outcome.succeeded ? L"ok" : L"failed";
}

std::wstring Ids(const std::vector<uint32_t>& ids)
{
    std::wstring text;
    for (size_t index = 0; index < ids.size(); ++index)
    {
        text += (index ? L" " : L"") + FormatUnsigned(ids[index]);
    }
    return text;
}

std::wstring Seconds100ns(const std::optional<uint64_t>& value)
{
    return value ? FormatFixed(static_cast<double>(*value) / 10'000'000.0, 6) : L"";
}

template <typename T>
std::wstring Number(const std::optional<T>& value)
{
    return value ? std::to_wstring(*value) : L"";
}

std::wstring Mask(uint64_t mask)
{
    return platform::FormatHex(mask, 16);
}

void WriteOptional(win32ui::JsonWriter& writer, std::wstring_view key, const std::optional<uint32_t>& value)
{
    value ? writer.Unsigned(key, *value) : writer.Null(key);
}

void WriteOptional(win32ui::JsonWriter& writer, std::wstring_view key, const std::optional<int32_t>& value)
{
    value ? writer.Signed(key, *value) : writer.Null(key);
}

void WriteOptional(win32ui::JsonWriter& writer, std::wstring_view key, const std::optional<uint64_t>& value)
{
    value ? writer.Unsigned(key, *value) : writer.Null(key);
}

const wchar_t* StateKey(RunState state)
{
    switch (state)
    {
    case RunState::kIdle:
        return L"idle";
    case RunState::kStarting:
        return L"starting";
    case RunState::kActive:
        return L"active";
    case RunState::kStopping:
        return L"stopping";
    case RunState::kCompleted:
        return L"completed";
    case RunState::kFailed:
        return L"failed";
    }
    return L"unknown";
}

void WriteError(win32ui::JsonWriter& writer, std::wstring_view key, const std::optional<platform::Error>& error);

void WriteOutcome(win32ui::JsonWriter& writer, std::wstring_view key, const SettingOutcome& outcome)
{
    writer.BeginObject(key);
    writer.Bool(L"attempted", outcome.attempted);
    writer.Bool(L"succeeded", outcome.succeeded);
    WriteError(writer, L"error", outcome.error);
    writer.EndObject();
}

void WriteError(win32ui::JsonWriter& writer, std::wstring_view key, const std::optional<platform::Error>& error)
{
    if (!error)
    {
        writer.Null(key);
        return;
    }
    writer.BeginObject(key);
    writer.String(L"operation", error->operation);
    writer.String(L"context", error->context);
    writer.String(L"code", error->CodeText());
    writer.String(L"description", error->Describe());
    writer.EndObject();
}

} // namespace

std::wstring CsvQuote(std::wstring_view field)
{
    const bool needs_quotes = field.find_first_of(L",\"\r\n") != std::wstring_view::npos || (!field.empty() && (field.front() == L' ' || field.back() == L' '));
    if (!needs_quotes)
    {
        return std::wstring(field);
    }
    std::wstring quoted = L"\"";
    for (wchar_t ch : field)
    {
        if (ch == L'"')
        {
            quoted += L"\"\"";
        }
        else
        {
            quoted.push_back(ch);
        }
    }
    quoted += L"\"";
    return quoted;
}

std::wstring BuildThreadResultsCsv(const ExportInput& input)
{
    std::wstring csv;
    for (size_t index = 0; index < std::size(kColumns); ++index)
    {
        csv += (index ? L"," : L"") + std::wstring(kColumns[index]);
    }
    csv += L"\r\n";
    if (!input.config || !input.snapshot)
    {
        return csv;
    }
    const RunConfig& config = *input.config;
    const RunSnapshot& snapshot = *input.snapshot;
    const uint32_t build = snapshot.environment ? snapshot.environment->windows_build : input.environment.windows_build;
    for (size_t index = 0; index < config.threads.size(); ++index)
    {
        const ThreadConfig& thread = config.threads[index];
        const ThreadSnapshot* observed = thread.active && index < snapshot.threads.size() && snapshot.threads[index].active ? &snapshot.threads[index] : nullptr;
        const ThreadAffinityConfig& affinity_config = thread.affinity;
        const ActivityRelevance relevance = RelevanceOf(thread.activity);
        std::vector<std::wstring> row;
        row.push_back(kResultsCsvSchema);
        row.push_back(snapshot.run_id);
        row.push_back(Number(snapshot.requested_duration_s));
        row.push_back(FormatUnsigned(index));
        row.push_back(thread.name);
        row.push_back(thread.active ? L"true" : L"false");
        const ThreadSample* sample = observed && observed->sample ? &*observed->sample : nullptr;
        const AppliedSettings* applied = observed ? &observed->applied : nullptr;
        const wchar_t* state_name = sample && sample->thread_state ? ThreadStateName(*sample->thread_state) : nullptr;
        const wchar_t* reason_name = sample && sample->wait_reason ? WaitReasonName(*sample->wait_reason, build) : nullptr;
        row.push_back(observed && observed->thread_id ? FormatUnsigned(observed->thread_id) : L"");
        row.push_back(observed ? ThreadRunStateKey(observed->state) : L"");
        row.push_back(sample ? Number(sample->thread_state) : L"");
        row.push_back(state_name ? state_name : L"");
        row.push_back(sample ? Number(sample->wait_reason) : L"");
        row.push_back(reason_name ? reason_name : L"");
        row.push_back(sample ? Number(sample->base_priority) : L"");
        row.push_back(sample ? Number(sample->current_priority) : L"");
        row.push_back(sample ? Number(sample->context_switches) : L"");
        row.push_back(sample ? (sample->previous ? L"true" : L"false") : L"");
        row.push_back(sample && sample->error ? sample->error->Describe() : L"");
        row.push_back(RelativeThreadPriorityKey(thread.scheduling.relative_priority));
        row.push_back(std::to_wstring(static_cast<int32_t>(thread.scheduling.relative_priority)));
        row.push_back(applied ? OkText(applied->relative_priority) : L"");
        row.push_back(applied ? Number(applied->relative_priority_readback) : L"");
        row.push_back(thread.scheduling.priority_boost_enabled ? L"true" : L"false");
        row.push_back(applied ? OkText(applied->priority_boost) : L"");
        row.push_back(applied && applied->priority_boost_disabled_readback ? (*applied->priority_boost_disabled_readback ? L"true" : L"false") : L"");
        row.push_back(AffinityModeKey(affinity_config.mode));
        const bool affinity = affinity_config.mode == AffinityMode::kGroupAffinity;
        row.push_back(affinity ? FormatUnsigned(affinity_config.group_affinity.group) : L"");
        row.push_back(affinity ? Mask(affinity_config.group_affinity.mask) : L"");
        row.push_back(affinity ? platform::FormatMaskList(affinity_config.group_affinity.mask) : L"");
        row.push_back(affinity_config.mode == AffinityMode::kCpuSets ? Ids(affinity_config.selected_cpu_set_ids) : L"");
        row.push_back(applied ? OkText(applied->group_affinity) : L"");
        row.push_back(applied ? OkText(applied->selected_cpu_sets) : L"");
        const bool group_affinity_readback = applied && applied->group_affinity_readback;
        row.push_back(group_affinity_readback ? FormatUnsigned(applied->group_affinity_readback->group) : L"");
        row.push_back(group_affinity_readback ? Mask(applied->group_affinity_readback->mask) : L"");
        row.push_back(group_affinity_readback ? platform::FormatMaskList(applied->group_affinity_readback->mask) : L"");
        row.push_back(applied && applied->selected_cpu_set_ids_readback ? Ids(*applied->selected_cpu_set_ids_readback) : L"");
        row.push_back(affinity_config.ideal_processor_enabled ? FormatUnsigned(affinity_config.ideal_processor.group) : L"");
        row.push_back(affinity_config.ideal_processor_enabled ? FormatUnsigned(affinity_config.ideal_processor.number) : L"");
        row.push_back(applied ? OkText(applied->ideal_processor) : L"");
        row.push_back(applied && applied->ideal_processor_readback ? FormatUnsigned(applied->ideal_processor_readback->group) : L"");
        row.push_back(applied && applied->ideal_processor_readback ? FormatUnsigned(applied->ideal_processor_readback->number) : L"");
        row.push_back(ActivityLevelKey(ActivityLevelOf(thread.activity)));
        row.push_back(FormatUnsigned(thread.activity.work_percent));
        row.push_back(relevance.period ? FormatUnsigned(thread.activity.period_ms) : L"");
        row.push_back(relevance.waitable_timer_kind ? WaitableTimerKindKey(thread.activity.waitable_timer_kind) : L"");
        row.push_back(applied && applied->obtained_waitable_timer_kind ? WaitableTimerKindKey(*applied->obtained_waitable_timer_kind) : L"");
        row.push_back(observed ? Number(observed->user_time_100ns) : L"");
        row.push_back(observed ? Number(observed->kernel_time_100ns) : L"");
        row.push_back(observed ? Seconds100ns(observed->user_time_100ns) : L"");
        row.push_back(observed ? Seconds100ns(observed->kernel_time_100ns) : L"");
        const std::optional<double> cycle_seconds = observed ? CpuSecondsFromCycles(snapshot, *observed) : std::nullopt;
        row.push_back(cycle_seconds ? FormatFixed(*cycle_seconds, 6) : L"");
        row.push_back(observed && observed->cpu_percent_one_logical_processor ? FormatFixed(*observed->cpu_percent_one_logical_processor, 2) : L"");
        row.push_back(observed ? Number(observed->cycles_total) : L"");
        row.push_back(observed ? Number(observed->cycles_delta) : L"");
        row.push_back(observed ? Number(observed->work_batches) : L"");
        row.push_back(observed ? Number(observed->missed_periods) : L"");
        row.push_back(observed ? Number(observed->overruns) : L"");
        row.push_back(observed && observed->wake_lateness ? FormatUnsigned(observed->wake_lateness->count) : L"");
        row.push_back(observed && observed->wake_lateness ? std::to_wstring(observed->wake_lateness->p50_us) : L"");
        row.push_back(observed && observed->wake_lateness ? std::to_wstring(observed->wake_lateness->p99_us) : L"");
        row.push_back(observed && observed->wake_lateness ? std::to_wstring(observed->wake_lateness->max_us) : L"");
        row.push_back(thread.uses_shared_mutex ? L"true" : L"false");
        row.push_back(observed && observed->mutex_wait ? FormatUnsigned(observed->mutex_wait->count) : L"");
        row.push_back(observed && observed->mutex_wait ? std::to_wstring(observed->mutex_wait->p50_us) : L"");
        row.push_back(observed && observed->mutex_wait ? std::to_wstring(observed->mutex_wait->p99_us) : L"");
        row.push_back(observed && observed->mutex_wait ? std::to_wstring(observed->mutex_wait->max_us) : L"");
        const auto priority = SummarizePriority(observed ? observed->running_priority : std::nullopt);
        row.push_back(Number(priority.reads));
        row.push_back(Number(priority.min));
        row.push_back(Number(priority.max));
        row.push_back(priority.levels);
        row.push_back(applied && applied->readback_error ? applied->readback_error->Describe() : L"");
        row.push_back(observed && observed->warning ? *observed->warning : L"");
        row.push_back(observed && observed->error ? observed->error->Describe() : L"");
        for (size_t column = 0; column < row.size(); ++column)
        {
            csv += (column ? L"," : L"") + CsvQuote(row[column]);
        }
        csv += L"\r\n";
    }
    return csv;
}

std::wstring BuildRunMetadataJson(const ExportInput& input)
{
    win32ui::JsonWriter writer;
    writer.BeginObject();
    writer.String(L"format", L"thread-sim.run-metadata");
    writer.Unsigned(L"version", kRunMetadataVersion);
    writer.String(L"app_version", input.app_version);
    if (input.snapshot)
    {
        const RunSnapshot& snapshot = *input.snapshot;
        const uint32_t build = snapshot.environment ? snapshot.environment->windows_build : input.environment.windows_build;
        writer.String(L"coordinator_thread_priority", L"time_critical");
        writer.String(L"run_id", snapshot.run_id);
        writer.String(L"state", StateKey(snapshot.state));
        writer.String(L"started_utc", snapshot.started_utc);
        writer.String(L"ended_utc", snapshot.ended_utc);
        writer.Number(L"elapsed_s", static_cast<double>(snapshot.elapsed_us) / 1e6, 6);
        WriteOptional(writer, L"requested_duration_s", snapshot.requested_duration_s);
        writer.Unsigned(L"requested_sample_interval_ms", snapshot.sample_interval_ms);
        writer.Signed(L"qpc_frequency_hz", snapshot.qpc_frequency);
        if (snapshot.cycle_counter_hz)
        {
            writer.Number(L"cycle_counter_hz", *snapshot.cycle_counter_hz, 0);
        }
        writer.Signed(L"origin_qpc", snapshot.origin_qpc);
        writer.Signed(L"end_qpc", snapshot.end_qpc);
        writer.String(L"clock_note", L"QPC origin is the start gate release. Elapsed times are relative to it. UTC timestamps are wall clock correlation only.");
        WriteError(writer, L"failure", snapshot.failure);
        writer.BeginArray(L"threads");
        for (const auto& thread : snapshot.threads)
        {
            writer.BeginObject();
            writer.Unsigned(L"thread_index", thread.index);
            writer.Bool(L"active", thread.active);
            thread.thread_id ? writer.Unsigned(L"thread_id", thread.thread_id) : writer.Null(L"thread_id");
            writer.String(L"final_thread_state", ThreadRunStateKey(thread.state));
            if (thread.sample)
            {
                const ThreadSample& sample = *thread.sample;
                writer.BeginObject(L"sample");
                writer.Signed(L"sample_qpc", sample.sample_qpc);
                WriteOptional(writer, L"thread_state", sample.thread_state);
                const wchar_t* state_name = sample.thread_state ? ThreadStateName(*sample.thread_state) : nullptr;
                state_name ? writer.String(L"thread_state_name", state_name) : writer.Null(L"thread_state_name");
                WriteOptional(writer, L"wait_reason", sample.wait_reason);
                const wchar_t* reason_name = sample.wait_reason ? WaitReasonName(*sample.wait_reason, build) : nullptr;
                reason_name ? writer.String(L"wait_reason_name", reason_name) : writer.Null(L"wait_reason_name");
                WriteOptional(writer, L"base_priority", sample.base_priority);
                WriteOptional(writer, L"current_priority", sample.current_priority);
                WriteOptional(writer, L"context_switches", sample.context_switches);
                writer.Bool(L"previous", sample.previous);
                WriteError(writer, L"error", sample.error);
                writer.EndObject();
            }
            else
            {
                writer.Null(L"sample");
            }
            writer.BeginObject(L"readback");
            WriteOptional(writer, L"relative_priority_value", thread.applied.relative_priority_readback);
            thread.applied.priority_boost_disabled_readback ? writer.Bool(L"priority_boost_disabled", *thread.applied.priority_boost_disabled_readback) : writer.Null(L"priority_boost_disabled");
            if (thread.applied.group_affinity_readback)
            {
                writer.Unsigned(L"group_affinity_group", thread.applied.group_affinity_readback->group);
                writer.String(L"group_affinity_mask", Mask(thread.applied.group_affinity_readback->mask));
            }
            if (thread.applied.selected_cpu_set_ids_readback)
            {
                writer.BeginArray(L"selected_cpu_set_ids");
                for (uint32_t id : *thread.applied.selected_cpu_set_ids_readback)
                {
                    writer.Unsigned({}, id);
                }
                writer.EndArray();
            }
            if (thread.applied.ideal_processor_readback)
            {
                writer.Unsigned(L"ideal_processor_group", thread.applied.ideal_processor_readback->group);
                writer.Unsigned(L"ideal_processor_number", thread.applied.ideal_processor_readback->number);
            }
            writer.EndObject();
            WriteOptional(writer, L"cpu_user_100ns", thread.user_time_100ns);
            WriteOptional(writer, L"cpu_kernel_100ns", thread.kernel_time_100ns);
            WriteOptional(writer, L"cycles_total", thread.cycles_total);
            WriteOptional(writer, L"work_batches", thread.work_batches);
            if (input.config && thread.index < input.config->threads.size())
            {
                writer.Bool(L"requested_shared_mutex", input.config->threads[thread.index].uses_shared_mutex);
            }
            else
            {
                writer.Null(L"requested_shared_mutex");
            }
            if (thread.mutex_wait)
            {
                writer.Unsigned(L"mutex_wait_count", thread.mutex_wait->count);
                writer.Signed(L"mutex_wait_p50_us", thread.mutex_wait->p50_us);
                writer.Signed(L"mutex_wait_p99_us", thread.mutex_wait->p99_us);
                writer.Signed(L"mutex_wait_max_us", thread.mutex_wait->max_us);
            }
            else
            {
                for (const auto* key : {L"mutex_wait_count", L"mutex_wait_p50_us", L"mutex_wait_p99_us", L"mutex_wait_max_us"})
                {
                    writer.Null(key);
                }
            }
            const auto priority = SummarizePriority(thread.running_priority);
            WriteOptional(writer, L"running_priority_reads", priority.reads);
            WriteOptional(writer, L"running_priority_min", priority.min);
            WriteOptional(writer, L"running_priority_max", priority.max);
            priority.reads ? writer.String(L"running_priority_levels", priority.levels) : writer.Null(L"running_priority_levels");
            writer.BeginObject(L"applied");
            WriteOutcome(writer, L"relative_priority", thread.applied.relative_priority);
            WriteOutcome(writer, L"priority_boost", thread.applied.priority_boost);
            WriteOutcome(writer, L"group_affinity", thread.applied.group_affinity);
            WriteOutcome(writer, L"selected_cpu_sets", thread.applied.selected_cpu_sets);
            WriteOutcome(writer, L"ideal_processor", thread.applied.ideal_processor);
            WriteOutcome(writer, L"waitable_timer", thread.applied.waitable_timer);
            writer.EndObject();
            WriteError(writer, L"readback_error", thread.applied.readback_error);
            thread.warning ? writer.String(L"warning", *thread.warning) : writer.Null(L"warning");
            WriteError(writer, L"error", thread.error);
            writer.EndObject();
        }
        writer.EndArray();
        writer.BeginArray(L"notices");
        for (const auto& notice : snapshot.notices)
        {
            writer.String({}, notice);
        }
        writer.EndArray();
        if (snapshot.process)
        {
            writer.BeginObject(L"process");
            const auto& process = *snapshot.process;
            process.cpu_percent_one_logical_processor ? writer.Number(L"cpu_percent_one_logical_processor", *process.cpu_percent_one_logical_processor, 2) : writer.Null(L"cpu_percent_one_logical_processor");
            WriteOptional(writer, L"thread_count", process.thread_count);
            WriteOptional(writer, L"base_priority", process.base_priority);
            writer.EndObject();
        }
        else
        {
            writer.Null(L"process");
        }
    }
    // use caller values only when Start captured none
    const platform::Environment& environment = input.snapshot && input.snapshot->environment ? *input.snapshot->environment : input.environment;
    const platform::Topology* run_topology = input.snapshot && input.snapshot->topology ? input.snapshot->topology.get() : input.topology;
    writer.BeginObject(L"environment");
    writer.String(L"windows_version", environment.windows_version);
    writer.Unsigned(L"windows_build", environment.windows_build);
    writer.String(L"windows_display_version", environment.windows_display);
    writer.String(L"architecture", environment.architecture);
    writer.String(L"processor_name", environment.processor_name);
    writer.Unsigned(L"process_id", environment.process_id);
    writer.String(L"process_priority_class", environment.priority_class ? platform::PriorityClassKey(*environment.priority_class) : L"unknown");
    if (environment.on_ac_power)
    {
        writer.Bool(L"on_ac_power", *environment.on_ac_power);
    }
    else
    {
        writer.Null(L"on_ac_power");
    }
    if (environment.hypervisor_present_cpuid)
    {
        writer.Bool(L"hypervisor_present_cpuid", *environment.hypervisor_present_cpuid);
    }
    else
    {
        writer.Null(L"hypervisor_present_cpuid");
    }
    writer.String(L"hypervisor_note", L"CPUID hypervisor-present bit, also set on bare metal when virtualization-based security is enabled.");
    if (environment.process_affinity_mask && environment.system_affinity_mask)
    {
        writer.String(L"process_affinity_primary_group", platform::FormatMaskList(*environment.process_affinity_mask));
        writer.String(L"system_affinity_primary_group", platform::FormatMaskList(*environment.system_affinity_mask));
    }
    environment.process_group_count ? writer.Unsigned(L"process_group_count", *environment.process_group_count) : writer.Null(L"process_group_count");
    writer.String(L"computer_name_hash", environment.computer_name_hash);
    writer.EndObject();
    if (run_topology)
    {
        const platform::Topology& topology = *run_topology;
        writer.BeginObject(L"topology");
        writer.String(L"source", topology.source_label);
        writer.BeginArray(L"groups");
        for (const auto& group : topology.groups)
        {
            writer.BeginObject();
            writer.Unsigned(L"group", group.index);
            writer.Unsigned(L"active_processor_count", group.active_processor_count);
            writer.Unsigned(L"maximum_processor_count", group.maximum_processor_count);
            writer.String(L"active_processor_list", platform::FormatMaskList(group.active_processor_mask));
            writer.EndObject();
        }
        writer.EndArray();
        writer.BeginArray(L"logical_processors");
        for (const auto& processor : topology.processors)
        {
            writer.BeginObject();
            writer.Unsigned(L"group", processor.id.group);
            writer.Unsigned(L"number", processor.id.number);
            processor.core_record_index == platform::kUnknownIndex ? writer.Null(L"core_record_index") : writer.Unsigned(L"core_record_index", processor.core_record_index);
            processor.l3_cache_record_index == platform::kUnknownIndex ? writer.Null(L"l3_cache_record_index") : writer.Unsigned(L"l3_cache_record_index", processor.l3_cache_record_index);
            processor.numa_node_number == platform::kUnknownIndex ? writer.Null(L"numa_node_number") : writer.Unsigned(L"numa_node_number", processor.numa_node_number);
            processor.has_cpu_set ? writer.Unsigned(L"cpu_set_id", processor.cpu_set_id) : writer.Null(L"cpu_set_id");
            writer.EndObject();
        }
        writer.EndArray();
        writer.Bool(L"cpu_sets_known", topology.cpu_sets_known);
        writer.BeginArray(L"process_default_cpu_set_ids");
        for (uint32_t id : topology.process_default_cpu_set_ids)
        {
            writer.Unsigned({}, id);
        }
        writer.EndArray();
        writer.Bool(L"process_default_cpu_set_ids_known", topology.process_default_cpu_set_ids_known);
        writer.EndObject();
    }
    if (input.config && run_topology)
    {
        WriteRunConfigObject(writer, L"configuration", *input.config, *run_topology);
    }
    writer.EndObject();
    return writer.Take();
}

platform::Status ExportResults(const std::wstring& base_path, const ExportInput& input)
{
    // UTF-8 BOM lets spreadsheets detect encoding
    const std::string csv = "\xEF\xBB\xBF" + platform::WideToUtf8(BuildThreadResultsCsv(input));
    if (platform::Status status = platform::WriteFileAtomic(base_path + L".csv", csv); !status)
    {
        return status;
    }
    return platform::WriteUtf8FileAtomic(base_path + L".json", BuildRunMetadataJson(input));
}

} // namespace thread_sim::engine

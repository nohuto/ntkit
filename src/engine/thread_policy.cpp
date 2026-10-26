// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/thread_policy.h"

namespace thread_sim::engine
{

std::wstring ThreadContext(size_t index, const ThreadConfig& thread)
{
    return L"thread " + std::to_wstring(index + 1) + L" (" + thread.name + L")";
}

platform::Status ApplyThreadPolicy(HANDLE handle, const ThreadConfig& thread, size_t index, AppliedSettings& applied, std::optional<PolicySetting> force_failure)
{
    const std::wstring context = ThreadContext(index, thread);
    std::optional<platform::Error> failure;
    auto set = [&](PolicySetting setting, SettingOutcome& outcome, const wchar_t* operation, const wchar_t* label, auto setter) {
        outcome.attempted = true;
        const bool forced = force_failure == setting;
        if (forced || !setter())
        {
            outcome.error = (forced ? platform::Win32Error(ERROR_INVALID_PARAMETER, operation) : platform::LastWin32Error(operation)).WithContext(context);
            if (!failure)
            {
                failure = *outcome.error;
                failure->operation = std::wstring(L"Apply thread ") + label;
                failure->detail = L"Required setup failed. The launch aborted and all created threads joined.";
            }
        }
        else
        {
            outcome.succeeded = true;
        }
    };
    const auto& affinity = thread.affinity;
    if (affinity.mode == AffinityMode::kGroupAffinity)
    {
        GROUP_AFFINITY group{};
        group.Group = affinity.group_affinity.group;
        group.Mask = affinity.group_affinity.mask;
        set(PolicySetting::kGroupAffinity, applied.group_affinity, L"SetThreadGroupAffinity", L"group affinity", [&] {
            return SetThreadGroupAffinity(handle, &group, nullptr);
        });
    }
    if (affinity.mode == AffinityMode::kCpuSets)
    {
        const std::vector<ULONG> ids(affinity.selected_cpu_set_ids.begin(), affinity.selected_cpu_set_ids.end());
        set(PolicySetting::kSelectedCpuSets, applied.selected_cpu_sets, L"SetThreadSelectedCpuSets", L"selected CPU Sets", [&] {
            return SetThreadSelectedCpuSets(handle, ids.data(), static_cast<ULONG>(ids.size()));
        });
    }
    if (affinity.ideal_processor_enabled)
    {
        PROCESSOR_NUMBER processor{affinity.ideal_processor.group, affinity.ideal_processor.number, 0};
        set(PolicySetting::kIdealProcessor, applied.ideal_processor, L"SetThreadIdealProcessorEx", L"ideal processor", [&] {
            return SetThreadIdealProcessorEx(handle, &processor, nullptr);
        });
    }
    set(PolicySetting::kRelativePriority, applied.relative_priority, L"SetThreadPriority", L"relative priority", [&] {
        return SetThreadPriority(handle, static_cast<int>(thread.scheduling.relative_priority));
    });
    set(PolicySetting::kPriorityBoost, applied.priority_boost, L"SetThreadPriorityBoost", L"priority boost", [&] {
        return SetThreadPriorityBoost(handle, !thread.scheduling.priority_boost_enabled);
    });

    ReadThreadPolicy(handle, applied);
    return failure ? platform::Status(*failure) : platform::Status::Ok();
}

void ReadThreadPolicy(HANDLE handle, AppliedSettings& applied)
{
    applied.relative_priority_readback.reset();
    applied.priority_boost_disabled_readback.reset();
    applied.group_affinity_readback.reset();
    applied.selected_cpu_set_ids_readback.reset();
    applied.ideal_processor_readback.reset();
    applied.readback_error.reset();
    auto read_error = [&](const wchar_t* operation) {
        const auto error = platform::LastWin32Error(operation);
        if (!applied.readback_error)
        {
            applied.readback_error = error;
        }
    };
    const int priority = GetThreadPriority(handle);
    if (priority != THREAD_PRIORITY_ERROR_RETURN)
    {
        applied.relative_priority_readback = priority;
    }
    else
    {
        read_error(L"GetThreadPriority");
    }
    BOOL disabled = FALSE;
    if (GetThreadPriorityBoost(handle, &disabled))
    {
        applied.priority_boost_disabled_readback = disabled != FALSE;
    }
    else
    {
        read_error(L"GetThreadPriorityBoost");
    }
    GROUP_AFFINITY group{};
    if (GetThreadGroupAffinity(handle, &group))
    {
        applied.group_affinity_readback = platform::GroupAffinity{group.Group, group.Mask};
    }
    else
    {
        read_error(L"GetThreadGroupAffinity");
    }
    ULONG count = 0;
    const BOOL sized = GetThreadSelectedCpuSets(handle, nullptr, 0, &count);
    if (sized || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<ULONG> ids(count);
        if (count == 0 || GetThreadSelectedCpuSets(handle, ids.data(), count, &count))
        {
            ids.resize(count);
            applied.selected_cpu_set_ids_readback = std::vector<uint32_t>(ids.begin(), ids.end());
        }
        else
        {
            read_error(L"GetThreadSelectedCpuSets");
        }
    }
    else
    {
        read_error(L"GetThreadSelectedCpuSets");
    }
    PROCESSOR_NUMBER processor{};
    if (GetThreadIdealProcessorEx(handle, &processor))
    {
        applied.ideal_processor_readback = ProcessorNumber{processor.Group, processor.Number};
    }
    else
    {
        read_error(L"GetThreadIdealProcessorEx");
    }
}

} // namespace thread_sim::engine

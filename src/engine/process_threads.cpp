// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/process_threads.h"

#include "platform/clock.h"
#include "platform/text.h"

#include <algorithm>
#include <array>

namespace thread_sim::engine
{

namespace
{

std::wstring StartName(uintptr_t address)
{
    if (!address)
    {
        return {};
    }
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &module))
    {
        std::array<wchar_t, 32768> path{};
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length && length < path.size())
        {
            const std::wstring_view full(path.data(), length);
            const size_t slash = full.find_last_of(L"\\/");
            return std::wstring(full.substr(slash == std::wstring_view::npos ? 0 : slash + 1)) + L"+" +
                   platform::FormatHex(address - reinterpret_cast<uintptr_t>(module));
        }
    }
    return platform::FormatHex(address);
}

} // namespace

ProcessThreadSampler::ProcessThreadSampler(bool force_variable_counter)
    : invariant_counter_(!force_variable_counter && platform::InvariantTimeStampCounter())
{
}

platform::Result<std::vector<OtherThreadSnapshot>> ProcessThreadSampler::Sample(std::span<const uint32_t> exclude)
{
    std::lock_guard lock(mutex_);
    const auto system = system_query_.QueryCurrentProcess();
    const int64_t now = platform::QpcClock::Now();
    if (!system)
    {
        return system.error();
    }
    return SampleThreads(exclude, system.value(), now);
}

std::vector<OtherThreadSnapshot> ProcessThreadSampler::SampleFromSystemThreads(std::span<const uint32_t> exclude, std::span<const platform::SystemThread> threads, int64_t sample_qpc)
{
    std::lock_guard lock(mutex_);
    return SampleThreads(exclude, threads, sample_qpc);
}

size_t ProcessThreadSampler::cached_handle_count() const
{
    std::lock_guard lock(mutex_);
    return static_cast<size_t>(std::count_if(cache_.begin(), cache_.end(), [](const auto& thread) { return static_cast<bool>(thread.handle); }));
}

int64_t ProcessThreadSampler::last_query_ticks() const
{
    std::lock_guard lock(mutex_);
    return system_query_.last_call_ticks();
}

std::vector<OtherThreadSnapshot> ProcessThreadSampler::SampleThreads(std::span<const uint32_t> exclude, std::span<const platform::SystemThread> threads, int64_t sample_qpc)
{
    const uint64_t tsc = invariant_counter_ ? __rdtsc() : 0;
    const auto excluded = [&](uint32_t id) { return std::find(exclude.begin(), exclude.end(), id) != exclude.end(); };
    std::erase_if(cache_, [&](const auto& cached) {
        return excluded(cached.thread_id) || std::none_of(threads.begin(), threads.end(), [&](const auto& thread) { return thread.thread_id == cached.thread_id; });
    });
    cache_.reserve(threads.size());
    std::vector<OtherThreadSnapshot> result;
    result.reserve(threads.size());
    for (const auto& thread : threads)
    {
        if (excluded(thread.thread_id))
        {
            continue;
        }
        auto found = std::find_if(cache_.begin(), cache_.end(), [&](const auto& cached) { return cached.thread_id == thread.thread_id; });
        if (found == cache_.end())
        {
            cache_.emplace_back();
            found = cache_.end() - 1;
            found->thread_id = thread.thread_id;
        }
        OtherThreadSnapshot snapshot;
        snapshot.thread_id = thread.thread_id;
        auto& sample = snapshot.sample.emplace();
        sample.sample_qpc = sample_qpc;
        sample.thread_state = thread.state;
        if (thread.state == kThreadStateWaiting)
        {
            sample.wait_reason = thread.wait_reason;
        }
        sample.base_priority = thread.base_priority;
        sample.current_priority = thread.priority;
        sample.context_switches = thread.context_switches;
        DWORD exit_code = 0;
        bool checked = false;
        bool known_exited = false;
        if (found->handle)
        {
            if (!GetExitCodeThread(found->handle.get(), &exit_code))
            {
                snapshot.error = platform::LastWin32Error(L"GetExitCodeThread");
                cache_.erase(found);
                result.push_back(std::move(snapshot));
                continue;
            }
            checked = exit_code == STILL_ACTIVE;
            if (!checked)
            {
                // exited handles still identify the old thread if its TID is reused
                known_exited = true;
                *found = CachedThread{};
                found->thread_id = thread.thread_id;
            }
        }
        if (!found->handle)
        {
            found->handle.reset(OpenThread(THREAD_QUERY_INFORMATION, FALSE, thread.thread_id));
        }
        if (!found->handle)
        {
            const DWORD error = GetLastError();
            if (known_exited && error == ERROR_INVALID_PARAMETER)
            {
                cache_.erase(found);
                continue;
            }
            snapshot.error = platform::Win32Error(error, L"OpenThread");
        }
        else if (!checked && !GetExitCodeThread(found->handle.get(), &exit_code))
        {
            snapshot.error = platform::LastWin32Error(L"GetExitCodeThread");
            cache_.erase(found);
        }
        else if (exit_code != STILL_ACTIVE)
        {
            cache_.erase(found);
            continue;
        }
        else
        {
            ReadName(*found, snapshot, sample_qpc);
            ReadThread(*found, snapshot, sample_qpc, tsc);
        }
        result.push_back(std::move(snapshot));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.thread_id < right.thread_id; });
    return result;
}

void ProcessThreadSampler::ReadName(CachedThread& cached, OtherThreadSnapshot& snapshot, int64_t now)
{
    if (!cached.has_description || now - cached.description_qpc >= 3 * platform::QpcClock::Frequency())
    {
        PWSTR description = nullptr;
        const HRESULT hr = GetThreadDescription(cached.handle.get(), &description);
        const std::unique_ptr<wchar_t, decltype(&LocalFree)> owned(description, &LocalFree);
        if (SUCCEEDED(hr))
        {
            cached.has_description = description && *description;
            cached.name = cached.has_description ? description : L"";
        }
        else
        {
            snapshot.error = platform::HResultError(hr, L"GetThreadDescription");
        }
        cached.description_qpc = now;
    }
    if (!cached.has_description)
    {
        if (!cached.queried_start)
        {
            const auto address = platform::QueryThreadStartAddress(cached.handle.get());
            if (address)
            {
                cached.start_name = StartName(address.value());
                cached.queried_start = true;
            }
            else if (!snapshot.error)
            {
                snapshot.error = address.error();
            }
        }
        cached.name = cached.start_name;
    }
    snapshot.name = cached.name;
}

void ProcessThreadSampler::ReadThread(CachedThread& cached, OtherThreadSnapshot& snapshot, int64_t now, uint64_t tsc)
{
    const HANDLE handle = cached.handle.get();
    const auto read_error = [&](const wchar_t* operation) {
        if (!snapshot.error)
        {
            snapshot.error = platform::LastWin32Error(operation);
        }
    };
    const int priority = GetThreadPriority(handle);
    if (priority != THREAD_PRIORITY_ERROR_RETURN)
    {
        snapshot.relative_priority = priority;
    }
    else
    {
        read_error(L"GetThreadPriority");
    }
    BOOL disabled = FALSE;
    if (GetThreadPriorityBoost(handle, &disabled))
    {
        snapshot.priority_boost_disabled = disabled != FALSE;
    }
    else
    {
        read_error(L"GetThreadPriorityBoost");
    }
    GROUP_AFFINITY group{};
    if (GetThreadGroupAffinity(handle, &group))
    {
        snapshot.group_affinity = platform::GroupAffinity{group.Group, group.Mask};
    }
    else
    {
        read_error(L"GetThreadGroupAffinity");
    }
    PROCESSOR_NUMBER processor{};
    if (GetThreadIdealProcessorEx(handle, &processor))
    {
        snapshot.ideal_processor = ProcessorNumber{processor.Group, processor.Number};
    }
    else
    {
        read_error(L"GetThreadIdealProcessorEx");
    }
    ULONG count = 0;
    if (GetThreadSelectedCpuSets(handle, nullptr, 0, &count) || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        cpu_set_ids_.resize(count);
        if (!count || GetThreadSelectedCpuSets(handle, cpu_set_ids_.data(), count, &count))
        {
            snapshot.selected_cpu_set_ids.assign(cpu_set_ids_.begin(), cpu_set_ids_.begin() + count);
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
    ULONG64 cycles = 0;
    if (QueryThreadCycleTime(handle, &cycles))
    {
        snapshot.cycles_total = cycles;
    }
    else
    {
        read_error(L"QueryThreadCycleTime");
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    std::optional<uint64_t> cpu_time;
    if (GetThreadTimes(handle, &created, &exited, &kernel, &user))
    {
        snapshot.kernel_time_100ns = platform::FileTimeTo100ns(kernel);
        snapshot.user_time_100ns = platform::FileTimeTo100ns(user);
        cpu_time = *snapshot.kernel_time_100ns + *snapshot.user_time_100ns;
    }
    else
    {
        read_error(L"GetThreadTimes");
    }
    if (invariant_counter_)
    {
        if (cached.cycles && snapshot.cycles_total && cycles >= *cached.cycles && tsc > cached.sample_tsc)
        {
            snapshot.cpu_percent_one_logical_processor = 100.0 * static_cast<double>(cycles - *cached.cycles) / static_cast<double>(tsc - cached.sample_tsc);
        }
    }
    else if (cached.cpu_time && cpu_time && *cpu_time >= *cached.cpu_time && now > cached.sample_qpc)
    {
        snapshot.cpu_percent_one_logical_processor = 100.0 * static_cast<double>(*cpu_time - *cached.cpu_time) * static_cast<double>(platform::QpcClock::Frequency()) /
                                                     (10'000'000.0 * static_cast<double>(now - cached.sample_qpc));
    }
    cached.cycles = snapshot.cycles_total;
    cached.cpu_time = cpu_time;
    cached.sample_qpc = now;
    cached.sample_tsc = tsc;
}

} // namespace thread_sim::engine

// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "platform/system_threads.h"

#include "platform/clock.h"

#include <win32ui/handle_owner.h>
#include <win32ui/system_api.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace thread_sim::platform
{

namespace
{

struct ClientId
{
    HANDLE process;
    HANDLE thread;
};

struct SystemThreadInformation
{
    LARGE_INTEGER kernel_time;
    LARGE_INTEGER user_time;
    LARGE_INTEGER create_time;
    ULONG wait_time;
    void* start_address;
    ClientId client_id;
    LONG priority;
    LONG base_priority;
    ULONG context_switches;
    ULONG thread_state;
    ULONG wait_reason;
};

struct SystemProcessInformation
{
    ULONG next_entry_offset;
    ULONG number_of_threads;
    std::byte reserved1[64];
    LONG base_priority;
    ULONG padding;
    HANDLE process_id;
    std::byte reserved2[168];
};

struct ThreadBasicInformation
{
    LONG exit_status;
    void* teb;
    ClientId client_id;
    ULONG_PTR affinity;
    LONG priority;
    LONG base_priority;
};

static_assert(sizeof(SystemProcessInformation) == 256);
static_assert(offsetof(SystemProcessInformation, next_entry_offset) == 0);
static_assert(offsetof(SystemProcessInformation, number_of_threads) == 4);
static_assert(offsetof(SystemProcessInformation, process_id) == 80);
static_assert(offsetof(SystemProcessInformation, base_priority) == 72);
static_assert(sizeof(SystemThreadInformation) == 80);
static_assert(offsetof(ClientId, thread) == 8);
static_assert(offsetof(SystemThreadInformation, client_id) == 40);
static_assert(offsetof(SystemThreadInformation, priority) == 56);
static_assert(offsetof(SystemThreadInformation, base_priority) == 60);
static_assert(offsetof(SystemThreadInformation, context_switches) == 64);
static_assert(offsetof(SystemThreadInformation, thread_state) == 68);
static_assert(offsetof(SystemThreadInformation, wait_reason) == 72);
static_assert(sizeof(ThreadBasicInformation) == 48);
static_assert(offsetof(ThreadBasicInformation, priority) == 40);

Error QueryError(LONG status, std::wstring_view operation)
{
    return NtStatusError(status, operation);
}

bool UnsupportedThreadQuery(LONG status)
{
    return status == static_cast<LONG>(0xC0000003u) || status == static_cast<LONG>(0xC0000002u);
}

} // namespace

Result<std::vector<SystemThread>> ParseSystemThreads(std::span<const std::byte> buffer, uint32_t process_id, SystemProcess* summary)
{
    std::vector<SystemThread> result;
    SystemProcess matched;
    size_t offset = 0;
    bool found = false;
    for (;;)
    {
        if (offset > buffer.size() || buffer.size() - offset < sizeof(SystemProcessInformation))
        {
            return FormatError(L"Parse system threads", L"The process record is truncated.");
        }
        SystemProcessInformation process;
        std::memcpy(&process, buffer.data() + offset, sizeof(process));
        const size_t remaining = buffer.size() - offset;
        const size_t record_size = process.next_entry_offset ? process.next_entry_offset : remaining;
        if (record_size < sizeof(process) || record_size > remaining ||
            (process.next_entry_offset && (process.next_entry_offset % alignof(SystemProcessInformation) != 0 || remaining - record_size < sizeof(process))) ||
            process.number_of_threads > (record_size - sizeof(process)) / sizeof(SystemThreadInformation))
        {
            return FormatError(L"Parse system threads", L"The process offsets or thread count are invalid.");
        }
        if (reinterpret_cast<uintptr_t>(process.process_id) == process_id)
        {
            found = true;
            matched = {process.number_of_threads, process.base_priority};
            result.reserve(process.number_of_threads);
            for (size_t index = 0; index < process.number_of_threads; ++index)
            {
                SystemThreadInformation thread;
                std::memcpy(&thread, buffer.data() + offset + sizeof(process) + index * sizeof(thread), sizeof(thread));
                result.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(thread.client_id.thread)), thread.thread_state, thread.wait_reason, thread.base_priority, thread.priority, thread.context_switches});
            }
        }
        if (!process.next_entry_offset)
        {
            break;
        }
        offset += process.next_entry_offset;
    }
    if (!found)
    {
        return InternalError(L"Query system threads", L"The process was absent from the system sample.");
    }
    if (summary)
    {
        *summary = matched;
    }
    return result;
}

Result<std::vector<SystemThread>> SystemThreadQuery::Query(uint32_t process_id)
{
    process_.reset();
    using QueryFunction = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    static const auto query = win32ui::ImportProc<QueryFunction>(L"ntdll.dll", "NtQuerySystemInformation");
    if (!query && !options_.query_system_information)
    {
        return UnsupportedError(L"NtQuerySystemInformation", L"The system query is unavailable.");
    }
    if (buffer_.empty())
    {
        buffer_.resize(256 * 1024);
    }
    for (unsigned attempt = 0; attempt < 16; ++attempt)
    {
        ULONG returned = 0;
        const int64_t started = QpcClock::Now();
        const LONG status = options_.query_system_information
                                ? options_.query_system_information(5, buffer_.data(), static_cast<ULONG>(buffer_.size()), &returned)
                                : query(5, buffer_.data(), static_cast<ULONG>(buffer_.size()), &returned);
        last_call_ticks_ = QpcClock::Now() - started;
        returned_size_ = returned;
        if (status == static_cast<LONG>(0xC0000004u))
        {
            const size_t needed = std::max<size_t>(returned, buffer_.size());
            const size_t grown = needed + needed / 4 + 65536;
            if (grown > std::numeric_limits<ULONG>::max())
            {
                return InternalError(L"NtQuerySystemInformation", L"The system sample exceeds the buffer limit.");
            }
            buffer_.resize(grown);
            continue;
        }
        if (status < 0)
        {
            return QueryError(status, L"NtQuerySystemInformation");
        }
        if (returned > buffer_.size())
        {
            return FormatError(L"NtQuerySystemInformation", L"The returned size exceeds the buffer.");
        }
        SystemProcess summary;
        auto threads = ParseSystemThreads(std::span<const std::byte>(buffer_.data(), returned), process_id, &summary);
        if (threads)
        {
            process_ = summary;
        }
        return threads;
    }
    return InternalError(L"NtQuerySystemInformation", L"The system sample kept growing during the query.");
}

Result<std::vector<SystemThread>> SystemThreadQuery::QueryCurrentProcess()
{
    const auto full_sample = [&] {
        use_system_process_information_ = true;
        return Query(GetCurrentProcessId());
    };
    if (current_process_threads_unavailable_)
    {
        return full_sample();
    }
    const int64_t now = options_.now ? options_.now() : QpcClock::Now();
    if (!last_current_process_check_)
    {
        last_current_process_check_ = now;
    }
    if (now - *last_current_process_check_ >= 5 * QpcClock::Frequency())
    {
        last_current_process_check_ = now;
        if (current_process_threads_incomplete_)
        {
            auto walked = QueryCurrentProcessThreads();
            if (!walked)
            {
                return full_sample();
            }
        }
        auto threads = full_sample();
        if (threads && !current_process_thread_ids_.empty())
        {
            // compare IDs as a new thread can replace an exited thread without changing the count
            current_process_threads_incomplete_ = std::any_of(threads->begin(), threads->end(), [&](const auto& thread) {
                return !std::binary_search(current_process_thread_ids_.begin(), current_process_thread_ids_.end(), thread.thread_id);
            });
        }
        return threads;
    }
    if (current_process_threads_incomplete_)
    {
        return full_sample();
    }
    auto threads = QueryCurrentProcessThreads();
    if (threads)
    {
        use_system_process_information_ = false;
        return threads;
    }
    return full_sample();
}

Result<std::vector<SystemThread>> SystemThreadQuery::QueryCurrentProcessThreads()
{
    process_.reset();
    returned_size_ = 0;
    last_call_ticks_ = 0;
    using NextFunction = LONG(NTAPI*)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE);
    using QueryFunction = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto next = win32ui::ImportProc<NextFunction>(L"ntdll.dll", "NtGetNextThread");
    static const auto query = win32ui::ImportProc<QueryFunction>(L"ntdll.dll", "NtQueryInformationThread");
    if ((!next && !options_.get_next_thread) || (!query && !options_.query_information_thread))
    {
        current_process_threads_unavailable_ = true;
        return UnsupportedError(L"ThreadSystemThreadInformation", L"the NT thread entry points are unavailable");
    }
    std::vector<SystemThread> result;
    win32ui::UniqueHandle current;
    for (;;)
    {
        HANDLE handle = nullptr;
        const int64_t next_started = QpcClock::Now();
        // MAXIMUM_ALLOWED can open a thread that denies only THREAD_QUERY_LIMITED_INFORMATION
        const LONG next_status = options_.get_next_thread
                                     ? options_.get_next_thread(GetCurrentProcess(), current.get(), MAXIMUM_ALLOWED, 0, 0, &handle)
                                     : next(GetCurrentProcess(), current.get(), MAXIMUM_ALLOWED, 0, 0, &handle);
        last_call_ticks_ += QpcClock::Now() - next_started;
        win32ui::UniqueHandle opened(handle);
        if (next_status == static_cast<LONG>(0x8000001Au))
        {
            if (result.empty())
            {
                return FormatError(L"NtGetNextThread", L"the current process has no accessible threads");
            }
            current_process_thread_ids_.clear();
            for (const auto& thread : result)
            {
                current_process_thread_ids_.push_back(thread.thread_id);
            }
            std::sort(current_process_thread_ids_.begin(), current_process_thread_ids_.end());
            return result;
        }
        if (next_status < 0)
        {
            current_process_threads_unavailable_ = UnsupportedThreadQuery(next_status);
            return QueryError(next_status, L"NtGetNextThread");
        }
        if (!opened)
        {
            return FormatError(L"NtGetNextThread", L"the returned thread handle is invalid");
        }
        current = std::move(opened);
        SystemThreadInformation information{};
        ULONG returned = 0;
        const int64_t query_started = QpcClock::Now();
        const LONG status = options_.query_information_thread
                                ? options_.query_information_thread(current.get(), 40, &information, sizeof(information), &returned)
                                : query(current.get(), 40, &information, sizeof(information), &returned);
        last_call_ticks_ += QpcClock::Now() - query_started;
        if (status < 0)
        {
            current_process_threads_unavailable_ = UnsupportedThreadQuery(status);
            return QueryError(status, L"NtQueryInformationThread (ThreadSystemThreadInformation)");
        }
        if (returned != sizeof(information))
        {
            current_process_threads_unavailable_ = true;
            return FormatError(L"ThreadSystemThreadInformation", L"the returned thread record size is invalid");
        }
        const uintptr_t thread_id = reinterpret_cast<uintptr_t>(information.client_id.thread);
        if (reinterpret_cast<uintptr_t>(information.client_id.process) != GetCurrentProcessId() ||
            !thread_id || thread_id > std::numeric_limits<uint32_t>::max())
        {
            return FormatError(L"ThreadSystemThreadInformation", L"the returned thread identity is invalid");
        }
        result.push_back({static_cast<uint32_t>(thread_id), information.thread_state, information.wait_reason, information.base_priority, information.priority, information.context_switches});
    }
}

Result<int32_t> QueryRunningThreadPriority()
{
    using QueryFunction = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto query = win32ui::ImportProc<QueryFunction>(L"ntdll.dll", "NtQueryInformationThread");
    if (!query)
    {
        return UnsupportedError(L"NtQueryInformationThread", L"The thread query is unavailable.");
    }
    ThreadBasicInformation information{};
    const LONG status = query(GetCurrentThread(), 0, &information, sizeof(information), nullptr);
    if (status < 0)
    {
        return QueryError(status, L"NtQueryInformationThread");
    }
    return information.priority;
}

Result<uintptr_t> QueryThreadStartAddress(HANDLE thread)
{
    using QueryFunction = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto query = win32ui::ImportProc<QueryFunction>(L"ntdll.dll", "NtQueryInformationThread");
    if (!query)
    {
        return UnsupportedError(L"NtQueryInformationThread", L"The thread query is unavailable.");
    }
    void* address = nullptr;
    const LONG status = query(thread, 9, &address, sizeof(address), nullptr);
    if (status < 0)
    {
        return QueryError(status, L"NtQueryInformationThread (start address)");
    }
    return reinterpret_cast<uintptr_t>(address);
}

} // namespace thread_sim::platform

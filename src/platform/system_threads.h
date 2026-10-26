// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "platform/error.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace thread_sim::platform
{

struct SystemThread
{
    uint32_t thread_id = 0;
    uint32_t state = 0;
    uint32_t wait_reason = 0;
    int32_t base_priority = 0;
    int32_t priority = 0;
    uint32_t context_switches = 0;
};

struct SystemProcess
{
    uint32_t thread_count = 0;
    int32_t base_priority = 0;
};

Result<std::vector<SystemThread>> ParseSystemThreads(std::span<const std::byte> buffer, uint32_t process_id, SystemProcess* process = nullptr);

// test overrides, empty functions use system APIs and QpcClock
struct SystemThreadQueryOptions
{
    std::function<LONG(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE)> get_next_thread;
    std::function<LONG(HANDLE, ULONG, PVOID, ULONG, PULONG)> query_information_thread;
    std::function<LONG(ULONG, PVOID, ULONG, PULONG)> query_system_information;
    std::function<int64_t()> now;
};

class SystemThreadQuery
{
  public:
    SystemThreadQuery() = default;
    explicit SystemThreadQuery(SystemThreadQueryOptions options)
        : options_(std::move(options))
    {
    }
    Result<std::vector<SystemThread>> Query(uint32_t process_id);
    Result<std::vector<SystemThread>> QueryCurrentProcess();
    bool using_system_process_information() const noexcept
    {
        return use_system_process_information_;
    }
    const std::optional<SystemProcess>& process() const noexcept
    {
        return process_;
    }
    size_t buffer_size() const noexcept
    {
        return buffer_.size();
    }
    size_t returned_size() const noexcept
    {
        return returned_size_;
    }
    int64_t last_call_ticks() const noexcept
    {
        return last_call_ticks_;
    }

  private:
    Result<std::vector<SystemThread>> QueryCurrentProcessThreads();

    SystemThreadQueryOptions options_;
    bool current_process_threads_unavailable_ = false;
    bool current_process_threads_incomplete_ = false;
    bool use_system_process_information_ = false;
    std::optional<int64_t> last_current_process_check_;
    std::vector<uint32_t> current_process_thread_ids_;
    std::optional<SystemProcess> process_;
    std::vector<std::byte> buffer_;
    size_t returned_size_ = 0;
    int64_t last_call_ticks_ = 0;
};

Result<int32_t> QueryRunningThreadPriority();
Result<uintptr_t> QueryThreadStartAddress(HANDLE thread);

} // namespace thread_sim::platform

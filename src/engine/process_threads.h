// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/snapshot.h"

#include "platform/system_threads.h"

#include <win32ui/handle_owner.h>

#include <mutex>
#include <span>

namespace thread_sim::engine
{

class ProcessThreadSampler
{
  public:
    // test override, default uses CPUID
    explicit ProcessThreadSampler(bool force_variable_counter = false);
    platform::Result<std::vector<OtherThreadSnapshot>> Sample(std::span<const uint32_t> exclude);
    // reuses the coordinator system query, sample_qpc is query completion time
    std::vector<OtherThreadSnapshot> SampleFromSystemThreads(std::span<const uint32_t> exclude, std::span<const platform::SystemThread> threads, int64_t sample_qpc);
    size_t cached_handle_count() const;
    // QPC ticks spent in the last system query only
    int64_t last_query_ticks() const;

  private:
    struct CachedThread
    {
        uint32_t thread_id = 0;
        win32ui::UniqueHandle handle;
        std::wstring name;
        std::wstring start_name;
        bool has_description = false;
        bool queried_start = false;
        int64_t description_qpc = 0;
        int64_t sample_qpc = 0;
        uint64_t sample_tsc = 0;
        std::optional<uint64_t> cycles;
        std::optional<uint64_t> cpu_time;
    };

    std::vector<OtherThreadSnapshot> SampleThreads(std::span<const uint32_t> exclude, std::span<const platform::SystemThread> threads, int64_t sample_qpc);
    void ReadName(CachedThread& cached, OtherThreadSnapshot& snapshot, int64_t now);
    void ReadThread(CachedThread& cached, OtherThreadSnapshot& snapshot, int64_t now, uint64_t tsc);

    mutable std::mutex mutex_;
    platform::SystemThreadQuery system_query_;
    std::vector<CachedThread> cache_;
    std::vector<ULONG> cpu_set_ids_;
    bool invariant_counter_;
};

} // namespace thread_sim::engine

// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/snapshot.h"

#include <array>
#include <atomic>

namespace thread_sim::engine
{

class LatencyHistogram
{
  public:
    static constexpr size_t kBucketCount = 1024 + 21 * 64 + 1;
    static size_t Bucket(uint64_t us) noexcept;
    static uint64_t LowerBound(size_t bucket) noexcept;
    void Record(uint64_t us) noexcept;
    LatencySummary Summarize() const noexcept;

  private:
    std::array<std::atomic<uint64_t>, kBucketCount> buckets_{};
    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> max_{0};
};

uint64_t MultiplyDivide(uint64_t value, uint64_t multiplier, uint64_t divisor) noexcept;

struct PeriodDecision
{
    uint64_t period = 0;
    uint64_t missed = 0;
    bool work = false;
};

class PeriodSchedule
{
  public:
    PeriodSchedule(int64_t origin, int64_t frequency, const ActivityConfig& activity) noexcept;
    int64_t Boundary(uint64_t period) const noexcept;
    int64_t WorkEnd(uint64_t period) const noexcept;
    uint64_t Containing(int64_t now) const noexcept;
    PeriodDecision Next(int64_t now, uint64_t expected) const noexcept;
    bool Overran(uint64_t period, int64_t now) const noexcept;

  private:
    int64_t origin_;
    uint64_t frequency_;
    uint64_t period_us_;
    uint64_t work_us_;
};

inline constexpr uint32_t kWorkloadBatchIterations = 512;
uint64_t WorkloadBatch(uint64_t state) noexcept;

} // namespace thread_sim::engine

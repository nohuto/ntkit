// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/workload_metrics.h"

#include <algorithm>
#include <bit>
#include <intrin.h>

namespace thread_sim::engine
{

size_t LatencyHistogram::Bucket(uint64_t us) noexcept
{
    if (us < 1024)
    {
        return static_cast<size_t>(us);
    }
    if (us >= (1ull << 31))
    {
        return kBucketCount - 1;
    }
    const unsigned exponent = std::bit_width(us) - 1u;
    return 1024 + (exponent - 10) * 64 + static_cast<size_t>((us - (1ull << exponent)) >> (exponent - 6));
}

uint64_t LatencyHistogram::LowerBound(size_t bucket) noexcept
{
    if (bucket < 1024)
    {
        return bucket;
    }
    if (bucket >= kBucketCount - 1)
    {
        return 1ull << 31;
    }
    const size_t exponent = 10 + (bucket - 1024) / 64;
    return (1ull << exponent) + ((bucket - 1024) % 64 << (exponent - 6));
}

void LatencyHistogram::Record(uint64_t us) noexcept
{
    buckets_[Bucket(us)].fetch_add(1, std::memory_order_relaxed);
    max_.store(std::max(us, max_.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    count_.fetch_add(1, std::memory_order_relaxed);
}

LatencySummary LatencyHistogram::Summarize() const noexcept
{
    LatencySummary result;
    result.count = count_.load(std::memory_order_relaxed);
    result.max_us = static_cast<int64_t>(max_.load(std::memory_order_relaxed));
    const uint64_t p50 = result.count / 2 + result.count % 2;
    const uint64_t p99 = result.count - result.count / 100;
    uint64_t cumulative = 0;
    bool middle = false;
    for (size_t index = 0; result.count && index < kBucketCount; ++index)
    {
        cumulative += buckets_[index].load(std::memory_order_relaxed);
        if (!middle && cumulative >= p50)
        {
            result.p50_us = static_cast<int64_t>(LowerBound(index));
            middle = true;
        }
        if (cumulative >= p99)
        {
            result.p99_us = static_cast<int64_t>(LowerBound(index));
            break;
        }
    }
    return result;
}

uint64_t MultiplyDivide(uint64_t value, uint64_t multiplier, uint64_t divisor) noexcept
{
    uint64_t high = 0;
    const uint64_t low = _umul128(value, multiplier, &high);
    uint64_t remainder = 0;
    return _udiv128(high, low, divisor, &remainder);
}

PeriodSchedule::PeriodSchedule(int64_t origin, int64_t frequency, const ActivityConfig& activity) noexcept
    : origin_(origin), frequency_(static_cast<uint64_t>(frequency)), period_us_(static_cast<uint64_t>(activity.period_ms) * 1000),
      work_us_(period_us_ * activity.work_percent / 100)
{
}

int64_t PeriodSchedule::Boundary(uint64_t period) const noexcept
{
    return origin_ + static_cast<int64_t>(MultiplyDivide(period * period_us_, frequency_, 1'000'000));
}

int64_t PeriodSchedule::WorkEnd(uint64_t period) const noexcept
{
    return origin_ + static_cast<int64_t>(MultiplyDivide(period * period_us_ + work_us_, frequency_, 1'000'000));
}

uint64_t PeriodSchedule::Containing(int64_t now) const noexcept
{
    uint64_t period = MultiplyDivide(static_cast<uint64_t>(std::max<int64_t>(0, now - origin_)), 1'000'000, frequency_) / period_us_;
    if (now >= Boundary(period + 1))
    {
        ++period;
    }
    return period;
}

PeriodDecision PeriodSchedule::Next(int64_t now, uint64_t expected) const noexcept
{
    const uint64_t current = Containing(now);
    const uint64_t candidate = std::max(current, expected);
    const bool work = now >= Boundary(candidate) && now < WorkEnd(candidate);
    const bool missed_current = candidate == current && !work;
    return {candidate + (missed_current ? 1 : 0), candidate - expected + (missed_current ? 1 : 0), work};
}

bool PeriodSchedule::Overran(uint64_t period, int64_t now) const noexcept
{
    return now >= Boundary(period + 1);
}

__declspec(noinline) uint64_t WorkloadBatch(uint64_t state) noexcept
{
    for (uint32_t index = 0; index < kWorkloadBatchIterations; ++index)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
    }
    return state;
}

} // namespace thread_sim::engine

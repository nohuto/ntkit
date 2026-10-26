// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "platform/windows_config.h"

#include <windows.h>

#include <cstdint>
#include <intrin.h>

namespace thread_sim::platform
{

// cycle to time conversion requires a time stamp counter invariant across frequency and power states
inline bool InvariantTimeStampCounter() noexcept
{
    static const bool invariant = [] {
        int registers[4]{};
        __cpuid(registers, 0x80000000);
        if (static_cast<unsigned>(registers[0]) < 0x80000007u)
        {
            return false;
        }
        __cpuid(registers, 0x80000007);
        return (registers[3] & (1 << 8)) != 0;
    }();
    return invariant;
}

// QPC measures elapsed wall time, not CPU time or cycles
struct QpcClock
{
    static int64_t Frequency() noexcept
    {
        static const int64_t frequency = [] {
            LARGE_INTEGER value = {};
            QueryPerformanceFrequency(&value);
            return value.QuadPart > 0 ? value.QuadPart : 1;
        }();
        return frequency;
    }
    static int64_t Now() noexcept
    {
        LARGE_INTEGER value = {};
        QueryPerformanceCounter(&value);
        return value.QuadPart;
    }
    // scale seconds and remainder separately to avoid overflow
    static int64_t TicksToMicroseconds(int64_t ticks) noexcept
    {
        const int64_t frequency = Frequency();
        const int64_t whole = ticks / frequency;
        const int64_t part = ticks % frequency;
        return whole * 1'000'000 + part * 1'000'000 / frequency;
    }
};

// FILETIME and GetThreadTimes use 100 ns units
inline uint64_t FileTimeTo100ns(const FILETIME& time) noexcept
{
    return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

} // namespace thread_sim::platform

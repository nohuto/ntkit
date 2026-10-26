// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/config.h"
#include "engine/snapshot.h"

#include <windows.h>

#include <string>
#include <vector>

namespace thread_sim::frame
{

// Idle to Realtime, in the order of their base priorities
inline constexpr DWORD kPriorityClasses[] = {IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS, REALTIME_PRIORITY_CLASS};
// "Below normal", unnamed values show their number
std::wstring PriorityClassName(DWORD priority_class);

// shared table, detail and dialog text
// uses kernel state during activity, such as "Waiting (Suspended)"
// uses app state before/after runs, such as "Pending", "Stopped", "Failed" or "Inactive"
std::wstring ThreadStateText(const engine::ThreadSnapshot& thread, uint32_t windows_build);
// activity level and waiting share, such as "Low, 25 % of 100 ms"
std::wstring DescribeActivity(const engine::ActivityConfig& activity);
// largest whole unit, such as "45 s", "5 min" or "1 h"
std::wstring DescribeDuration(uint32_t seconds);
inline std::wstring DescribeDuration(std::optional<uint32_t> seconds)
{
    return seconds ? DescribeDuration(*seconds) : L"Unlimited";
}
// "250 ms", whole seconds as "2 s"
std::wstring DescribeInterval(uint32_t milliseconds);
// "0x100, 0x101"
std::wstring DescribeCpuSetIds(const std::vector<uint32_t>& ids);

} // namespace thread_sim::frame

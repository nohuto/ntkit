// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/formatting.h"

#include "engine/validation.h"

#include "platform/text.h"

namespace thread_sim::frame
{

std::wstring PriorityClassName(DWORD priority_class)
{
    switch (priority_class)
    {
    case IDLE_PRIORITY_CLASS:
        return L"Idle";
    case BELOW_NORMAL_PRIORITY_CLASS:
        return L"Below normal";
    case NORMAL_PRIORITY_CLASS:
        return L"Normal";
    case ABOVE_NORMAL_PRIORITY_CLASS:
        return L"Above normal";
    case HIGH_PRIORITY_CLASS:
        return L"High";
    case REALTIME_PRIORITY_CLASS:
        return L"Realtime";
    default:
        return L"Class " + std::to_wstring(priority_class);
    }
}

std::wstring ThreadStateText(const engine::ThreadSnapshot& thread, uint32_t windows_build)
{
    if (!thread.active)
    {
        return L"Inactive";
    }
    switch (thread.state)
    {
    case engine::ThreadRunState::kPending:
    case engine::ThreadRunState::kStopped:
    case engine::ThreadRunState::kFailed:
        return engine::ThreadRunStateLabel(thread.state);
    default:
        return thread.sample && !thread.sample->previous && !thread.sample->error ? engine::DescribeThreadState(*thread.sample, windows_build) : engine::ThreadRunStateLabel(thread.state);
    }
}

std::wstring DescribeActivity(const engine::ActivityConfig& activity, bool uses_shared_mutex)
{
    std::wstring text = engine::ActivityLevelLabel(engine::ActivityLevelOf(activity));
    if (engine::RelevanceOf(activity, uses_shared_mutex).period)
    {
        text += L", " + std::to_wstring(activity.work_percent) + L" % of " + std::to_wstring(activity.period_ms) + L" ms";
    }
    if (engine::RelevanceOf(activity, uses_shared_mutex).wake_source && activity.wake_source != engine::WakeSource::kTimer)
    {
        text += activity.wake_source == engine::WakeSource::kEvent ? L", event wake" : L", window message wake";
    }
    if (activity.start_delay_ms > 0)
    {
        text += L", starts after " + std::to_wstring(activity.start_delay_ms) + L" ms";
    }
    return text;
}

std::wstring DescribeDuration(uint32_t seconds)
{
    if (seconds > 0 && seconds % 3600 == 0)
    {
        return std::to_wstring(seconds / 3600) + L" h";
    }
    if (seconds > 0 && seconds % 60 == 0)
    {
        return std::to_wstring(seconds / 60) + L" min";
    }
    return std::to_wstring(seconds) + L" s";
}

std::wstring DescribeInterval(uint32_t milliseconds)
{
    if (milliseconds > 0 && milliseconds % 1000 == 0)
    {
        return std::to_wstring(milliseconds / 1000) + L" s";
    }
    return std::to_wstring(milliseconds) + L" ms";
}

std::wstring DescribeCpuSetIds(const std::vector<uint32_t>& ids)
{
    std::wstring text;
    for (size_t index = 0; index < ids.size(); ++index)
    {
        text += (index ? L", " : L"") + platform::FormatHex(ids[index]);
    }
    return text;
}

} // namespace thread_sim::frame

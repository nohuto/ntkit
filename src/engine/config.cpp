// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/config.h"

namespace thread_sim::engine
{

namespace
{

template <typename Enum>
struct Names
{
    Enum value;
    const wchar_t* key;
    const wchar_t* label;
};

constexpr Names<RelativeThreadPriority> kRelativeThreadPriorityNames[] = {
    {RelativeThreadPriority::kLowest, L"lowest", L"Lowest"},
    {RelativeThreadPriority::kBelowNormal, L"below_normal", L"Below normal"},
    {RelativeThreadPriority::kNormal, L"normal", L"Normal"},
    {RelativeThreadPriority::kAboveNormal, L"above_normal", L"Above normal"},
    {RelativeThreadPriority::kHighest, L"highest", L"Highest"},
};

constexpr Names<AffinityMode> kAffinityModeNames[] = {
    {AffinityMode::kDefault, L"os_default", L"OS default"},
    {AffinityMode::kGroupAffinity, L"group_affinity", L"Group affinity"},
    {AffinityMode::kCpuSets, L"cpu_sets", L"CPU Sets"},
};

constexpr Names<WaitableTimerKind> kWaitableTimerKindNames[] = {
    {WaitableTimerKind::kDefault, L"default", L"Default waitable timer"},
    {WaitableTimerKind::kHighResolution, L"high_resolution", L"High-resolution waitable timer"},
};

constexpr Names<ActivityLevel> kActivityLevelNames[] = {
    {ActivityLevel::kLow, L"low", L"Low"},
    {ActivityLevel::kMedium, L"medium", L"Medium"},
    {ActivityLevel::kBusy, L"busy", L"Busy"},
    {ActivityLevel::kMaximum, L"maximum", L"Maximum"},
    {ActivityLevel::kCustom, L"custom", L"Custom"},
};

template <typename Enum, size_t N>
const Names<Enum>* Find(const Names<Enum> (&table)[N], Enum value)
{
    for (const auto& entry : table)
    {
        if (entry.value == value)
        {
            return &entry;
        }
    }
    return nullptr;
}

template <typename Enum, size_t N>
bool ParseKey(const Names<Enum> (&table)[N], std::wstring_view key, Enum* out)
{
    for (const auto& entry : table)
    {
        if (key == entry.key)
        {
            *out = entry.value;
            return true;
        }
    }
    return false;
}

template <typename Enum, size_t N>
const wchar_t* KeyOf(const Names<Enum> (&table)[N], Enum value)
{
    const auto* entry = Find(table, value);
    return entry ? entry->key : L"unknown";
}

template <typename Enum, size_t N>
const wchar_t* LabelOf(const Names<Enum> (&table)[N], Enum value)
{
    const auto* entry = Find(table, value);
    return entry ? entry->label : L"Unknown";
}

} // namespace

const wchar_t* RelativeThreadPriorityKey(RelativeThreadPriority value)
{
    return KeyOf(kRelativeThreadPriorityNames, value);
}
const wchar_t* AffinityModeKey(AffinityMode value)
{
    return KeyOf(kAffinityModeNames, value);
}
const wchar_t* WaitableTimerKindLabel(WaitableTimerKind value)
{
    return LabelOf(kWaitableTimerKindNames, value);
}
const wchar_t* WaitableTimerKindKey(WaitableTimerKind value)
{
    return KeyOf(kWaitableTimerKindNames, value);
}

ActivityLevel ActivityLevelOf(const ActivityConfig& activity)
{
    for (ActivityLevel level : {ActivityLevel::kLow, ActivityLevel::kMedium, ActivityLevel::kBusy, ActivityLevel::kMaximum})
    {
        if (activity.work_percent == ActivityLevelWork(level))
        {
            return level;
        }
    }
    return ActivityLevel::kCustom;
}

uint32_t ActivityLevelWork(ActivityLevel value)
{
    switch (value)
    {
    case ActivityLevel::kLow:
        return 25;
    case ActivityLevel::kMedium:
        return 50;
    case ActivityLevel::kBusy:
        return 75;
    case ActivityLevel::kMaximum:
        return 100;
    default:
        return 0;
    }
}

const wchar_t* ActivityLevelLabel(ActivityLevel value)
{
    return LabelOf(kActivityLevelNames, value);
}

const wchar_t* ActivityLevelKey(ActivityLevel value)
{
    return KeyOf(kActivityLevelNames, value);
}

bool ParseRelativeThreadPriorityKey(std::wstring_view key, RelativeThreadPriority* out)
{
    return ParseKey(kRelativeThreadPriorityNames, key, out);
}
bool ParseAffinityModeKey(std::wstring_view key, AffinityMode* out)
{
    return ParseKey(kAffinityModeNames, key, out);
}
bool ParseWaitableTimerKindKey(std::wstring_view key, WaitableTimerKind* out)
{
    return ParseKey(kWaitableTimerKindNames, key, out);
}

ThreadConfig MakeDefaultThread(size_t index)
{
    ThreadConfig thread;
    thread.name = L"Thread " + std::to_wstring(index + 1);
    return thread;
}

RunConfig MakeDefaultRun()
{
    RunConfig run;
    for (size_t index = 0; index < 2; ++index)
    {
        run.threads.push_back(MakeDefaultThread(index));
    }
    return run;
}

std::wstring NextThreadName(const RunConfig& run)
{
    for (size_t candidate = run.threads.size() + 1;; ++candidate)
    {
        std::wstring name = L"Thread " + std::to_wstring(candidate);
        bool used = false;
        for (const auto& thread : run.threads)
        {
            used = used || thread.name == name;
        }
        if (!used)
        {
            return name;
        }
    }
}

} // namespace thread_sim::engine

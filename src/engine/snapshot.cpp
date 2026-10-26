// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/snapshot.h"

#include <iterator>

namespace thread_sim::engine
{

const wchar_t* RunStateLabel(RunState state)
{
    switch (state)
    {
    case RunState::kIdle:
        return L"Idle";
    case RunState::kStarting:
        return L"Starting";
    case RunState::kActive:
        return L"Active";
    case RunState::kStopping:
        return L"Stopping";
    case RunState::kCompleted:
        return L"Completed";
    case RunState::kFailed:
        return L"Failed";
    }
    return L"Unknown";
}

const wchar_t* ThreadRunStateLabel(ThreadRunState state)
{
    switch (state)
    {
    case ThreadRunState::kPending:
        return L"Pending";
    case ThreadRunState::kWorking:
        return L"Working";
    case ThreadRunState::kResting:
        return L"Resting";
    case ThreadRunState::kSuspending:
        return L"Suspending";
    case ThreadRunState::kSuspended:
        return L"Suspended";
    case ThreadRunState::kStopped:
        return L"Stopped";
    case ThreadRunState::kFailed:
        return L"Failed";
    }
    return L"Unknown";
}

const wchar_t* ThreadRunStateKey(ThreadRunState state)
{
    switch (state)
    {
    case ThreadRunState::kPending:
        return L"pending";
    case ThreadRunState::kWorking:
        return L"working";
    case ThreadRunState::kResting:
        return L"resting";
    case ThreadRunState::kSuspending:
        return L"suspending";
    case ThreadRunState::kSuspended:
        return L"suspended";
    case ThreadRunState::kStopped:
        return L"stopped";
    case ThreadRunState::kFailed:
        return L"failed";
    }
    return L"unknown";
}

bool IsTerminal(RunState state)
{
    return state == RunState::kCompleted || state == RunState::kFailed;
}

namespace
{

// kernel _KTHREAD_STATE and _KWAIT_REASON names/values, unchanged since build 1507
constexpr const wchar_t* kThreadStates[] = {L"Initialized", L"Ready", L"Running", L"Standby", L"Terminated", L"Waiting", L"Transition", L"DeferredReady", L"GateWaitObsolete", L"WaitingForProcessInSwap"};
constexpr const wchar_t* kWaitReasons[] = {
    L"Executive",
    L"FreePage",
    L"PageIn",
    L"PoolAllocation",
    L"DelayExecution",
    L"Suspended",
    L"UserRequest",
    L"WrExecutive",
    L"WrFreePage",
    L"WrPageIn",
    L"WrPoolAllocation",
    L"WrDelayExecution",
    L"WrSuspended",
    L"WrUserRequest",
    L"WrSpare0",
    L"WrQueue",
    L"WrLpcReceive",
    L"WrLpcReply",
    L"WrVirtualMemory",
    L"WrPageOut",
    L"WrRendezvous",
    L"WrKeyedEvent",
    L"WrTerminated",
    L"WrProcessInSwap",
    L"WrCpuRateControl",
    L"WrCalloutStack",
    L"WrKernel",
    L"WrResource",
    L"WrPushLock",
    L"WrMutex",
    L"WrQuantumEnd",
    L"WrDispatchInt",
    L"WrPreempted",
    L"WrYieldExecution",
    L"WrFastMutex",
    L"WrGuardedMutex",
    L"WrRundown",
    L"WrAlertByThreadId",
    L"WrDeferredPreempt",
    L"WrPhysicalFault",
    L"WrIoRing",
    L"WrMdlCache",
    L"WrRcu",
};

// new reasons append to the list, moving MaximumWaitReason
uint32_t WaitReasonLimit(uint32_t build)
{
    if (build >= 26100)
    {
        return 43;
    }
    if (build >= 22621)
    {
        return 42;
    }
    if (build >= 22000)
    {
        return 41;
    }
    if (build >= 16299)
    {
        return 40;
    }
    return 39;
}

struct RelativePriorityName
{
    int32_t value;
    const wchar_t* name;
};

constexpr RelativePriorityName kRelativePriorities[] = {
    {-15, L"Idle"},
    {-2, L"Lowest"},
    {-1, L"Below normal"},
    {0, L"Normal"},
    {1, L"Above normal"},
    {2, L"Highest"},
    {15, L"Time critical"},
};

} // namespace

const wchar_t* ThreadStateName(uint32_t state)
{
    return state < std::size(kThreadStates) ? kThreadStates[state] : nullptr;
}

const wchar_t* WaitReasonName(uint32_t reason, uint32_t build)
{
    return reason < WaitReasonLimit(build) && reason < std::size(kWaitReasons) ? kWaitReasons[reason] : nullptr;
}

std::wstring DescribeThreadState(const ThreadSample& sample, uint32_t build)
{
    if (!sample.thread_state)
    {
        return {};
    }
    const wchar_t* state = ThreadStateName(*sample.thread_state);
    std::wstring text = state ? std::wstring(state) : L"Unknown state (" + std::to_wstring(*sample.thread_state) + L")";
    if (*sample.thread_state == kThreadStateWaiting && sample.wait_reason)
    {
        const wchar_t* reason = WaitReasonName(*sample.wait_reason, build);
        text += reason ? L" (" + std::wstring(reason) + L")" : L" (unknown wait reason " + std::to_wstring(*sample.wait_reason) + L")";
    }
    return text;
}

std::wstring DescribeRelativePriority(int32_t value)
{
    for (const auto& entry : kRelativePriorities)
    {
        if (entry.value == value)
        {
            return std::wstring(entry.name) + L" (" + std::to_wstring(value) + L")";
        }
    }
    return std::to_wstring(value);
}

std::optional<double> CpuSecondsFromCycles(const RunSnapshot& run, const ThreadSnapshot& thread)
{
    if (!run.cycle_counter_hz || *run.cycle_counter_hz <= 0 || !thread.cycles_total)
    {
        return std::nullopt;
    }
    return static_cast<double>(*thread.cycles_total) / *run.cycle_counter_hz;
}

} // namespace thread_sim::engine

// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/workload_backend.h"

#include "engine/process_threads.h"
#include "engine/workload_metrics.h"

#include "platform/clock.h"
#include "platform/system_threads.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <intrin.h>
#include <process.h>

namespace thread_sim::engine
{

namespace
{

using platform::QpcClock;

struct alignas(64) ThreadCounters
{
    std::atomic<ThreadRunState> state{ThreadRunState::kPending};
    std::array<std::byte, 7> counter_padding{};
    std::atomic<uint64_t> batches{0};
    std::atomic<uint64_t> checksum{0};
    std::atomic<uint64_t> missed{0};
    std::atomic<uint64_t> overruns{0};
    LatencyHistogram wake;
    LatencyHistogram mutex;
    std::array<std::atomic<uint64_t>, 32> priority_reads{};
    std::array<std::atomic<uint64_t>, 32> priority_offsets{};
    std::atomic<bool> priority_measured{false};
    std::array<std::byte, 39> tail_padding{};
};

static_assert(alignof(ThreadCounters) == 64);
static_assert(sizeof(ThreadCounters) % 64 == 0);

class Execution;

struct Thread
{
    ThreadCounters counters;
    Execution* owner = nullptr;
    size_t index = 0;
    win32ui::UniqueHandle handle;
    win32ui::UniqueHandle timer;
    bool suspended = false;
    std::atomic<bool> parked{false};
    std::atomic<DWORD> park_error{ERROR_SUCCESS};
    bool joined = false;
    bool final_times = false;
    std::atomic<bool> finished{false};
    std::optional<platform::Error> error;
    std::optional<std::wstring> warning;
    std::optional<uint64_t> last_cycles;
    int64_t last_qpc = 0;
};

class CoordinatorPriority
{
  public:
    CoordinatorPriority()
        : previous_(GetThreadPriority(GetCurrentThread()))
    {
    }
    platform::Status Raise()
    {
        if (previous_ == THREAD_PRIORITY_ERROR_RETURN)
        {
            return platform::LastWin32Error(L"GetThreadPriority (coordinator)");
        }
        if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
        {
            return platform::LastWin32Error(L"SetThreadPriority (coordinator)");
        }
        raised_ = true;
        return platform::Status::Ok();
    }
    ~CoordinatorPriority()
    {
        if (raised_)
        {
            SetThreadPriority(GetCurrentThread(), previous_);
        }
    }

  private:
    int previous_;
    bool raised_ = false;
};

class Execution
{
  public:
    Execution(RunContext& context, const WorkloadOptions& options)
        : context_(context), options_(options), invariant_counter_(!options.force_variable_counter && platform::InvariantTimeStampCounter())
    {
        const auto& plan = context.plan();
        base_.run_id = plan.run_id;
        base_.started_utc = plan.started_utc;
        base_.requested_duration_s = plan.config.duration_s;
        base_.sample_interval_ms = plan.config.sample_interval_ms;
        base_.qpc_frequency = QpcClock::Frequency();
        base_.threads.resize(plan.config.threads.size());
        threads_.reserve(plan.config.threads.size());
        for (size_t index = 0; index < plan.config.threads.size(); ++index)
        {
            base_.threads[index].index = static_cast<uint32_t>(index);
            base_.threads[index].active = plan.config.threads[index].active;
            auto thread = std::make_unique<Thread>();
            thread->owner = this;
            thread->index = index;
            if (plan.config.threads[index].active)
            {
                ++active_;
            }
            threads_.push_back(std::move(thread));
        }
    }

    ~Execution()
    {
        End();
        for (;;)
        {
            bool pending = false;
            for (auto& thread : threads_)
            {
                if (!thread->handle || WaitForSingleObject(thread->handle.get(), 0) == WAIT_OBJECT_0)
                {
                    continue;
                }
                pending = true;
                if (thread->suspended && ResumeThread(thread->handle.get()) != static_cast<DWORD>(-1))
                {
                    thread->suspended = false;
                }
                if (!thread->suspended)
                {
                    thread->parked.store(false, std::memory_order_release);
                    SetEvent(context_.thread_event(thread->index));
                }
            }
            if (!pending)
            {
                return;
            }
            WaitForSingleObject(changed_.get(), 20);
        }
    }

    void Abort(const platform::Error& error)
    {
        observer_enabled_ = false;
        base_.failure = error;
        End();
        for (auto& thread : threads_)
        {
            if (thread->handle)
            {
                Resume(*thread);
            }
        }
        base_.state = RunState::kStopping;
        Join();
        if (context_.run_origin())
        {
            Sample();
        }
        Finish(RunState::kFailed);
    }

    void Run()
    {
        auto raised = priority_.Raise();
        if (!raised)
        {
            base_.failure = raised.error();
            Finish(RunState::kFailed);
            return;
        }
        if (!CreateEvents())
        {
            Finish(RunState::kFailed);
            return;
        }
        if (options_.setup_delay_ms)
        {
            WaitForSingleObject(context_.stop_event(), options_.setup_delay_ms);
        }
        for (auto& thread : threads_)
        {
            if (context_.stop_requested())
            {
                AbortSetup(false);
                return;
            }
            if (!Config(*thread).active)
            {
                continue;
            }
            if (!Setup(*thread))
            {
                AbortSetup(true);
                return;
            }
        }
        base_.state = RunState::kStarting;
        Publish();
        for (auto& thread : threads_)
        {
            if (thread->handle && !Resume(*thread))
            {
                AbortSetup(true);
                return;
            }
        }
        const HANDLE ready_handles[] = {context_.stop_event(), ready_.get(), end_event_.get()};
        const DWORD ready = WaitForMultipleObjects(3, ready_handles, FALSE, INFINITE);
        if (ready != WAIT_OBJECT_0 + 1 || context_.stop_requested())
        {
            if (ready == WAIT_FAILED)
            {
                base_.failure = platform::LastWin32Error(L"WaitForMultipleObjects (start gate)");
            }
            {
                std::lock_guard lock(status_mutex_);
                if (failure_)
                {
                    base_.failure = failure_;
                }
            }
            AbortSetup(base_.failure.has_value());
            return;
        }
        const int64_t origin = QpcClock::Now();
        context_.SetRunOrigin(origin);
        base_.origin_qpc = origin;
        SetEvent(gate_.get());
        Baseline();
        base_.state = RunState::kActive;
        Sample();
        Publish();
        Coordinate();
    }

  private:
    const ThreadConfig& Config(const Thread& thread) const
    {
        return context_.plan().config.threads[thread.index];
    }

    bool CreateEvents()
    {
        for (auto* event : {&gate_, &ready_, &end_event_, &changed_})
        {
            event->reset(CreateEventW(nullptr, event == &changed_ ? FALSE : TRUE, FALSE, nullptr));
            if (!*event)
            {
                base_.failure = platform::LastWin32Error(L"CreateEventW (workload)");
                return false;
            }
        }
        if (std::any_of(context_.plan().config.threads.begin(), context_.plan().config.threads.end(), [](const ThreadConfig& thread) {
                return thread.active && thread.uses_shared_mutex;
            }))
        {
            mutex_.reset(CreateMutexW(nullptr, FALSE, nullptr));
            if (!mutex_)
            {
                base_.failure = platform::LastWin32Error(L"CreateMutexW (shared mutex)");
                return false;
            }
        }
        return true;
    }

    bool Setup(Thread& thread)
    {
        auto& observed = base_.threads[thread.index];
        const auto& config = Config(thread);
        const auto identity = ThreadContext(thread.index, config);
        if (config.activity.work_percent < 100)
        {
            auto& outcome = observed.applied.waitable_timer;
            outcome.attempted = true;
            const bool high = config.activity.waitable_timer_kind == WaitableTimerKind::kHighResolution;
            if (!(high && options_.force_high_resolution_fallback))
            {
                thread.timer.reset(CreateWaitableTimerExW(nullptr, nullptr, high ? CREATE_WAITABLE_TIMER_HIGH_RESOLUTION : 0, TIMER_ALL_ACCESS));
            }
            const DWORD error = high && options_.force_high_resolution_fallback ? ERROR_INVALID_PARAMETER : GetLastError();
            if (!thread.timer && high && error == ERROR_INVALID_PARAMETER)
            {
                outcome.error = platform::Win32Error(error, L"CreateWaitableTimerExW (high resolution)").WithContext(identity);
                outcome.error->detail = L"High resolution timer unavailable. Using a default timer.";
                thread.timer.reset(CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS));
                if (thread.timer)
                {
                    observed.applied.obtained_waitable_timer_kind = WaitableTimerKind::kDefault;
                    observed.warning = outcome.error->detail;
                }
            }
            if (!thread.timer)
            {
                outcome.error = platform::LastWin32Error(L"CreateWaitableTimerExW").WithContext(identity);
                observed.error = outcome.error;
                observed.state = ThreadRunState::kFailed;
                base_.failure = outcome.error;
                return false;
            }
            outcome.succeeded = true;
            if (!observed.applied.obtained_waitable_timer_kind)
            {
                observed.applied.obtained_waitable_timer_kind = config.activity.waitable_timer_kind;
            }
        }
        unsigned id = 0;
        const uintptr_t created = _beginthreadex(nullptr, 256 * 1024, &Execution::ThreadEntry, &thread, CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION, &id);
        if (!created)
        {
            observed.error = platform::CrtError(errno, L"_beginthreadex (thread)").WithContext(identity);
            observed.state = ThreadRunState::kFailed;
            base_.failure = observed.error;
            return false;
        }
        thread.handle.reset(reinterpret_cast<HANDLE>(created));
        thread.suspended = true;
        observed.thread_id = id;
        const std::wstring name = L"ThreadSim thread " + std::to_wstring(thread.index + 1) + L": " + config.name;
        const HRESULT named = SetThreadDescription(thread.handle.get(), name.c_str());
        if (FAILED(named))
        {
            observed.warning = platform::HResultError(named, L"SetThreadDescription").WithContext(identity).Describe();
        }
        auto policy = ApplyThreadPolicy(thread.handle.get(), config, thread.index, observed.applied, options_.force_setter_failure_thread == thread.index ? std::optional(options_.forced_setting) : std::nullopt);
        if (!policy)
        {
            base_.failure = policy.error();
            observed.state = ThreadRunState::kFailed;
            const auto& applied = observed.applied;
            for (const auto* outcome : {&applied.group_affinity, &applied.selected_cpu_sets, &applied.ideal_processor, &applied.relative_priority, &applied.priority_boost})
            {
                if (outcome->attempted && !outcome->succeeded)
                {
                    observed.error = outcome->error;
                    break;
                }
            }
            return false;
        }
        return true;
    }

    bool Resume(Thread& thread)
    {
        if (thread.suspended)
        {
            if (!resume_failure_forced_ && thread.parked.load(std::memory_order_acquire) && options_.force_resume_failure_thread == thread.index)
            {
                resume_failure_forced_ = true;
                Fail(thread, platform::Win32Error(ERROR_GEN_FAILURE, L"ResumeThread"));
                return false;
            }
            if (ResumeThread(thread.handle.get()) == static_cast<DWORD>(-1))
            {
                Fail(thread, platform::LastWin32Error(L"ResumeThread"));
                return false;
            }
            thread.suspended = false;
        }
        if (thread.parked.exchange(false, std::memory_order_acq_rel))
        {
            SetEvent(context_.thread_event(thread.index));
        }
        return true;
    }

    void End() noexcept
    {
        end_.store(true, std::memory_order_relaxed);
        if (end_event_)
        {
            SetEvent(end_event_.get());
        }
    }

    void AbortSetup(bool failed)
    {
        End();
        for (auto& thread : threads_)
        {
            if (thread->handle)
            {
                Resume(*thread);
            }
        }
        Join();
        for (auto& observed : base_.threads)
        {
            if (observed.state != ThreadRunState::kFailed)
            {
                observed.state = ThreadRunState::kStopped;
            }
        }
        Finish(failed ? RunState::kFailed : RunState::kCompleted);
    }

    static unsigned __stdcall ThreadEntry(void* parameter)
    {
        auto& thread = *static_cast<Thread*>(parameter);
        try
        {
            thread.owner->Work(thread);
        }
        catch (...)
        {
            thread.owner->Fail(thread, platform::InternalError(L"Thread workload", L"An exception interrupted the thread."));
        }
        // stopped state may wake the coordinator before thread exit, this wake follows the last access
        thread.finished.store(true, std::memory_order_release);
        SetEvent(thread.owner->changed_.get());
        return 0;
    }

    void State(Thread& thread, ThreadRunState state)
    {
        // only acknowledgements wake the coordinator, short periods switch work/rest too often to sample each change
        const ThreadRunState previous = thread.counters.state.exchange(state, std::memory_order_relaxed);
        if (previous != state && (state == ThreadRunState::kSuspending || state == ThreadRunState::kStopped || state == ThreadRunState::kFailed || previous == ThreadRunState::kSuspending))
        {
            SetEvent(changed_.get());
        }
    }

    void Fail(Thread& thread, platform::Error error)
    {
        error.WithContext(ThreadContext(thread.index, Config(thread)));
        {
            std::lock_guard lock(status_mutex_);
            thread.error = error;
            if (!failure_)
            {
                failure_ = error;
            }
        }
        State(thread, ThreadRunState::kFailed);
        End();
    }

    bool Release(Thread& thread, bool& owns)
    {
        if (!owns)
        {
            return true;
        }
        owns = false;
        if (!ReleaseMutex(mutex_.get()))
        {
            Fail(thread, platform::LastWin32Error(L"ReleaseMutex"));
            return false;
        }
        return true;
    }

    bool Checkpoint(Thread& thread, bool& owns, bool& resumed)
    {
        auto request = context_.desired(thread.index);
        if (end_.load(std::memory_order_relaxed) || request == ThreadRequest::kStop)
        {
            if (Release(thread, owns))
            {
                State(thread, ThreadRunState::kStopped);
            }
            return false;
        }
        if (request != ThreadRequest::kSuspend)
        {
            return true;
        }
        thread.parked.store(true, std::memory_order_release);
        State(thread, ThreadRunState::kSuspending);
        SetEvent(changed_.get());
        while (thread.parked.load(std::memory_order_acquire))
        {
            if (WaitForSingleObject(context_.thread_event(thread.index), INFINITE) == WAIT_FAILED)
            {
                thread.park_error.store(GetLastError(), std::memory_order_release);
                SetEvent(changed_.get());
            }
        }
        const DWORD park_error = thread.park_error.load(std::memory_order_acquire);
        if (park_error != ERROR_SUCCESS)
        {
            Fail(thread, platform::Win32Error(park_error, L"WaitForSingleObject (suspend)"));
            return false;
        }
        request = context_.desired(thread.index);
        if (end_.load(std::memory_order_relaxed) || request == ThreadRequest::kStop)
        {
            if (Release(thread, owns))
            {
                State(thread, ThreadRunState::kStopped);
            }
            return false;
        }
        resumed = true;
        return true;
    }

    bool Acquire(Thread& thread, bool& owns)
    {
        const int64_t requested = QpcClock::Now();
        const HANDLE handles[] = {mutex_.get(), context_.stop_event(), context_.thread_event(thread.index), end_event_.get()};
        const DWORD wait = WaitForMultipleObjects(4, handles, FALSE, INFINITE);
        const int64_t acquired = QpcClock::Now();
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED_0)
        {
            owns = true;
            thread.counters.mutex.Record(static_cast<uint64_t>(QpcClock::TicksToMicroseconds(acquired - requested)));
            if (wait == WAIT_ABANDONED_0)
            {
                std::lock_guard lock(status_mutex_);
                thread.warning = L"Shared mutex abandoned. Ownership obtained.";
            }
            return true;
        }
        if (wait == WAIT_FAILED)
        {
            Fail(thread, platform::LastWin32Error(L"WaitForMultipleObjects (shared mutex)"));
            return false;
        }
        return true;
    }

    bool Rest(Thread& thread, int64_t boundary)
    {
        if (options_.force_timer_failure_thread == thread.index)
        {
            Fail(thread, platform::Win32Error(ERROR_GEN_FAILURE, L"SetWaitableTimer"));
            return false;
        }
        const int64_t now = QpcClock::Now();
        const uint64_t difference = static_cast<uint64_t>(std::max<int64_t>(1, boundary - now));
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<int64_t>(std::max<uint64_t>(1, MultiplyDivide(difference, 10'000'000, static_cast<uint64_t>(QpcClock::Frequency()))));
        if (!SetWaitableTimer(thread.timer.get(), &due, 0, nullptr, nullptr, FALSE))
        {
            Fail(thread, platform::LastWin32Error(L"SetWaitableTimer"));
            return false;
        }
        const HANDLE handles[] = {thread.timer.get(), context_.stop_event(), context_.thread_event(thread.index), end_event_.get()};
        const DWORD wait = WaitForMultipleObjects(4, handles, FALSE, INFINITE);
        const int64_t woke = QpcClock::Now();
        if (wait == WAIT_OBJECT_0)
        {
            thread.counters.wake.Record(static_cast<uint64_t>(std::max<int64_t>(0, QpcClock::TicksToMicroseconds(woke - boundary))));
        }
        if (wait == WAIT_FAILED)
        {
            Fail(thread, platform::LastWin32Error(L"WaitForMultipleObjects (activity timer)"));
            return false;
        }
        return true;
    }

    void ReadPriority(Thread& thread, int64_t now, int64_t window_start, int64_t& next)
    {
        if (now < next)
        {
            return;
        }
        next = now + std::max<int64_t>(1, QpcClock::Frequency() / 1000);
        auto priority = platform::QueryRunningThreadPriority();
        if (priority)
        {
            const size_t level = static_cast<size_t>(std::clamp(priority.value(), 0, 31));
            thread.counters.priority_reads[level].fetch_add(1, std::memory_order_relaxed);
            if (Config(thread).activity.work_percent < 100)
            {
                thread.counters.priority_offsets[level].fetch_add(static_cast<uint64_t>(QpcClock::TicksToMicroseconds(now - window_start)), std::memory_order_relaxed);
            }
            thread.counters.priority_measured.store(true, std::memory_order_relaxed);
        }
    }

    void Work(Thread& thread)
    {
        if (ready_count_.fetch_add(1, std::memory_order_relaxed) + 1 == active_)
        {
            SetEvent(ready_.get());
        }
        const HANDLE gate_handles[] = {context_.stop_event(), end_event_.get(), gate_.get()};
        const DWORD gate = WaitForMultipleObjects(3, gate_handles, FALSE, INFINITE);
        if (gate == WAIT_FAILED)
        {
            Fail(thread, platform::LastWin32Error(L"WaitForMultipleObjects (thread gate)"));
            return;
        }
        if (gate != WAIT_OBJECT_0 + 2 || end_.load(std::memory_order_relaxed))
        {
            State(thread, ThreadRunState::kStopped);
            return;
        }
        const auto& config = Config(thread);
        const bool continuous = config.activity.work_percent == 100;
        const PeriodSchedule schedule(context_.run_origin(), QpcClock::Frequency(), config.activity);
        bool owns = false;
        struct Ownership
        {
            HANDLE mutex;
            bool& owns;
            ~Ownership()
            {
                if (owns)
                {
                    ReleaseMutex(mutex);
                }
            }
        } ownership{mutex_.get(), owns};
        uint64_t expected = 0;
        uint64_t checksum = static_cast<uint64_t>(QpcClock::Now()) ^ ((thread.index + 1) * 0x9E3779B97F4A7C15ull);
        for (;;)
        {
            bool resumed = false;
            if (!Checkpoint(thread, owns, resumed))
            {
                return;
            }
            int64_t now = QpcClock::Now();
            if (resumed)
            {
                expected = schedule.Containing(now);
            }
            uint64_t period = 0;
            if (!continuous || config.uses_shared_mutex)
            {
                const auto decision = schedule.Next(now, expected);
                if (!continuous)
                {
                    thread.counters.missed.fetch_add(decision.missed, std::memory_order_relaxed);
                }
                expected = decision.period;
                if (!decision.work)
                {
                    State(thread, ThreadRunState::kResting);
                    if (!Rest(thread, schedule.Boundary(expected)))
                    {
                        return;
                    }
                    continue;
                }
                period = decision.period;
            }
            State(thread, ThreadRunState::kWorking);
            if (config.uses_shared_mutex)
            {
                if (!Acquire(thread, owns))
                {
                    return;
                }
                if (!owns)
                {
                    continue;
                }
            }
            const int64_t window_start = QpcClock::Now();
            int64_t next_priority = window_start;
            bool realign = false;
            bool worked = false;
            for (;;)
            {
                bool inner_resumed = false;
                if (!Checkpoint(thread, owns, inner_resumed))
                {
                    return;
                }
                now = QpcClock::Now();
                if (inner_resumed)
                {
                    expected = schedule.Containing(now);
                    realign = true;
                    break;
                }
                if ((!continuous || config.uses_shared_mutex) && now >= schedule.WorkEnd(period))
                {
                    break;
                }
                ReadPriority(thread, now, window_start, next_priority);
                checksum = WorkloadBatch(checksum);
                thread.counters.checksum.store(checksum, std::memory_order_relaxed);
                thread.counters.batches.fetch_add(1, std::memory_order_relaxed);
                worked = true;
            }
            if (!Release(thread, owns))
            {
                return;
            }
            if (!realign)
            {
                // mutex waits past the work window count as missed periods, like late wakes
                if (!continuous && !worked)
                {
                    thread.counters.missed.fetch_add(1, std::memory_order_relaxed);
                }
                else if (!continuous && schedule.Overran(period, now))
                {
                    thread.counters.overruns.fetch_add(1, std::memory_order_relaxed);
                }
                expected = period + 1;
            }
        }
    }

    void Baseline()
    {
        if (!invariant_counter_)
        {
            base_.notices.push_back(L"Time stamp counter is not invariant. CPU measurements from cycles are unavailable.");
        }
        ULONG64 process_cycles = 0;
        if (QueryProcessCycleTime(GetCurrentProcess(), &process_cycles))
        {
            process_cycles_ = process_cycles;
            process_qpc_ = QpcClock::Now();
        }
        rate_qpc_ = QpcClock::Now();
        rate_tsc_ = __rdtsc();
        for (auto& thread : threads_)
        {
            if (!thread->handle)
            {
                continue;
            }
            ULONG64 cycles = 0;
            if (QueryThreadCycleTime(thread->handle.get(), &cycles))
            {
                thread->last_cycles = cycles;
                thread->last_qpc = QpcClock::Now();
            }
        }
    }

    // thread cycles use time stamp counter ticks, measure their rate against QPC
    // GetThreadTimes charges whole clock ticks and reports about half for 25 ms bursts
    void MeasureCycleRate()
    {
        const int64_t now = QpcClock::Now();
        const uint64_t tsc = __rdtsc();
        if (invariant_counter_ && rate_qpc_ && now > rate_qpc_)
        {
            base_.cycle_counter_hz = static_cast<double>(tsc - rate_tsc_) * static_cast<double>(QpcClock::Frequency()) / static_cast<double>(now - rate_qpc_);
        }
    }

    void Sample()
    {
        const int64_t started = QpcClock::Now();
        const auto system = system_query_.Query(GetCurrentProcessId());
        const int64_t queried = QpcClock::Now();
        base_.last_sample_interval_us = QpcClock::TicksToMicroseconds(started - (last_sample_ ? last_sample_ : context_.run_origin()));
        last_sample_ = started;
        MeasureCycleRate();
        if (!base_.process)
        {
            base_.process.emplace();
        }
        auto& process = *base_.process;
        process.thread_count.reset();
        process.base_priority.reset();
        if (system_query_.process())
        {
            process.thread_count = system_query_.process()->thread_count;
            process.base_priority = system_query_.process()->base_priority;
        }
        ULONG64 process_cycles = 0;
        if (QueryProcessCycleTime(GetCurrentProcess(), &process_cycles))
        {
            const int64_t now = QpcClock::Now();
            const bool long_enough = now - process_qpc_ >= QpcClock::Frequency() / 20;
            if (process_cycles_ && long_enough && base_.cycle_counter_hz)
            {
                process.cpu_percent_one_logical_processor = 100.0 * static_cast<double>(process_cycles - *process_cycles_) * static_cast<double>(QpcClock::Frequency()) /
                                                            (*base_.cycle_counter_hz * static_cast<double>(now - process_qpc_));
            }
            if (!process_cycles_ || long_enough)
            {
                process_cycles_ = process_cycles;
                process_qpc_ = now;
            }
        }
        else
        {
            process.cpu_percent_one_logical_processor.reset();
        }
        size_t sampled = 0;
        int64_t readback_ticks = 0;
        for (auto& owner : threads_)
        {
            auto& thread = *owner;
            if (!thread.handle)
            {
                continue;
            }
            auto& observed = base_.threads[thread.index];
            const bool exited = WaitForSingleObject(thread.handle.get(), 0) == WAIT_OBJECT_0;
            sampled += exited ? 0 : 1;
            const auto& config = Config(thread);
            observed.state = thread.suspended ? ThreadRunState::kSuspended : thread.counters.state.load(std::memory_order_relaxed);
            if (!exited)
            {
                const int64_t read_started = QpcClock::Now();
                ReadThreadPolicy(thread.handle.get(), observed.applied);
                readback_ticks += QpcClock::Now() - read_started;
            }
            if (context_.desired(thread.index) == ThreadRequest::kSuspend && observed.state != ThreadRunState::kSuspended &&
                observed.state != ThreadRunState::kStopped && observed.state != ThreadRunState::kFailed)
            {
                observed.state = ThreadRunState::kSuspending;
            }
            observed.work_batches = thread.counters.batches.load(std::memory_order_relaxed);
            if (config.activity.work_percent < 100)
            {
                observed.missed_periods = static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, thread.counters.missed.load(std::memory_order_relaxed)));
                observed.overruns = static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, thread.counters.overruns.load(std::memory_order_relaxed)));
                observed.wake_lateness = thread.counters.wake.Summarize();
            }
            if (config.uses_shared_mutex)
            {
                observed.mutex_wait = thread.counters.mutex.Summarize();
            }
            if (thread.counters.priority_measured.load(std::memory_order_relaxed))
            {
                RunningPriority priority;
                for (size_t index = 0; index < priority.reads.size(); ++index)
                {
                    priority.reads[index] = thread.counters.priority_reads[index].load(std::memory_order_relaxed);
                    priority.window_offset_us_sum[index] = thread.counters.priority_offsets[index].load(std::memory_order_relaxed);
                }
                observed.running_priority = priority;
            }
            if (!thread.final_times)
            {
                FILETIME created{}, exit{}, kernel{}, user{};
                if (GetThreadTimes(thread.handle.get(), &created, &exit, &kernel, &user))
                {
                    observed.kernel_time_100ns = platform::FileTimeTo100ns(kernel);
                    observed.user_time_100ns = platform::FileTimeTo100ns(user);
                }
                else
                {
                    observed.warning = platform::LastWin32Error(L"GetThreadTimes").WithContext(ThreadContext(thread.index, config)).Describe();
                }
                ULONG64 cycles = 0;
                if (QueryThreadCycleTime(thread.handle.get(), &cycles))
                {
                    const int64_t now = QpcClock::Now();
                    observed.cycles_total = cycles;
                    // samples just after start or acknowledgement are too short for a share
                    // extend the interval until a sample at least 50 ms later
                    const bool long_enough = now - thread.last_qpc >= QpcClock::Frequency() / 20;
                    if (thread.last_cycles && long_enough)
                    {
                        observed.cycles_delta = cycles - *thread.last_cycles;
                        if (base_.cycle_counter_hz)
                        {
                            observed.cpu_percent_one_logical_processor = 100.0 * static_cast<double>(*observed.cycles_delta) * static_cast<double>(QpcClock::Frequency()) /
                                                                         (*base_.cycle_counter_hz * static_cast<double>(now - thread.last_qpc));
                        }
                    }
                    if (!thread.last_cycles || long_enough)
                    {
                        thread.last_cycles = cycles;
                        thread.last_qpc = now;
                    }
                }
                else
                {
                    observed.warning = platform::LastWin32Error(L"QueryThreadCycleTime").WithContext(ThreadContext(thread.index, config)).Describe();
                }
                thread.final_times = exited;
            }
            else if (!joining_)
            {
                // threads ending during the run used no CPU in this interval
                // threads running to the end keep their last interval share
                observed.cycles_delta = 0;
                if (base_.cycle_counter_hz)
                {
                    observed.cpu_percent_one_logical_processor = 0.0;
                }
            }
            if (exited && observed.sample)
            {
                observed.sample->previous = true;
            }
            else
            {
                ThreadSample sample;
                sample.sample_qpc = queried;
                sample.previous = exited;
                if (!system)
                {
                    sample.error = system.error();
                }
                else
                {
                    const auto found = std::find_if(system->begin(), system->end(), [&](const platform::SystemThread& entry) { return entry.thread_id == observed.thread_id; });
                    if (found != system->end())
                    {
                        sample.thread_state = found->state;
                        if (found->state == kThreadStateWaiting)
                        {
                            sample.wait_reason = found->wait_reason;
                        }
                        sample.base_priority = found->base_priority;
                        sample.current_priority = found->priority;
                        sample.context_switches = found->context_switches;
                    }
                    else
                    {
                        sample.error = platform::InternalError(L"Sample thread", L"The thread was absent from the system sample.");
                    }
                }
                observed.sample = std::move(sample);
            }
        }
        base_.other_threads.clear();
        if (context_.sample_other_threads())
        {
            if (!other_sampler_)
            {
                other_sampler_ = std::make_unique<ProcessThreadSampler>(options_.force_variable_counter);
                other_exclude_.reserve(threads_.size());
            }
            other_exclude_.clear();
            for (const auto& thread : base_.threads)
            {
                if (thread.thread_id)
                {
                    other_exclude_.push_back(thread.thread_id);
                }
            }
            if (system)
            {
                base_.other_threads = other_sampler_->SampleFromSystemThreads(other_exclude_, system.value(), queried);
            }
        }
        else
        {
            other_sampler_.reset();
        }
        CollectStatus();
        if (observer_enabled_ && options_.sample_observer)
        {
            options_.sample_observer({sampled, QpcClock::Now() - started, readback_ticks});
        }
    }

    void CollectStatus()
    {
        std::lock_guard lock(status_mutex_);
        for (size_t index = 0; index < threads_.size(); ++index)
        {
            auto& observed = base_.threads[index];
            if (threads_[index]->error)
            {
                observed.error = threads_[index]->error;
                observed.state = ThreadRunState::kFailed;
            }
            if (threads_[index]->warning)
            {
                observed.warning = threads_[index]->warning;
            }
        }
        if (failure_)
        {
            base_.failure = failure_;
        }
    }

    void UpdateSuspension()
    {
        for (auto& owner : threads_)
        {
            auto& thread = *owner;
            if (!thread.handle || !thread.parked.load(std::memory_order_acquire))
            {
                continue;
            }
            if (context_.desired(thread.index) != ThreadRequest::kSuspend || end_.load(std::memory_order_relaxed) ||
                thread.park_error.load(std::memory_order_acquire) != ERROR_SUCCESS)
            {
                Resume(thread);
            }
            else if (!thread.suspended)
            {
                if (options_.force_suspend_failure_thread == thread.index)
                {
                    Fail(thread, platform::Win32Error(ERROR_GEN_FAILURE, L"SuspendThread"));
                    Resume(thread);
                }
                else if (SuspendThread(thread.handle.get()) == static_cast<DWORD>(-1))
                {
                    Fail(thread, platform::LastWin32Error(L"SuspendThread"));
                    Resume(thread);
                }
                else
                {
                    thread.suspended = true;
                }
            }
        }
    }

    void Coordinate()
    {
        const uint64_t frequency = static_cast<uint64_t>(QpcClock::Frequency());
        const uint64_t interval_us = static_cast<uint64_t>(context_.plan().config.sample_interval_ms) * 1000;
        const auto duration = context_.plan().config.duration_s;
        const std::optional<int64_t> deadline = duration ? std::optional(context_.run_origin() + static_cast<int64_t>(*duration * frequency)) : std::nullopt;
        uint64_t sample_number = 1;
        for (;;)
        {
            UpdateSuspension();
            const int64_t now = QpcClock::Now();
            if (context_.stop_requested() || (deadline && now >= *deadline) || end_.load(std::memory_order_relaxed))
            {
                base_.state = RunState::kStopping;
                End();
                Publish();
                break;
            }
            if (AllExited())
            {
                break;
            }
            const int64_t next_sample = context_.run_origin() + static_cast<int64_t>(MultiplyDivide(sample_number * interval_us, frequency, 1'000'000));
            const int64_t until = std::max<int64_t>(0, (deadline ? std::min(next_sample, *deadline) : next_sample) - now);
            const uint64_t milliseconds = MultiplyDivide(static_cast<uint64_t>(until), 1000, frequency);
            const DWORD timeout = static_cast<DWORD>(std::min<uint64_t>(MAXDWORD - 1, milliseconds + (until > 0 ? 1 : 0)));
            const HANDLE handles[] = {context_.stop_event(), context_.request_event(), changed_.get(), end_event_.get()};
            const DWORD wait = WaitForMultipleObjects(4, handles, FALSE, timeout);
            if (wait == WAIT_FAILED)
            {
                base_.failure = platform::LastWin32Error(L"WaitForMultipleObjects (coordinator)");
                End();
            }
            if (context_.stop_requested() || (deadline && QpcClock::Now() >= *deadline) || end_.load(std::memory_order_relaxed))
            {
                continue;
            }
            UpdateSuspension();
            Sample();
            Publish();
            const uint64_t elapsed_us = static_cast<uint64_t>(std::max<int64_t>(0, context_.ElapsedSinceOriginUs(QpcClock::Now())));
            sample_number = elapsed_us / interval_us + 1;
        }
        Join();
        Sample();
        Finish(base_.failure ? RunState::kFailed : RunState::kCompleted);
    }

    bool AllExited() const
    {
        return std::all_of(threads_.begin(), threads_.end(), [](const auto& thread) {
            return !thread->handle || thread->finished.load(std::memory_order_acquire) || WaitForSingleObject(thread->handle.get(), 0) == WAIT_OBJECT_0;
        });
    }

    void Join()
    {
        joining_ = true;
        for (auto& thread : threads_)
        {
            if (thread->handle)
            {
                Resume(*thread);
            }
        }
        for (;;)
        {
            for (auto& thread : threads_)
            {
                if (thread->handle && (thread->suspended || thread->parked.load(std::memory_order_acquire)))
                {
                    Resume(*thread);
                }
            }
            std::array<HANDLE, MAXIMUM_WAIT_OBJECTS> handles{};
            DWORD count = 0;
            bool pending = false;
            for (auto& thread : threads_)
            {
                if (!thread->handle || thread->joined)
                {
                    continue;
                }
                if (WaitForSingleObject(thread->handle.get(), 0) == WAIT_OBJECT_0)
                {
                    thread->joined = true;
                    continue;
                }
                pending = true;
                handles[count++] = thread->handle.get();
                if (count == MAXIMUM_WAIT_OBJECTS)
                {
                    break;
                }
            }
            if (!pending)
            {
                return;
            }
            const DWORD wait = WaitForMultipleObjects(count, handles.data(), TRUE, 20);
            if (wait == WAIT_FAILED)
            {
                base_.failure = platform::LastWin32Error(L"WaitForMultipleObjects (join)");
                End();
            }
            if (context_.run_origin())
            {
                Sample();
                Publish();
            }
        }
    }

    void Publish()
    {
        base_.elapsed_us = context_.ElapsedSinceOriginUs(QpcClock::Now());
        context_.Publish(base_);
    }

    void Finish(RunState state)
    {
        CollectStatus();
        base_.state = state;
        base_.ended_utc = UtcNowIso8601();
        for (auto& observed : base_.threads)
        {
            if (observed.state != ThreadRunState::kFailed)
            {
                observed.state = ThreadRunState::kStopped;
            }
        }
        Publish();
    }

    RunContext& context_;
    const WorkloadOptions& options_;
    CoordinatorPriority priority_;
    RunSnapshot base_;
    std::vector<std::unique_ptr<Thread>> threads_;
    size_t active_ = 0;
    std::atomic<size_t> ready_count_{0};
    std::atomic<bool> end_{false};
    win32ui::UniqueHandle gate_;
    win32ui::UniqueHandle ready_;
    win32ui::UniqueHandle end_event_;
    win32ui::UniqueHandle changed_;
    win32ui::UniqueHandle mutex_;
    std::mutex status_mutex_;
    std::optional<platform::Error> failure_;
    platform::SystemThreadQuery system_query_;
    std::unique_ptr<ProcessThreadSampler> other_sampler_;
    std::vector<uint32_t> other_exclude_;
    std::optional<uint64_t> process_cycles_;
    int64_t process_qpc_ = 0;
    int64_t last_sample_ = 0;
    int64_t rate_qpc_ = 0;
    uint64_t rate_tsc_ = 0;
    bool invariant_counter_ = false;
    bool joining_ = false;
    bool observer_enabled_ = true;
    bool resume_failure_forced_ = false;
};

class WorkloadBackend final : public Backend
{
  public:
    explicit WorkloadBackend(WorkloadOptions options)
        : options_(std::move(options))
    {
    }
    void Run(RunContext& context) override
    {
        Execution execution(context, options_);
        try
        {
            execution.Run();
        }
        catch (...)
        {
            execution.Abort(platform::InternalError(L"Run workload", L"An exception interrupted the workload engine."));
        }
    }

  private:
    WorkloadOptions options_;
};

} // namespace

std::unique_ptr<Backend> MakeWorkloadBackend(const WorkloadOptions& options)
{
    return std::make_unique<WorkloadBackend>(options);
}

} // namespace thread_sim::engine

// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/run_controller.h"

#include "engine/validation.h"

#include "platform/clock.h"

#include <bcrypt.h>

#include <cerrno>
#include <cstdio>
#include <exception>
#include <process.h>

namespace thread_sim::engine
{

SnapshotMailbox::SnapshotMailbox(NotificationSink* sink)
    : sink_(sink)
{
}

void SnapshotMailbox::Publish(std::shared_ptr<const RunSnapshot> snapshot)
{
    {
        std::lock_guard lock(mutex_);
        latest_ = std::move(snapshot);
    }
    published_.fetch_add(1, std::memory_order_relaxed);
    if (!pending_.exchange(true, std::memory_order_acq_rel))
    {
        notified_.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard sink_lock(sink_mutex_);
        if (!sink_ || !sink_->NotifySnapshotReady())
        {
            // consumer polling recovers lost notifications during a run
            pending_.store(false, std::memory_order_release);
        }
    }
}

void SnapshotMailbox::DisconnectSink()
{
    std::lock_guard lock(sink_mutex_);
    sink_ = nullptr;
}

std::shared_ptr<const RunSnapshot> SnapshotMailbox::Take()
{
    // clear first so a concurrent publish sends a new notification
    pending_.store(false, std::memory_order_release);
    std::lock_guard lock(mutex_);
    return latest_;
}

RunContext::RunContext(const RunPlan& plan, SnapshotMailbox* mailbox, std::shared_ptr<const std::atomic<bool>> sample_other_threads)
    : plan_(plan), mailbox_(mailbox), sample_other_threads_(std::move(sample_other_threads))
{
}

bool RunContext::Initialize()
{
    stop_event_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stop_event_)
    {
        init_error_ = platform::LastWin32Error(L"CreateEventW (stop)");
        return false;
    }
    request_event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!request_event_)
    {
        init_error_ = platform::LastWin32Error(L"CreateEventW (requests)");
        return false;
    }
    const size_t count = plan_.config.threads.size();
    thread_events_.resize(count);
    for (auto& event : thread_events_)
    {
        event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!event)
        {
            init_error_ = platform::LastWin32Error(L"CreateEventW (thread request)");
            return false;
        }
    }
    desired_ = std::make_unique<std::atomic<uint8_t>[]>(count);
    for (size_t index = 0; index < count; ++index)
    {
        desired_[index].store(static_cast<uint8_t>(ThreadRequest::kRun), std::memory_order_relaxed);
    }
    return true;
}

HANDLE RunContext::thread_event(size_t thread) const noexcept
{
    return thread < thread_events_.size() ? thread_events_[thread].get() : nullptr;
}

ThreadRequest RunContext::desired(size_t thread) const noexcept
{
    if (stop_all_.load(std::memory_order_acquire))
    {
        return ThreadRequest::kStop;
    }
    if (thread >= plan_.config.threads.size())
    {
        return ThreadRequest::kStop;
    }
    return static_cast<ThreadRequest>(desired_[thread].load(std::memory_order_acquire));
}

void RunContext::SetRunOrigin(int64_t qpc) noexcept
{
    origin_qpc_.store(qpc, std::memory_order_release);
}

int64_t RunContext::ElapsedSinceOriginUs(int64_t qpc) const noexcept
{
    const int64_t origin = origin_qpc_.load(std::memory_order_acquire);
    return platform::QpcClock::TicksToMicroseconds(qpc - (origin ? origin : plan_.start_qpc));
}

void RunContext::Publish(RunSnapshot snapshot)
{
    if (!snapshot.other_threads.empty() && !sample_other_threads())
    {
        snapshot.other_threads.clear();
    }
    snapshot.generation = plan_.generation;
    snapshot.sequence = ++sequence_;
    snapshot.final = IsTerminal(snapshot.state);
    snapshot.threads.resize(plan_.config.threads.size());
    for (size_t index = 0; index < snapshot.threads.size(); ++index)
    {
        auto& thread = snapshot.threads[index];
        if (!plan_.config.threads[index].active)
        {
            thread = ThreadSnapshot{};
        }
        thread.index = static_cast<uint32_t>(index);
        thread.active = plan_.config.threads[index].active;
    }
    snapshot.topology = plan_.topology;
    snapshot.environment = plan_.environment;
    if (snapshot.origin_qpc == 0)
    {
        snapshot.origin_qpc = origin_qpc_.load(std::memory_order_acquire);
    }
    if (snapshot.final && snapshot.end_qpc == 0)
    {
        snapshot.end_qpc = platform::QpcClock::Now();
    }
    published_final_ = published_final_ || snapshot.final;
    auto shared = std::make_shared<const RunSnapshot>(std::move(snapshot));
    last_ = shared;
    mailbox_->Publish(std::move(shared));
}

void RunContext::RequestStopAll()
{
    if (stop_all_.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }
    SetEvent(stop_event_.get());
    SetEvent(request_event_.get());
}

void RunContext::RequestThreads(std::span<const uint32_t> threads, ThreadRequest request)
{
    bool changed = false;
    for (uint32_t thread : threads)
    {
        if (thread >= plan_.config.threads.size() || !plan_.config.threads[thread].active || stop_requested())
        {
            continue;
        }
        auto current = desired_[thread].load(std::memory_order_acquire);
        while (current != static_cast<uint8_t>(ThreadRequest::kStop) && current != static_cast<uint8_t>(request))
        {
            if (desired_[thread].compare_exchange_weak(current, static_cast<uint8_t>(request), std::memory_order_acq_rel))
            {
                SetEvent(thread_event(thread));
                changed = true;
                break;
            }
        }
    }
    if (changed)
    {
        SetEvent(request_event_.get());
    }
}

namespace
{

std::wstring ExceptionText(const std::exception& exception)
{
    std::wstring text;
    for (const char* ch = exception.what(); ch && *ch; ++ch)
    {
        text.push_back(static_cast<unsigned char>(*ch));
    }
    return text;
}

} // namespace

struct RunController::CoordinatorState
{
    std::unique_ptr<RunPlan> plan;
    std::unique_ptr<RunContext> context;
    std::unique_ptr<Backend> backend;
    std::shared_ptr<SnapshotMailbox> mailbox;
};

RunController::RunController(BackendFactory factory, NotificationSink* sink)
    : factory_(std::move(factory)), mailbox_(std::make_shared<SnapshotMailbox>(sink)), sample_other_threads_(std::make_shared<std::atomic<bool>>(false))
{
}

RunController::~RunController()
{
    if (coordinator_)
    {
        RequestStopAll();
        if (WaitForSingleObject(coordinator_.get(), 5000) != WAIT_OBJECT_0)
        {
            // live coordinator retains all state, including the disconnected mailbox
            mailbox_->DisconnectSink();
            state_.release();
            coordinator_.release();
        }
    }
}

platform::Status RunController::Start(const RunConfig& config, const platform::Topology& topology, platform::Environment environment)
{
    if (!TryReap())
    {
        return platform::ValidationError(L"Start run", L"The previous run is still stopping.");
    }
    const auto process = platform::QueryProcessSettings();
    if (!process)
    {
        return process.error();
    }
    const ValidationResult validation = Validate(config, topology, process.value());
    if (validation.HasErrors())
    {
        const ValidationIssue* first = nullptr;
        for (const auto& issue : validation.issues)
        {
            if (issue.severity == IssueSeverity::kError)
            {
                first = &issue;
                break;
            }
        }
        std::wstring detail = first ? first->message : L"The config is invalid.";
        if (validation.ErrorCount() > 1)
        {
            detail += L" (" + std::to_wstring(validation.ErrorCount() - 1) + L" more)";
        }
        return platform::ValidationError(L"Start run", detail);
    }

    state_ = std::make_unique<CoordinatorState>();
    state_->mailbox = mailbox_;
    auto plan = std::make_unique<RunPlan>();
    plan->generation = generation_ + 1;
    plan->run_id = MakeRunId();
    plan->started_utc = UtcNowIso8601();
    plan->start_qpc = platform::QpcClock::Now();
    plan->config = config;
    plan->topology = std::make_shared<const platform::Topology>(topology);
    plan->environment = std::make_shared<const platform::Environment>(std::move(environment));
    std::unique_ptr<Backend> backend = factory_ ? factory_() : nullptr;
    if (!backend)
    {
        return platform::InternalError(L"Start run", L"No execution backend is available.");
    }
    auto context = std::make_unique<RunContext>(*plan, mailbox_.get(), sample_other_threads_);
    if (!context->Initialize())
    {
        return context->initialization_error();
    }

    generation_ = plan->generation;
    state_->plan = std::move(plan);
    state_->context = std::move(context);
    state_->backend = std::move(backend);

    // publish before coordinator creation to fix sequence order without suspending it
    RunSnapshot starting;
    starting.state = RunState::kStarting;
    starting.run_id = state_->plan->run_id;
    starting.started_utc = state_->plan->started_utc;
    starting.requested_duration_s = config.duration_s;
    starting.sample_interval_ms = config.sample_interval_ms;
    starting.qpc_frequency = platform::QpcClock::Frequency();
    for (size_t index = 0; index < config.threads.size(); ++index)
    {
        ThreadSnapshot thread;
        thread.index = static_cast<uint32_t>(index);
        thread.active = config.threads[index].active;
        starting.threads.push_back(std::move(thread));
    }
    state_->context->Publish(std::move(starting));

    const uintptr_t thread = _beginthreadex(nullptr, 0, &RunController::CoordinatorEntry, state_.get(), 0, nullptr);
    if (thread == 0)
    {
        const platform::Error error = platform::CrtError(errno, L"_beginthreadex (run coordinator)");
        const auto last = state_->context->last_published();
        RunSnapshot failed = last ? *last : RunSnapshot{};
        failed.state = RunState::kFailed;
        failed.ended_utc = UtcNowIso8601();
        failed.failure = error;
        state_->context->Publish(std::move(failed));
        state_->backend.reset();
        state_->context.reset();
        state_->plan.reset();
        return error;
    }
    coordinator_.reset(reinterpret_cast<HANDLE>(thread));
    return platform::Status::Ok();
}

unsigned __stdcall RunController::CoordinatorEntry(void* parameter)
{
    Coordinate(*static_cast<CoordinatorState*>(parameter));
    return 0;
}

void RunController::Coordinate(CoordinatorState& state)
{
    SetThreadDescription(GetCurrentThread(), L"Coordinator");
    std::optional<platform::Error> fault;
    try
    {
        state.backend->Run(*state.context);
    }
    catch (const std::bad_alloc&)
    {
        fault = platform::InternalError(L"Run backend", L"Out of memory.");
    }
    catch (const std::exception& exception)
    {
        fault = platform::InternalError(L"Run backend", ExceptionText(exception));
    }
    catch (...)
    {
        fault = platform::InternalError(L"Run backend", L"An unknown exception interrupted the backend.");
    }
    // destroy on the coordinator so backend destructor joins cannot block the UI
    state.backend.reset();
    if (!state.context->published_final())
    {
        // derive failure from the last published state to preserve final samples
        const auto last = state.context->last_published();
        RunSnapshot failed = last ? *last : RunSnapshot{};
        failed.state = RunState::kFailed;
        failed.run_id = state.plan->run_id;
        failed.started_utc = state.plan->started_utc;
        failed.ended_utc = UtcNowIso8601();
        failed.failure = fault ? *fault : platform::InternalError(L"Run backend", L"The backend returned without a final state.");
        state.context->Publish(std::move(failed));
    }
}

void RunController::RequestSuspend(std::span<const uint32_t> threads)
{
    if (state_ && state_->context)
    {
        state_->context->RequestThreads(threads, ThreadRequest::kSuspend);
    }
}

void RunController::RequestResume(std::span<const uint32_t> threads)
{
    if (state_ && state_->context)
    {
        state_->context->RequestThreads(threads, ThreadRequest::kRun);
    }
}

void RunController::RequestStop(std::span<const uint32_t> threads)
{
    if (state_ && state_->context)
    {
        state_->context->RequestThreads(threads, ThreadRequest::kStop);
    }
}

void RunController::RequestStopAll()
{
    if (state_ && state_->context)
    {
        state_->context->RequestStopAll();
    }
}

void RunController::SetSampleOtherThreads(bool enabled) noexcept
{
    sample_other_threads_->store(enabled, std::memory_order_relaxed);
}

void RunController::RequestSample()
{
    if (coordinator_ && state_ && state_->context)
    {
        SetEvent(state_->context->request_event());
    }
}

std::shared_ptr<const RunSnapshot> RunController::TakeLatest()
{
    return mailbox_->Take();
}

bool RunController::TryReap()
{
    if (!coordinator_)
    {
        return true;
    }
    if (WaitForSingleObject(coordinator_.get(), 0) != WAIT_OBJECT_0)
    {
        return false;
    }
    coordinator_.reset();
    state_->context.reset();
    state_->plan.reset();
    return true;
}

bool RunController::WaitIdle(uint32_t timeout_ms)
{
    if (!coordinator_)
    {
        return true;
    }
    WaitForSingleObject(coordinator_.get(), timeout_ms);
    return TryReap();
}

std::wstring UtcNowIso8601()
{
    SYSTEMTIME time = {};
    GetSystemTime(&time);
    wchar_t buffer[40] = {};
    swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return buffer;
}

std::wstring MakeRunId()
{
    SYSTEMTIME time = {};
    GetSystemTime(&time);
    uint32_t random = 0;
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&random), sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
    {
        const uint64_t mix = static_cast<uint64_t>(platform::QpcClock::Now()) ^ (static_cast<uint64_t>(GetCurrentProcessId()) * 2654435761ull);
        random = static_cast<uint32_t>(mix ^ (mix >> 32));
    }
    wchar_t buffer[48] = {};
    swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u-%08X", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, random);
    return buffer;
}

} // namespace thread_sim::engine

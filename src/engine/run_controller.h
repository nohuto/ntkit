// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/config.h"
#include "engine/snapshot.h"

#include "platform/environment.h"
#include "platform/error.h"
#include "platform/topology.h"

#include <win32ui/handle_owner.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace thread_sim::engine
{

class NotificationSink
{
  public:
    // engine threads call this, must be cheap, thread safe and nonblocking
    // returns false if notification cannot be queued
    virtual bool NotifySnapshotReady() noexcept = 0;

  protected:
    ~NotificationSink() = default;
};

// single replaceable slot, at most one notification pending until the consumer takes the snapshot
class SnapshotMailbox
{
  public:
    explicit SnapshotMailbox(NotificationSink* sink);
    void Publish(std::shared_ptr<const RunSnapshot> snapshot);
    std::shared_ptr<const RunSnapshot> Take();
    void DisconnectSink();
    uint64_t published() const noexcept
    {
        return published_.load(std::memory_order_relaxed);
    }
    uint64_t notified() const noexcept
    {
        return notified_.load(std::memory_order_relaxed);
    }

  private:
    NotificationSink* sink_;
    std::mutex sink_mutex_;
    std::mutex mutex_;
    std::shared_ptr<const RunSnapshot> latest_;
    std::atomic<bool> pending_{false};
    std::atomic<uint64_t> published_{0};
    std::atomic<uint64_t> notified_{0};
};

enum class ThreadRequest : uint8_t
{
    kRun = 0,
    kSuspend,
    kStop,
};

// topology and environment captured at Start, shared with every snapshot
struct RunPlan
{
    uint64_t generation = 0;
    std::wstring run_id;
    std::wstring started_utc;
    int64_t start_qpc = 0;
    RunConfig config;
    std::shared_ptr<const platform::Topology> topology;
    std::shared_ptr<const platform::Environment> environment;
};

struct ThreadPolicyRequest
{
    uint32_t thread = 0;
    SchedulingConfig scheduling;
    ThreadAffinityConfig affinity;
};

// shared by UI controller, coordinator and worker threads
// only coordinator calls Publish and SetRunOrigin
class RunContext
{
  public:
    RunContext(const RunPlan& plan, SnapshotMailbox* mailbox, std::shared_ptr<const std::atomic<bool>> sample_other_threads = {});
    RunContext(const RunContext&) = delete;
    RunContext& operator=(const RunContext&) = delete;

    const RunPlan& plan() const noexcept
    {
        return plan_;
    }
    HANDLE stop_event() const noexcept
    {
        return stop_event_.get();
    }
    HANDLE request_event() const noexcept
    {
        return request_event_.get();
    }
    HANDLE thread_event(size_t thread) const noexcept;
    bool stop_requested() const noexcept
    {
        return stop_all_.load(std::memory_order_acquire);
    }
    ThreadRequest desired(size_t thread) const noexcept;
    void SetRunOrigin(int64_t qpc) noexcept;
    int64_t run_origin() const noexcept
    {
        return origin_qpc_.load(std::memory_order_acquire);
    }
    int64_t ElapsedSinceOriginUs(int64_t qpc) const noexcept;
    bool sample_other_threads() const noexcept
    {
        return sample_other_threads_ && sample_other_threads_->load(std::memory_order_relaxed);
    }

    void Publish(RunSnapshot snapshot);
    bool published_final() const noexcept
    {
        return published_final_;
    }
    std::shared_ptr<const RunSnapshot> last_published() const noexcept
    {
        return last_;
    }

    void RequestStopAll();
    void RequestThreads(std::span<const uint32_t> threads, ThreadRequest request);
    void QueueThreadPolicy(ThreadPolicyRequest request);
    std::vector<ThreadPolicyRequest> TakeThreadPolicies(bool close = false);
    void RecordLiveChange(LiveChange change);
    bool Initialize();
    platform::Error initialization_error() const
    {
        return init_error_;
    }

  private:
    const RunPlan& plan_;
    SnapshotMailbox* mailbox_;
    std::shared_ptr<const std::atomic<bool>> sample_other_threads_;
    win32ui::UniqueHandle stop_event_;
    win32ui::UniqueHandle request_event_;
    std::vector<win32ui::UniqueHandle> thread_events_;
    std::atomic<bool> stop_all_{false};
    std::unique_ptr<std::atomic<uint8_t>[]> desired_;
    std::atomic<int64_t> origin_qpc_{0};
    uint64_t sequence_ = 0;
    bool published_final_ = false;
    std::shared_ptr<const RunSnapshot> last_;
    platform::Error init_error_;
    std::mutex policy_mutex_;
    std::vector<ThreadPolicyRequest> policy_requests_;
    std::vector<LiveChange> live_changes_;
    bool live_changes_closed_ = false;
};

class Backend
{
  public:
    // coordinator destroys backend after Run, destructor must not throw
    virtual ~Backend() = default;
    // runs on coordinator, must publish transitions and honor stop promptly
    // must join all threads even on exceptions, then publish Completed or Failed
    virtual void Run(RunContext& context) = 0;
};

using BackendFactory = std::function<std::unique_ptr<Backend>()>;

class RunController
{
  public:
    RunController(BackendFactory factory, NotificationSink* sink);
    ~RunController();
    RunController(const RunController&) = delete;
    RunController& operator=(const RunController&) = delete;

    platform::Status Start(const RunConfig& config, const platform::Topology& topology, platform::Environment environment = {});
    void RequestSuspend(std::span<const uint32_t> threads);
    void RequestResume(std::span<const uint32_t> threads);
    void RequestStop(std::span<const uint32_t> threads);
    void RequestStopAll();

    void ApplyThreadPolicy(uint32_t thread, const SchedulingConfig& scheduling, const ThreadAffinityConfig& affinity);
    void RecordProcessChange(std::wstring setting, std::wstring value, const platform::Status& status);

    void SetSampleOtherThreads(bool enabled) noexcept;
    void RequestSample();

    std::shared_ptr<const RunSnapshot> TakeLatest();
    uint64_t current_generation() const noexcept
    {
        return generation_;
    }
    bool busy() const noexcept
    {
        return static_cast<bool>(coordinator_);
    }
    // nonblocking, true once coordinator exits and run is released
    bool TryReap();
    // waits up to timeout_ms for coordinator exit, true once run is released
    bool WaitIdle(uint32_t timeout_ms);
    const SnapshotMailbox& mailbox() const noexcept
    {
        return *mailbox_;
    }

  private:
    static unsigned __stdcall CoordinatorEntry(void* parameter);
    struct CoordinatorState;
    static void Coordinate(CoordinatorState& state);

    BackendFactory factory_;
    std::shared_ptr<SnapshotMailbox> mailbox_;
    std::shared_ptr<std::atomic<bool>> sample_other_threads_;
    uint64_t generation_ = 0;
    std::unique_ptr<CoordinatorState> state_;
    win32ui::UniqueHandle coordinator_;
};

std::wstring MakeRunId();
std::wstring UtcNowIso8601();

} // namespace thread_sim::engine

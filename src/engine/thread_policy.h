// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/snapshot.h"

namespace thread_sim::engine
{

enum class PolicySetting
{
    kGroupAffinity,
    kSelectedCpuSets,
    kIdealProcessor,
    kRelativePriority,
    kPriorityBoost,
};

void ReadThreadPolicy(HANDLE handle, AppliedSettings& applied);

std::wstring ThreadContext(size_t index, const ThreadConfig& thread);
platform::Status ApplyThreadPolicy(HANDLE handle, const ThreadConfig& thread, size_t index, AppliedSettings& applied, std::optional<PolicySetting> force_failure = {});

} // namespace thread_sim::engine

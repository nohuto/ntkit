// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "platform/error.h"

#include <span>
#include <vector>

namespace thread_sim::platform
{

struct ProcessSettings
{
    DWORD priority_class = 0;
    uint16_t group = 0;
    uint64_t affinity = 0;
    uint64_t system_affinity = 0;
    std::vector<uint32_t> default_cpu_set_ids;
};

Result<ProcessSettings> QueryProcessSettings();
Status SetProcessPriorityClassValue(DWORD priority_class);
Status SetProcessAffinity(uint64_t mask);
Status SetProcessDefaultCpuSetIds(std::span<const uint32_t> ids);

} // namespace thread_sim::platform

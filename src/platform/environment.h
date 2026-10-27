// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace thread_sim::platform
{

struct Environment
{
    std::wstring windows_version;
    std::wstring windows_display;
    uint32_t windows_build = 0;
    std::wstring architecture;
    std::wstring processor_name;
    uint32_t process_id = 0;
    std::optional<uint32_t> priority_class;
    std::optional<bool> on_ac_power;
    std::optional<bool> hypervisor_present_cpuid; // CPUID hypervisor present bit, also set under VBS
    std::optional<uint64_t> process_affinity_mask;
    std::optional<uint64_t> system_affinity_mask;
    std::optional<uint16_t> process_group_count;
    std::wstring computer_name_hash;
};

Environment QueryEnvironment();
const wchar_t* PriorityClassKey(uint32_t priority_class);

} // namespace thread_sim::platform

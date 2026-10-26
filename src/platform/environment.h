// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace thread_sim::platform
{

// unknown environment values stay empty or nullopt
struct Environment
{
    std::wstring windows_version; // such as "10.0.22631.4317"
    std::wstring windows_display; // DisplayVersion, such as "23H2"
    uint32_t windows_build = 0;   // 0 if RtlGetVersion failed
    std::wstring architecture;    // "x64"
    std::wstring processor_name;
    uint32_t process_id = 0;
    std::optional<uint32_t> priority_class;
    std::optional<bool> on_ac_power;
    std::optional<bool> hypervisor_present_cpuid;  // CPUID hypervisor present bit, also set under VBS
    std::optional<uint64_t> process_affinity_mask; // GetProcessAffinityMask: primary group only
    std::optional<uint64_t> system_affinity_mask;
    std::optional<uint16_t> process_group_count; // GetProcessGroupAffinity
    std::wstring computer_name_hash;             // not the raw name
};

Environment QueryEnvironment();
const wchar_t* PriorityClassKey(uint32_t priority_class);

} // namespace thread_sim::platform

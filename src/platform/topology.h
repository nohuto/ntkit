// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "platform/error.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace thread_sim::platform
{

inline constexpr uint32_t kUnknownIndex = 0xFFFFFFFFu;

// logical processor numbers are relative to a processor group, never flattened
struct ProcessorNumber
{
    uint16_t group = 0;
    uint8_t number = 0;

    auto operator<=>(const ProcessorNumber&) const = default;
};

struct GroupAffinity
{
    uint16_t group = 0;
    uint64_t mask = 0;

    bool operator==(const GroupAffinity&) const = default;
};

struct ProcessorGroupInfo
{
    uint16_t index = 0;
    uint8_t maximum_processor_count = 0;
    uint8_t active_processor_count = 0;
    uint64_t active_processor_mask = 0;
};

// record indices are vector positions, numa_node_number is the OS node number
// kUnknownIndex means no record covers this logical processor
struct LogicalProcessorInfo
{
    ProcessorNumber id;
    uint32_t core_record_index = kUnknownIndex;
    uint32_t package_record_index = kUnknownIndex;
    uint32_t numa_node_number = kUnknownIndex;
    uint32_t l2_cache_record_index = kUnknownIndex;
    uint32_t l3_cache_record_index = kUnknownIndex;
    bool has_cpu_set = false;
    uint32_t cpu_set_id = 0; // opaque, valid only when has_cpu_set
};

struct CoreInfo
{
    GroupAffinity processors; // processor cores never span groups
    uint8_t efficiency_class = 0;
    bool smt = false;
};

struct CacheInfo
{
    uint8_t level = 0;
    uint8_t associativity = 0;
    uint16_t line_size = 0;
    uint32_t size_bytes = 0;
    uint8_t type = 0; // PROCESSOR_CACHE_TYPE
    std::vector<GroupAffinity> processors;
};

struct NumaNodeInfo
{
    uint32_t node_number = 0;
    std::vector<GroupAffinity> processors; // nodes may span groups
};

struct PackageInfo
{
    std::vector<GroupAffinity> processors;
};

// SYSTEM_CPU_SET_INFORMATION core, cache and node indices are relative to the processor group
struct CpuSetInfo
{
    uint32_t id = 0;
    uint16_t group = 0;
    uint8_t logical_processor_index = 0;
    uint8_t core_index = 0;
    uint8_t last_level_cache_index = 0;
    uint8_t numa_node_index = 0;
    uint8_t efficiency_class = 0;
    uint8_t scheduling_class = 0;
    bool parked = false;
    bool allocated = false;
    bool allocated_to_target_process = false;
    bool realtime = false;
    uint64_t allocation_tag = 0;
};

struct Topology
{
    std::vector<ProcessorGroupInfo> groups;
    std::vector<LogicalProcessorInfo> processors; // sorted by (group, number)
    std::vector<CoreInfo> cores;
    std::vector<CacheInfo> caches;
    std::vector<NumaNodeInfo> numa_nodes;
    std::vector<PackageInfo> packages;
    std::vector<CpuSetInfo> cpu_sets; // empty when the CPU Set API is unavailable or failed
    bool cpu_sets_known = false;
    std::vector<uint32_t> process_default_cpu_set_ids; // empty = no process default restriction
    bool process_default_cpu_set_ids_known = false;
    std::vector<std::wstring> warnings; // skipped/unknown records and partial failures
    std::wstring source_label;          // "live"

    const LogicalProcessorInfo* FindProcessor(ProcessorNumber id) const noexcept;
    const CpuSetInfo* FindCpuSet(uint32_t id) const noexcept;
    const ProcessorGroupInfo* FindGroup(uint16_t group) const noexcept;
    uint32_t ActiveProcessorCount() const noexcept;
    std::vector<uint8_t> EfficiencyClasses() const; // distinct, ascending
};

Result<Topology> QueryTopology();

// uses documented variable length layouts, Size controls bounds checks and advancement
// skips unknown relationship types
Result<Topology> ParseTopology(std::span<const std::byte> logical_processor_information_ex, std::span<const std::byte> cpu_set_information, std::span<const uint32_t> process_default_cpu_set_ids, bool cpu_sets_known, bool process_defaults_known);

std::wstring FormatProcessorNumber(ProcessorNumber id);            // "G0:LP5"
std::wstring FormatMaskList(uint64_t mask);                        // "0-3,8,10-11"
std::wstring DescribeProcessor(ProcessorNumber id);                // "Group 0, processor 5"
std::wstring DescribeGroupAffinity(uint16_t group, uint64_t mask); // "Group 0, mask 0x000000000000000F"
// accepts lists like "0-3,8", rejects invalid or out of range processor numbers
Result<uint64_t> ParseMaskList(std::wstring_view text, uint8_t maximum_processor_count);

} // namespace thread_sim::platform

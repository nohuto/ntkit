// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "platform/topology.h"

#include "platform/text.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace thread_sim::platform
{

namespace
{

static_assert(sizeof(KAFFINITY) == sizeof(uint64_t), "Topology parsing assumes x64 64-bit group masks.");

constexpr size_t kInfoHeaderSize = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor);
constexpr size_t kProcessorFixedSize = offsetof(PROCESSOR_RELATIONSHIP, GroupMask);
constexpr size_t kNumaFixedSize = offsetof(NUMA_NODE_RELATIONSHIP, GroupMask);
constexpr size_t kCacheFixedSize = offsetof(CACHE_RELATIONSHIP, GroupMask);
constexpr size_t kGroupFixedSize = offsetof(GROUP_RELATIONSHIP, GroupInfo);
constexpr size_t kCpuSetHeaderSize = offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet);
constexpr size_t kCpuSetRecordSize = sizeof(SYSTEM_CPU_SET_INFORMATION);
constexpr size_t kMaxGroupProcessors = 64;
constexpr int kQueryAttempts = 4;
constexpr wchar_t kParseOperation[] = L"ParseTopology";

static_assert(kInfoHeaderSize == 8 && kProcessorFixedSize == 24 && kNumaFixedSize == 24 && kCacheFixedSize == 32 && kGroupFixedSize == 24);
static_assert(sizeof(GROUP_AFFINITY) == 16 && sizeof(PROCESSOR_GROUP_INFO) == 48 && kCpuSetRecordSize == 32);
// die, module and shared compute unit records share the GroupCount and GroupMask offsets
static_assert(offsetof(SHARED_COMPUTE_UNIT_RELATIONSHIP, GroupCount) == offsetof(PROCESSOR_RELATIONSHIP, GroupCount));
static_assert(offsetof(SHARED_COMPUTE_UNIT_RELATIONSHIP, GroupMasks) == kProcessorFixedSize);

struct RecordLayout
{
    std::wstring_view kind;
    size_t type_offset;
    size_t size_offset;
    size_t header_size;
};

constexpr RecordLayout kInfoLayout = {L"Logical processor record", offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Relationship), offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Size), kInfoHeaderSize};
constexpr RecordLayout kCpuSetLayout = {L"CPU Set record", offsetof(SYSTEM_CPU_SET_INFORMATION, Type), offsetof(SYSTEM_CPU_SET_INFORMATION, Size), kCpuSetHeaderSize};

struct RawRecord
{
    size_t offset = 0;
    DWORD type = 0;
    std::span<const std::byte> bytes;
};

// callers must check offset + sizeof(T) against bytes.size()
template <typename T>
T Load(std::span<const std::byte> bytes, size_t offset) noexcept
{
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

Error RecordError(std::wstring_view kind, size_t offset, std::wstring_view detail)
{
    std::wstring text(kind);
    text += L" at offset " + FormatUnsigned(offset) + L": ";
    text += detail;
    return FormatError(kParseOperation, text);
}

Result<std::vector<RawRecord>> SplitRecords(std::span<const std::byte> buffer, const RecordLayout& layout)
{
    std::vector<RawRecord> records;
    size_t offset = 0;
    while (offset < buffer.size())
    {
        const size_t remaining = buffer.size() - offset;
        if (remaining < layout.header_size)
        {
            return RecordError(layout.kind, offset, L"only " + FormatUnsigned(remaining) + L" bytes remain for a " + FormatUnsigned(layout.header_size) + L"-byte header.");
        }
        const auto tail = buffer.subspan(offset);
        const DWORD size = Load<DWORD>(tail, layout.size_offset);
        if (size < layout.header_size)
        {
            return RecordError(layout.kind, offset, L"Size " + FormatUnsigned(size) + L" is smaller than the " + FormatUnsigned(layout.header_size) + L"-byte header.");
        }
        if (size > remaining)
        {
            return RecordError(layout.kind, offset, L"Size " + FormatUnsigned(size) + L" extends past the end of the buffer (" + FormatUnsigned(remaining) + L" bytes remain).");
        }
        records.push_back({offset, Load<DWORD>(tail, layout.type_offset), tail.first(size)});
        offset += size;
    }
    return records;
}

Status RequireBodySize(const RawRecord& record, size_t fixed_size, std::wstring_view what)
{
    if (record.bytes.size() < kInfoHeaderSize + fixed_size)
    {
        return RecordError(kInfoLayout.kind, record.offset, L"Size " + FormatUnsigned(record.bytes.size()) + L" is smaller than the " + FormatUnsigned(kInfoHeaderSize + fixed_size) + L" bytes required for " + std::wstring(what) + L".");
    }
    return Status::Ok();
}

Result<std::vector<GroupAffinity>> ReadGroupAffinities(const RawRecord& record, size_t body_offset, size_t count)
{
    const size_t begin = kInfoHeaderSize + body_offset;
    const size_t needed = begin + count * sizeof(GROUP_AFFINITY);
    if (needed > record.bytes.size())
    {
        return RecordError(kInfoLayout.kind, record.offset, FormatUnsigned(count) + L" group mask(s) need " + FormatUnsigned(needed) + L" bytes but Size is " + FormatUnsigned(record.bytes.size()) + L".");
    }
    std::vector<GroupAffinity> masks;
    masks.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
        const size_t entry = begin + index * sizeof(GROUP_AFFINITY);
        masks.push_back({Load<WORD>(record.bytes, entry + offsetof(GROUP_AFFINITY, Group)), Load<uint64_t>(record.bytes, entry + offsetof(GROUP_AFFINITY, Mask))});
    }
    return masks;
}

Status ParseProcessorRecord(const RawRecord& record, Topology& topology)
{
    if (Status status = RequireBodySize(record, kProcessorFixedSize, L"PROCESSOR_RELATIONSHIP"); !status)
    {
        return status;
    }
    const BYTE flags = Load<BYTE>(record.bytes, kInfoHeaderSize + offsetof(PROCESSOR_RELATIONSHIP, Flags));
    const BYTE efficiency_class = Load<BYTE>(record.bytes, kInfoHeaderSize + offsetof(PROCESSOR_RELATIONSHIP, EfficiencyClass));
    const WORD group_count = Load<WORD>(record.bytes, kInfoHeaderSize + offsetof(PROCESSOR_RELATIONSHIP, GroupCount));
    auto masks = ReadGroupAffinities(record, kProcessorFixedSize, group_count);
    if (!masks)
    {
        return masks.error();
    }
    if (record.type == RelationProcessorCore)
    {
        if (group_count != 1)
        {
            return RecordError(kInfoLayout.kind, record.offset, L"a processor core must report exactly one group mask, not " + FormatUnsigned(group_count) + L".");
        }
        topology.cores.push_back({masks->front(), efficiency_class, (flags & LTP_PC_SMT) != 0});
    }
    else if (record.type == RelationProcessorPackage)
    {
        if (group_count == 0)
        {
            return RecordError(kInfoLayout.kind, record.offset, L"a processor package must report at least one group mask.");
        }
        topology.packages.push_back({std::move(masks).value()});
    }
    return Status::Ok();
}

void MergeMasks(std::vector<GroupAffinity>& target, const std::vector<GroupAffinity>& source)
{
    for (const auto& mask : source)
    {
        const auto existing = std::find_if(target.begin(), target.end(), [&](const GroupAffinity& item) { return item.group == mask.group; });
        if (existing != target.end())
        {
            existing->mask |= mask.mask;
        }
        else
        {
            target.push_back(mask);
        }
    }
}

Status ParseNumaRecord(const RawRecord& record, Topology& topology)
{
    if (Status status = RequireBodySize(record, kNumaFixedSize, L"NUMA_NODE_RELATIONSHIP"); !status)
    {
        return status;
    }
    const DWORD node_number = Load<DWORD>(record.bytes, kInfoHeaderSize + offsetof(NUMA_NODE_RELATIONSHIP, NodeNumber));
    const WORD group_count = Load<WORD>(record.bytes, kInfoHeaderSize + offsetof(NUMA_NODE_RELATIONSHIP, GroupCount));
    // GroupCount 0 uses the layout before Windows 11, one GroupMask in the union
    auto masks = ReadGroupAffinities(record, kNumaFixedSize, group_count == 0 ? 1 : group_count);
    if (!masks)
    {
        return masks.error();
    }
    // RelationNumaNode and RelationNumaNodeEx describe the same node, merge them
    const auto existing = std::find_if(topology.numa_nodes.begin(), topology.numa_nodes.end(), [&](const NumaNodeInfo& node) { return node.node_number == node_number; });
    if (existing != topology.numa_nodes.end())
    {
        MergeMasks(existing->processors, masks.value());
    }
    else
    {
        topology.numa_nodes.push_back({node_number, std::move(masks).value()});
    }
    return Status::Ok();
}

Status ParseCacheRecord(const RawRecord& record, Topology& topology)
{
    if (Status status = RequireBodySize(record, kCacheFixedSize, L"CACHE_RELATIONSHIP"); !status)
    {
        return status;
    }
    const size_t body = kInfoHeaderSize;
    const WORD group_count = Load<WORD>(record.bytes, body + offsetof(CACHE_RELATIONSHIP, GroupCount));
    auto masks = ReadGroupAffinities(record, kCacheFixedSize, group_count == 0 ? 1 : group_count);
    if (!masks)
    {
        return masks.error();
    }
    CacheInfo cache;
    cache.level = Load<BYTE>(record.bytes, body + offsetof(CACHE_RELATIONSHIP, Level));
    cache.associativity = Load<BYTE>(record.bytes, body + offsetof(CACHE_RELATIONSHIP, Associativity));
    cache.line_size = Load<WORD>(record.bytes, body + offsetof(CACHE_RELATIONSHIP, LineSize));
    cache.size_bytes = Load<DWORD>(record.bytes, body + offsetof(CACHE_RELATIONSHIP, CacheSize));
    cache.type = static_cast<uint8_t>(Load<DWORD>(record.bytes, body + offsetof(CACHE_RELATIONSHIP, Type)));
    cache.processors = std::move(masks).value();
    topology.caches.push_back(std::move(cache));
    return Status::Ok();
}

Status ParseGroupRecord(const RawRecord& record, Topology& topology)
{
    if (!topology.groups.empty())
    {
        return RecordError(kInfoLayout.kind, record.offset, L"duplicate RelationGroup record.");
    }
    if (Status status = RequireBodySize(record, kGroupFixedSize, L"GROUP_RELATIONSHIP"); !status)
    {
        return status;
    }
    const size_t body = kInfoHeaderSize;
    const WORD active_groups = Load<WORD>(record.bytes, body + offsetof(GROUP_RELATIONSHIP, ActiveGroupCount));
    if (active_groups == 0)
    {
        return RecordError(kInfoLayout.kind, record.offset, L"RelationGroup reports no active groups.");
    }
    const size_t needed = body + kGroupFixedSize + static_cast<size_t>(active_groups) * sizeof(PROCESSOR_GROUP_INFO);
    if (needed > record.bytes.size())
    {
        return RecordError(kInfoLayout.kind, record.offset, L"ActiveGroupCount " + FormatUnsigned(active_groups) + L" needs " + FormatUnsigned(needed) + L" bytes but Size is " + FormatUnsigned(record.bytes.size()) + L".");
    }
    for (WORD index = 0; index < active_groups; ++index)
    {
        const size_t entry = body + kGroupFixedSize + static_cast<size_t>(index) * sizeof(PROCESSOR_GROUP_INFO);
        ProcessorGroupInfo group;
        group.index = index;
        group.maximum_processor_count = Load<BYTE>(record.bytes, entry + offsetof(PROCESSOR_GROUP_INFO, MaximumProcessorCount));
        group.active_processor_count = Load<BYTE>(record.bytes, entry + offsetof(PROCESSOR_GROUP_INFO, ActiveProcessorCount));
        group.active_processor_mask = Load<uint64_t>(record.bytes, entry + offsetof(PROCESSOR_GROUP_INFO, ActiveProcessorMask));
        const std::wstring label = L"Group " + FormatUnsigned(index);
        if (group.maximum_processor_count > kMaxGroupProcessors)
        {
            return RecordError(kInfoLayout.kind, record.offset, label + L" reports MaximumProcessorCount " + FormatUnsigned(group.maximum_processor_count) + L", above 64.");
        }
        if (static_cast<unsigned>(std::popcount(group.active_processor_mask)) != group.active_processor_count)
        {
            topology.warnings.push_back(label + L" reports ActiveProcessorCount " + FormatUnsigned(group.active_processor_count) + L" but its active mask has " + FormatUnsigned(std::popcount(group.active_processor_mask)) + L" processors. Using the mask.");
        }
        if (group.maximum_processor_count < kMaxGroupProcessors && (group.active_processor_mask >> group.maximum_processor_count) != 0)
        {
            topology.warnings.push_back(label + L" has active processors at or above MaximumProcessorCount " + FormatUnsigned(group.maximum_processor_count) + L".");
        }
        topology.groups.push_back(group);
    }
    return Status::Ok();
}

Status ParseLogicalProcessorRecords(std::span<const std::byte> buffer, Topology& topology)
{
    auto records = SplitRecords(buffer, kInfoLayout);
    if (!records)
    {
        return records.error();
    }
    for (const auto& record : records.value())
    {
        Status status;
        switch (record.type)
        {
        case RelationProcessorCore:
        case RelationProcessorPackage:
        case RelationProcessorDie:
        case RelationProcessorModule:
        case RelationProcessorSharedComputeUnit:
            // die, module and shared compute unit records are validated but not stored
            status = ParseProcessorRecord(record, topology);
            break;
        case RelationNumaNode:
        case RelationNumaNodeEx:
            status = ParseNumaRecord(record, topology);
            break;
        case RelationCache:
            status = ParseCacheRecord(record, topology);
            break;
        case RelationGroup:
            status = ParseGroupRecord(record, topology);
            break;
        default:
            topology.warnings.push_back(L"Skipped logical processor record with unknown Relationship " + FormatUnsigned(record.type) + L" at offset " + FormatUnsigned(record.offset) + L".");
            break;
        }
        if (!status)
        {
            return status;
        }
    }
    if (topology.groups.empty())
    {
        return FormatError(kParseOperation, L"No RelationGroup record. Processors cannot be enumerated.");
    }
    std::sort(topology.numa_nodes.begin(), topology.numa_nodes.end(), [](const NumaNodeInfo& left, const NumaNodeInfo& right) { return left.node_number < right.node_number; });
    return Status::Ok();
}

LogicalProcessorInfo* FindMutableProcessor(std::vector<LogicalProcessorInfo>& processors, ProcessorNumber id) noexcept
{
    const auto it = std::lower_bound(processors.begin(), processors.end(), id, [](const LogicalProcessorInfo& processor, ProcessorNumber value) { return processor.id < value; });
    return it != processors.end() && it->id == id ? &*it : nullptr;
}

// returns false if any mask bit refers to an inactive processor
template <typename Fn>
bool ForEachMaskProcessor(std::vector<LogicalProcessorInfo>& processors, std::span<const GroupAffinity> masks, Fn&& apply)
{
    bool all_active = true;
    for (const auto& mask : masks)
    {
        for (uint64_t bits = mask.mask; bits != 0; bits &= bits - 1)
        {
            const auto number = static_cast<uint8_t>(std::countr_zero(bits));
            if (auto* processor = FindMutableProcessor(processors, {mask.group, number}))
            {
                apply(*processor);
            }
            else
            {
                all_active = false;
            }
        }
    }
    return all_active;
}

void AssignIndex(Topology& topology, std::span<const GroupAffinity> masks, uint32_t LogicalProcessorInfo::* field, uint32_t value, const std::wstring& label)
{
    bool overlap = false;
    const bool all_active = ForEachMaskProcessor(topology.processors, masks, [&](LogicalProcessorInfo& processor) {
        if (processor.*field != kUnknownIndex)
        {
            overlap = true;
            return;
        }
        processor.*field = value;
    });
    if (!all_active)
    {
        topology.warnings.push_back(label + L" lists processors that are not active in any group.");
    }
    if (overlap)
    {
        topology.warnings.push_back(label + L" overlaps an earlier record. The earlier assignment is kept.");
    }
}

void BuildProcessors(Topology& topology)
{
    for (const auto& group : topology.groups)
    {
        for (uint64_t bits = group.active_processor_mask; bits != 0; bits &= bits - 1)
        {
            LogicalProcessorInfo processor;
            processor.id = {group.index, static_cast<uint8_t>(std::countr_zero(bits))};
            topology.processors.push_back(processor);
        }
    }
    for (uint32_t index = 0; index < topology.cores.size(); ++index)
    {
        AssignIndex(topology, std::span(&topology.cores[index].processors, 1), &LogicalProcessorInfo::core_record_index, index, L"Core " + FormatUnsigned(index));
    }
    for (uint32_t index = 0; index < topology.packages.size(); ++index)
    {
        AssignIndex(topology, topology.packages[index].processors, &LogicalProcessorInfo::package_record_index, index, L"Package " + FormatUnsigned(index));
    }
    for (const auto& node : topology.numa_nodes)
    {
        AssignIndex(topology, node.processors, &LogicalProcessorInfo::numa_node_number, node.node_number, L"NUMA node " + FormatUnsigned(node.node_number));
    }
    for (uint32_t index = 0; index < topology.caches.size(); ++index)
    {
        const CacheInfo& cache = topology.caches[index];
        const bool tracked = cache.level == 2 || cache.level == 3;
        const auto field = cache.level == 2 ? &LogicalProcessorInfo::l2_cache_record_index : &LogicalProcessorInfo::l3_cache_record_index;
        const bool all_active = ForEachMaskProcessor(topology.processors, cache.processors, [&](LogicalProcessorInfo& processor) {
            uint32_t& slot = processor.*field;
            // prefer a unified or data cache over an instruction cache of the same level
            if (tracked && (slot == kUnknownIndex || (topology.caches[slot].type == CacheInstruction && cache.type != CacheInstruction)))
            {
                slot = index;
            }
        });
        if (!all_active)
        {
            topology.warnings.push_back(L"Cache " + FormatUnsigned(index) + L" (L" + FormatUnsigned(cache.level) + L") lists processors that are not active in any group.");
        }
    }
}

Status ParseCpuSetRecords(std::span<const std::byte> buffer, Topology& topology)
{
    auto records = SplitRecords(buffer, kCpuSetLayout);
    if (!records)
    {
        return records.error();
    }
    for (const auto& record : records.value())
    {
        if (record.type != CpuSetInformation)
        {
            topology.warnings.push_back(L"Skipped CPU Set record with unknown Type " + FormatUnsigned(record.type) + L" at offset " + FormatUnsigned(record.offset) + L".");
            continue;
        }
        if (record.bytes.size() < kCpuSetRecordSize)
        {
            return RecordError(kCpuSetLayout.kind, record.offset, L"Size " + FormatUnsigned(record.bytes.size()) + L" is smaller than the " + FormatUnsigned(kCpuSetRecordSize) + L" bytes required for CpuSetInformation.");
        }
        const auto& bytes = record.bytes;
        CpuSetInfo set;
        set.id = Load<DWORD>(bytes, offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.Id));
        set.group = Load<WORD>(bytes, offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.Group));
        set.logical_processor_index = Load<BYTE>(bytes, offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.LogicalProcessorIndex));
        set.core_index = Load<BYTE>(bytes, offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.CoreIndex));
        set.last_level_cache_index = Load<BYTE>(bytes, offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.LastLevelCacheIndex));
        set.numa_node_index = Load<BYTE>(bytes, offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.NumaNodeIndex));
        set.efficiency_class = Load<BYTE>(bytes, offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.EfficiencyClass));
        set.scheduling_class = Load<BYTE>(bytes, offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.SchedulingClass));
        set.allocation_tag = Load<uint64_t>(bytes, offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.AllocationTag));
        const BYTE flags = Load<BYTE>(bytes, offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.AllFlags));
        set.parked = (flags & SYSTEM_CPU_SET_INFORMATION_PARKED) != 0;
        set.allocated = (flags & SYSTEM_CPU_SET_INFORMATION_ALLOCATED) != 0;
        set.allocated_to_target_process = (flags & SYSTEM_CPU_SET_INFORMATION_ALLOCATED_TO_TARGET_PROCESS) != 0;
        set.realtime = (flags & SYSTEM_CPU_SET_INFORMATION_REALTIME) != 0;
        if (topology.FindCpuSet(set.id))
        {
            topology.warnings.push_back(L"Skipped duplicate CPU Set ID " + FormatHex(set.id) + L" at offset " + FormatUnsigned(record.offset) + L".");
            continue;
        }
        topology.cpu_sets.push_back(set);
    }
    if (topology.cpu_sets.empty())
    {
        topology.warnings.push_back(L"The system reported no CPU Sets.");
    }
    return Status::Ok();
}

void MapCpuSets(Topology& topology)
{
    for (const auto& set : topology.cpu_sets)
    {
        const ProcessorNumber id = {set.group, set.logical_processor_index};
        LogicalProcessorInfo* processor = FindMutableProcessor(topology.processors, id);
        if (!processor)
        {
            topology.warnings.push_back(L"CPU Set " + FormatHex(set.id) + L" refers to " + FormatProcessorNumber(id) + L", which is not an active processor.");
            continue;
        }
        if (processor->has_cpu_set)
        {
            topology.warnings.push_back(L"CPU Sets " + FormatHex(processor->cpu_set_id) + L" and " + FormatHex(set.id) + L" both claim " + FormatProcessorNumber(id) + L". Keeping " + FormatHex(processor->cpu_set_id) + L".");
            continue;
        }
        processor->has_cpu_set = true;
        processor->cpu_set_id = set.id;
    }
}

uint64_t RangeMask(unsigned first, unsigned last) noexcept
{
    const unsigned count = last - first + 1;
    return (count >= 64 ? ~0ull : (1ull << count) - 1) << first;
}

// variable length records need 8 byte aligned storage
struct RecordBuffer
{
    std::vector<uint64_t> storage;
    size_t length = 0;

    void Resize(size_t bytes)
    {
        storage.assign((bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t), 0);
    }
    size_t Capacity() const noexcept
    {
        return storage.size() * sizeof(uint64_t);
    }
    void* Data() noexcept
    {
        return storage.empty() ? nullptr : storage.data();
    }
    std::span<const std::byte> Bytes() const noexcept
    {
        return std::as_bytes(std::span(storage)).first(length);
    }
};

Result<RecordBuffer> QueryLogicalProcessorRecords()
{
    RecordBuffer buffer;
    for (int attempt = 0; attempt < kQueryAttempts; ++attempt)
    {
        DWORD length = static_cast<DWORD>(buffer.Capacity());
        if (GetLogicalProcessorInformationEx(RelationAll, static_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.Data()), &length))
        {
            if (length > buffer.Capacity())
            {
                return InternalError(L"GetLogicalProcessorInformationEx", L"Returned length exceeds the buffer.");
            }
            buffer.length = length;
            return buffer;
        }
        Error error = LastWin32Error(L"GetLogicalProcessorInformationEx");
        if (error.code != ERROR_INSUFFICIENT_BUFFER || length == 0)
        {
            return error;
        }
        buffer.Resize(length);
    }
    return InternalError(L"GetLogicalProcessorInformationEx", L"Required size kept growing across retries.");
}

Result<RecordBuffer> QuerySystemCpuSetRecords()
{
    RecordBuffer buffer;
    for (int attempt = 0; attempt < kQueryAttempts; ++attempt)
    {
        ULONG length = 0;
        // passing this process fills AllocatedToTargetProcess for our CPU Set reservations
        if (GetSystemCpuSetInformation(static_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.Data()), static_cast<ULONG>(buffer.Capacity()), &length, GetCurrentProcess(), 0))
        {
            if (length > buffer.Capacity())
            {
                return InternalError(L"GetSystemCpuSetInformation", L"Returned length exceeds the buffer.");
            }
            buffer.length = length;
            return buffer;
        }
        Error error = LastWin32Error(L"GetSystemCpuSetInformation");
        if (error.code != ERROR_INSUFFICIENT_BUFFER || length == 0)
        {
            return error;
        }
        buffer.Resize(length);
    }
    return InternalError(L"GetSystemCpuSetInformation", L"Required size kept growing across retries.");
}

Result<std::vector<uint32_t>> QueryProcessDefaultCpuSets()
{
    std::vector<ULONG> ids;
    for (int attempt = 0; attempt < kQueryAttempts; ++attempt)
    {
        ULONG required = 0;
        if (GetProcessDefaultCpuSets(GetCurrentProcess(), ids.empty() ? nullptr : ids.data(), static_cast<ULONG>(ids.size()), &required))
        {
            if (required > ids.size())
            {
                return InternalError(L"GetProcessDefaultCpuSets", L"Returned count exceeds the buffer.");
            }
            return std::vector<uint32_t>(ids.begin(), ids.begin() + required);
        }
        Error error = LastWin32Error(L"GetProcessDefaultCpuSets");
        if (error.code != ERROR_INSUFFICIENT_BUFFER || required == 0)
        {
            return error;
        }
        ids.assign(required, 0);
    }
    return InternalError(L"GetProcessDefaultCpuSets", L"Required count kept growing across retries.");
}

} // namespace

const LogicalProcessorInfo* Topology::FindProcessor(ProcessorNumber id) const noexcept
{
    const auto it = std::lower_bound(processors.begin(), processors.end(), id, [](const LogicalProcessorInfo& processor, ProcessorNumber value) { return processor.id < value; });
    return it != processors.end() && it->id == id ? &*it : nullptr;
}

const CpuSetInfo* Topology::FindCpuSet(uint32_t id) const noexcept
{
    for (const auto& set : cpu_sets)
    {
        if (set.id == id)
        {
            return &set;
        }
    }
    return nullptr;
}

const ProcessorGroupInfo* Topology::FindGroup(uint16_t group) const noexcept
{
    for (const auto& info : groups)
    {
        if (info.index == group)
        {
            return &info;
        }
    }
    return nullptr;
}

uint32_t Topology::ActiveProcessorCount() const noexcept
{
    return static_cast<uint32_t>(processors.size());
}

std::vector<uint8_t> Topology::EfficiencyClasses() const
{
    std::vector<uint8_t> classes;
    for (const auto& core : cores)
    {
        classes.push_back(core.efficiency_class);
    }
    for (const auto& set : cpu_sets)
    {
        classes.push_back(set.efficiency_class);
    }
    std::sort(classes.begin(), classes.end());
    classes.erase(std::unique(classes.begin(), classes.end()), classes.end());
    return classes;
}

Result<Topology> QueryTopology()
{
    auto information = QueryLogicalProcessorRecords();
    if (!information)
    {
        return std::move(information).error();
    }
    std::vector<std::wstring> warnings;
    auto cpu_sets = QuerySystemCpuSetRecords();
    if (!cpu_sets)
    {
        warnings.push_back(L"CPU Sets unavailable: " + cpu_sets.error().Describe());
    }
    auto defaults = QueryProcessDefaultCpuSets();
    if (!defaults)
    {
        warnings.push_back(L"Process default CPU Sets unavailable: " + defaults.error().Describe());
    }
    const auto cpu_set_bytes = cpu_sets ? cpu_sets->Bytes() : std::span<const std::byte>();
    const auto default_ids = defaults ? std::span<const uint32_t>(defaults.value()) : std::span<const uint32_t>();
    auto topology = ParseTopology(information->Bytes(), cpu_set_bytes, default_ids, cpu_sets.ok(), defaults.ok());
    if (!topology)
    {
        return std::move(topology).error().WithContext(L"live topology");
    }
    topology->warnings.insert(topology->warnings.begin(), warnings.begin(), warnings.end());
    topology->source_label = L"live";
    return topology;
}

Result<Topology> ParseTopology(std::span<const std::byte> logical_processor_information_ex, std::span<const std::byte> cpu_set_information, std::span<const uint32_t> process_default_cpu_set_ids, bool cpu_sets_known, bool process_defaults_known)
{
    if (!cpu_sets_known && !cpu_set_information.empty())
    {
        return ValidationError(kParseOperation, L"CPU Set records were supplied but marked unknown.");
    }
    if (!process_defaults_known && !process_default_cpu_set_ids.empty())
    {
        return ValidationError(kParseOperation, L"Process default CPU Sets were supplied but marked unknown.");
    }
    Topology topology;
    topology.cpu_sets_known = cpu_sets_known;
    topology.process_default_cpu_set_ids_known = process_defaults_known;
    if (Status status = ParseLogicalProcessorRecords(logical_processor_information_ex, topology); !status)
    {
        return status.error();
    }
    BuildProcessors(topology);
    if (cpu_sets_known)
    {
        if (Status status = ParseCpuSetRecords(cpu_set_information, topology); !status)
        {
            return status.error();
        }
        MapCpuSets(topology);
    }
    topology.process_default_cpu_set_ids.assign(process_default_cpu_set_ids.begin(), process_default_cpu_set_ids.end());
    if (cpu_sets_known)
    {
        for (const uint32_t id : topology.process_default_cpu_set_ids)
        {
            if (!topology.FindCpuSet(id))
            {
                topology.warnings.push_back(L"Process default CPU Set " + FormatHex(id) + L" is not in the system CPU Set list.");
            }
        }
    }
    return topology;
}

std::wstring FormatProcessorNumber(ProcessorNumber id)
{
    return L"G" + std::to_wstring(id.group) + L":LP" + std::to_wstring(id.number);
}

std::wstring DescribeProcessor(ProcessorNumber id)
{
    return L"Group " + std::to_wstring(id.group) + L", processor " + std::to_wstring(id.number);
}

std::wstring DescribeGroupAffinity(uint16_t group, uint64_t mask)
{
    return L"Group " + std::to_wstring(group) + L", mask " + FormatHex(mask, 16);
}

std::wstring FormatMaskList(uint64_t mask)
{
    std::wstring text;
    unsigned bit = 0;
    while (bit < 64)
    {
        if (((mask >> bit) & 1) == 0)
        {
            ++bit;
            continue;
        }
        unsigned last = bit;
        while (last + 1 < 64 && ((mask >> (last + 1)) & 1) != 0)
        {
            ++last;
        }
        if (!text.empty())
        {
            text += L',';
        }
        text += std::to_wstring(bit);
        if (last > bit)
        {
            text += L'-';
            text += std::to_wstring(last);
        }
        bit = last + 1;
    }
    return text;
}

Result<uint64_t> ParseMaskList(std::wstring_view text, uint8_t maximum_processor_count)
{
    constexpr wchar_t kOperation[] = L"ParseMaskList";
    if (Trim(text).empty())
    {
        return ValidationError(kOperation, L"The processor list is empty.");
    }
    const unsigned limit = std::min<unsigned>(maximum_processor_count, kMaxGroupProcessors);
    uint64_t mask = 0;
    size_t start = 0;
    while (true)
    {
        const size_t comma = text.find(L',', start);
        const std::wstring_view item = text.substr(start, comma == std::wstring_view::npos ? std::wstring_view::npos : comma - start);
        const std::wstring trimmed = Trim(item);
        if (trimmed.empty())
        {
            return ValidationError(kOperation, L"The processor list has an empty item.");
        }
        const size_t dash = trimmed.find(L'-');
        const auto first = ParseUnsigned(std::wstring_view(trimmed).substr(0, dash));
        const auto last = dash == std::wstring::npos ? first : ParseUnsigned(std::wstring_view(trimmed).substr(dash + 1));
        if (!first || !last)
        {
            return ValidationError(kOperation, L"\"" + trimmed + L"\" is not a processor number or range.");
        }
        if (*first > *last)
        {
            return ValidationError(kOperation, L"Range \"" + trimmed + L"\" is reversed.");
        }
        if (*last >= limit)
        {
            return ValidationError(kOperation, L"Processor " + FormatUnsigned(*last) + L" is out of range. The group has " + FormatUnsigned(limit) + L" processor(s).");
        }
        mask |= RangeMask(static_cast<unsigned>(*first), static_cast<unsigned>(*last));
        if (comma == std::wstring_view::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return mask;
}

} // namespace thread_sim::platform

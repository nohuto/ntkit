// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/config.h"

#include "platform/error.h"
#include "platform/topology.h"

#include <win32ui/json/writer.h>

#include <optional>
#include <string>
#include <vector>

namespace thread_sim::engine
{

// processor and CPU Set identities require matching topology
struct TopologyFingerprint
{
    std::vector<uint64_t> group_masks; // active processor mask per group, in group order
    uint32_t cpu_set_count = 0;
    uint64_t cpu_set_hash = 0; // FNV-1a over each CPU Set (id, group, logical processor)

    bool operator==(const TopologyFingerprint&) const = default;
};

TopologyFingerprint FingerprintOf(const platform::Topology& topology);

struct LoadedConfig
{
    RunConfig config;
    std::optional<TopologyFingerprint> saved_topology;
    std::vector<std::wstring> warnings;
};

std::wstring SerializeRunConfig(const RunConfig& config, const platform::Topology& topology);
void WriteRunConfigObject(win32ui::JsonWriter& writer, std::wstring_view key, const RunConfig& config, const platform::Topology& topology);
platform::Result<LoadedConfig> ParseRunConfig(std::wstring_view text);

platform::Status SaveRunConfig(const std::wstring& path, const RunConfig& config, const platform::Topology& topology);
platform::Result<LoadedConfig> LoadRunConfig(const std::wstring& path);

} // namespace thread_sim::engine

// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/config.h"
#include "engine/snapshot.h"

#include "platform/environment.h"
#include "platform/error.h"
#include "platform/topology.h"

#include <string>

namespace thread_sim::engine
{

struct ExportInput
{
    const RunConfig* config = nullptr;
    const RunSnapshot* snapshot = nullptr;
    const platform::Topology* topology = nullptr;
    platform::Environment environment;
    std::wstring app_version;
};

inline constexpr wchar_t kResultsCsvSchema[] = L"thread-sim.thread-results.v5";
inline constexpr uint32_t kRunMetadataVersion = 5;

// exports requested settings, applied outcomes, notices and process measurements
// inactive rows have no applied or measured fields, empty measurements mean unavailable
std::wstring BuildThreadResultsCsv(const ExportInput& input);
std::wstring BuildRunMetadataJson(const ExportInput& input);
std::wstring CsvQuote(std::wstring_view field);

// writes both <base>.csv and <base>.json
platform::Status ExportResults(const std::wstring& base_path, const ExportInput& input);

} // namespace thread_sim::engine

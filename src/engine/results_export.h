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

std::wstring BuildThreadResultsCsv(const ExportInput& input);
std::wstring BuildRunMetadataJson(const ExportInput& input);
std::wstring CsvQuote(std::wstring_view field);

platform::Status ExportResults(const std::wstring& base_path, const ExportInput& input);

} // namespace thread_sim::engine

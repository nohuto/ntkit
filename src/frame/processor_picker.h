// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "platform/topology.h"

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thread_sim::frame
{

enum class PickerMode
{
    kGroupAffinity,   // logical processors of one group
    kCpuSets,         // CPU Set IDs of any group
    kSingleProcessor, // one logical processor
    kBrowse,          // read only topology view
};

struct PickerSelection
{
    uint16_t group = 0;
    uint64_t mask = 0;
    std::vector<uint32_t> selected_cpu_set_ids;
    platform::ProcessorNumber processor;
    bool has_processor = true;
};

struct PickerMessage
{
    bool error = false;
    std::wstring text;
};

struct PickerOptions
{
    PickerMode mode = PickerMode::kBrowse;
    std::wstring title;
    // single processor mode, Clear leaves no selection
    bool allow_none = false;
    // selection messages shown below the list
    std::function<std::vector<PickerMessage>(const PickerSelection&)> validate;
};

std::optional<PickerSelection> ShowProcessorPicker(HWND owner, const PickerOptions& options, const platform::Topology& topology, const PickerSelection& initial);
#if defined(THREAD_SIM_DEV)
// shown group count, extra groups copy the first real group
void SetDevProcessorGroups(uint16_t count);
#endif

} // namespace thread_sim::frame

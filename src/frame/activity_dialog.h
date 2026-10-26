// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/config.h"
#include "engine/validation.h"

#include "platform/topology.h"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace thread_sim::frame
{

// edits threads from the focused thread activity, true if OK changed config
// focus selects the initial field, kNone focuses OK
bool ShowActivityDialog(HWND owner, engine::RunConfig* config, const std::vector<uint32_t>& threads, int focused, engine::Field focus = engine::Field::kNone);

} // namespace thread_sim::frame

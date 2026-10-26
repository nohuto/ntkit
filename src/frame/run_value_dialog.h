// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/config.h"
#include "engine/validation.h"

#include "platform/topology.h"

#include <windows.h>

namespace thread_sim::frame
{

// edits kRunDuration or kSampleInterval, true if OK changed config
bool ShowRunValueDialog(HWND owner, engine::RunConfig* config, engine::Field field);

} // namespace thread_sim::frame

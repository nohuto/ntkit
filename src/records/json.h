// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <win32ui/json/reader.h>

#include <string>

namespace thread_sim::json
{

win32ui::JsonErrorSink Errors(std::wstring* destination);

}

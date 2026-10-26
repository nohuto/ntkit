// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

// message/About control IDs also serve as UI Automation AutomationIds
// GUI tests read them here, values match library message_dialog.h

#pragma once

namespace thread_sim::ui::ids
{

inline constexpr int kDialogMessage = 2001;
inline constexpr int kDialogDetails = 2002;
inline constexpr int kDialogCopy = 2003;
inline constexpr int kAboutLineFirst = 2010;

} // namespace thread_sim::ui::ids

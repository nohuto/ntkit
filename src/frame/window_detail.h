// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "frame/control_ids.h"

#include "resource.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

namespace thread_sim::frame::window_detail
{

inline constexpr UINT_PTR kReapTimer = 1;
inline constexpr UINT_PTR kCloseRetryTimer = 3;
inline constexpr UINT_PTR kPollTimer = 4;
inline constexpr UINT_PTR kApplyTimer = 5;
inline constexpr UINT_PTR kOtherThreadsTimer = 6;
inline constexpr uint32_t kIdleOtherThreadsMs = 1000;
inline constexpr double kMinSnapshotApplyMs = 100.0;
inline constexpr UINT kRunFailedMessage = WM_APP + 3;
inline constexpr UINT kSelectionMessage = WM_APP + 4;

struct ToolButton
{
    int command;
    const wchar_t* text;
    const wchar_t* tip;
    int icon;
};

inline constexpr ToolButton kToolButtons[] = {
    {cmd::kRunStart, L"Start", L"Start", IDI_TOOLBAR_START},
    {cmd::kRunSuspend, L"Suspend", L"Suspend", IDI_TOOLBAR_SUSPEND},
    {cmd::kRunResume, L"Resume", L"Resume", IDI_TOOLBAR_RESUME},
    {cmd::kRunStopSelected, L"Stop", L"Stop", IDI_TOOLBAR_STOP},
    {cmd::kRunStopAll, L"Stop all", L"Stop all", IDI_TOOLBAR_STOP_ALL},
    {0, nullptr, nullptr, 0},
    {cmd::kThreadAdd, L"Add", L"Add thread", IDI_TOOLBAR_ADD},
    {cmd::kThreadDuplicate, L"Duplicate", L"Duplicate", IDI_TOOLBAR_DUPLICATE},
    {cmd::kThreadRemove, L"Remove", L"Remove", IDI_TOOLBAR_REMOVE},
    {0, nullptr, nullptr, 0},
    {cmd::kViewTopology, L"Topology", L"Topology", IDI_TOOLBAR_TOPOLOGY},
    {cmd::kFileExport, L"Export", L"Export results", IDI_TOOLBAR_EXPORT},
};

double ElapsedMs(int64_t start);
std::optional<std::wstring> FileDialog(HWND owner, bool save, const wchar_t* title, const wchar_t* filter_name, const wchar_t* filter_spec, const wchar_t* extension, const std::wstring& initial_name, const std::wstring& folder);
std::wstring DirectoryOf(const std::wstring& path);
std::wstring FileNameOf(const std::wstring& path);

} // namespace thread_sim::frame::window_detail

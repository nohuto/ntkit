// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

// control IDs also serve as UI Automation AutomationIds, GUI tests rely on them

#pragma once

namespace thread_sim::frame::ids
{

inline constexpr int kToolbar = 1001;
inline constexpr int kStatusBar = 1002;
inline constexpr int kThreadTable = 1006;

inline constexpr int kPickerList = 2101;
inline constexpr int kPickerGroupCombo = 2102;
inline constexpr int kPickerSelectAll = 2104;
inline constexpr int kPickerClear = 2105;
inline constexpr int kPickerValidation = 2106;
inline constexpr int kPickerWarnings = 2107;

inline constexpr int kActivityWorkEdit = 2301;
inline constexpr int kActivityPeriodEdit = 2302;
inline constexpr int kActivityTimerCombo = 2303;

inline constexpr int kRunValueEdit = 2401;
inline constexpr int kTextPromptEdit = 2421;

inline constexpr int kDetailsList = 2503;

} // namespace thread_sim::frame::ids

namespace thread_sim::frame::cmd
{

inline constexpr int kFileNew = 40001;
inline constexpr int kFileOpen = 40002;
inline constexpr int kFileSave = 40003;
inline constexpr int kFileSaveAs = 40004;
inline constexpr int kFileExport = 40005;
inline constexpr int kFileExit = 40006;

inline constexpr int kEditCopy = 40101;
inline constexpr int kEditCopyIds = 40102;
inline constexpr int kEditSelectAll = 40103;

inline constexpr int kThreadAdd = 40201;
inline constexpr int kThreadDuplicate = 40202;
inline constexpr int kThreadRemove = 40203;
inline constexpr int kThreadMoveUp = 40204;
inline constexpr int kThreadMoveDown = 40205;
inline constexpr int kThreadRename = 40206;
inline constexpr int kThreadBoost = 40207;
inline constexpr int kThreadAffinity = 40208;
inline constexpr int kThreadCpuSets = 40209;
inline constexpr int kThreadIdeal = 40210;
inline constexpr int kThreadPriorityFirst = 40211; // + index into engine::kAllPriorities
inline constexpr int kThreadPriorityLast = 40215;
inline constexpr int kThreadActive = 40216;
inline constexpr int kThreadActivityFirst = 40217; // + engine::ActivityLevel, Low to Maximum
inline constexpr int kThreadActivityLast = 40220;
inline constexpr int kThreadActivityCustom = 40221;
inline constexpr int kThreadDetails = 40222;
inline constexpr int kThreadSharedMutex = 40223;

inline constexpr int kRunStart = 40301;
inline constexpr int kRunSuspend = 40302;
inline constexpr int kRunResume = 40303;
inline constexpr int kRunStopSelected = 40304;
inline constexpr int kRunStopAll = 40305;
inline constexpr int kRunDurationUnlimited = 40309;
inline constexpr int kRunDurationFirst = 40310; // + index into kDurationPresets
inline constexpr int kRunDurationLast = 40315;
inline constexpr int kRunDurationCustom = 40316;

inline constexpr int kThemeSystem = 40401;
inline constexpr int kThemeLight = 40402;
inline constexpr int kThemeDark = 40403;
inline constexpr int kViewTopology = 40404;
inline constexpr int kViewResetLayout = 40405;
inline constexpr int kViewRefresh = 40406;
inline constexpr int kViewShowAllThreads = 40407;
inline constexpr int kViewGridLines = 40408;
inline constexpr int kViewToolbar = 40409;
inline constexpr int kViewUpdateSpeedFirst = 40410; // + index into kUpdateSpeedPresets
inline constexpr int kViewUpdateSpeedLast = 40415;
inline constexpr int kViewUpdateSpeedCustom = 40416;
inline constexpr int kViewStatusBar = 40417;
inline constexpr int kThemeCustom = 40418;
inline constexpr int kThemePresets = 40419;

inline constexpr int kHelpAbout = 40501;

inline constexpr int kProcessPriorityFirst = 40701; // + index into kPriorityClasses
inline constexpr int kProcessPriorityLast = 40706;
inline constexpr int kProcessAffinity = 40710;
inline constexpr int kProcessCpuSets = 40711;

// dev builds only
inline constexpr int kDevSimulateDpiFirst = 40621; // 96, 120, 144, 192
inline constexpr int kDevSimulateDpiLast = 40624;

} // namespace thread_sim::frame::cmd

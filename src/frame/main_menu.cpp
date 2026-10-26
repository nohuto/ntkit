// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/main_menu.h"

#include "frame/control_ids.h"
#include "frame/formatting.h"

#include "engine/config.h"
#include "engine/snapshot.h"

#include <win32ui/menu/popup_menu.h>

#include <commctrl.h>

namespace thread_sim::frame
{

namespace
{

void Append(HMENU menu, int command, const wchar_t* text)
{
    win32ui::AppendMenuItem(menu, MF_STRING, static_cast<UINT_PTR>(command), text, ShortcutText(command));
}

void Separator(HMENU menu)
{
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
}

void AppendRadio(HMENU menu, int command, const std::wstring& text)
{
    win32ui::AppendRadioItem(menu, static_cast<UINT_PTR>(command), text.c_str());
}

constexpr win32ui::Shortcut kShortcuts[] = {
    {cmd::kFileNew, FCONTROL, 'N', L"Ctrl+N"},
    {cmd::kFileOpen, FCONTROL, 'O', L"Ctrl+O"},
    {cmd::kFileSave, FCONTROL, 'S', L"Ctrl+S"},
    {cmd::kFileSaveAs, FCONTROL | FSHIFT, 'S', L"Ctrl+Shift+S"},
    {cmd::kFileExport, FCONTROL, 'E', L"Ctrl+E"},
    {cmd::kEditCopy, FCONTROL, 'C', L"Ctrl+C"},
    {cmd::kEditCopyIds, FCONTROL | FSHIFT, 'C', L"Ctrl+Shift+C"},
    {cmd::kEditSelectAll, FCONTROL, 'A', L"Ctrl+A"},
    {cmd::kThreadAdd, 0, VK_INSERT, L"Ins"},
    {cmd::kThreadDuplicate, FCONTROL, 'D', L"Ctrl+D"},
    {cmd::kThreadRemove, 0, VK_DELETE, L"Del"},
    {cmd::kThreadMoveUp, FALT, VK_UP, L"Alt+Up"},
    {cmd::kThreadMoveDown, FALT, VK_DOWN, L"Alt+Down"},
    {cmd::kThreadRename, 0, VK_F2, L"F2"},
    {cmd::kThreadDetails, FALT, VK_RETURN, L"Alt+Enter"},
    // F5 matches Explorer and Task Manager, run shortcuts follow Visual Studio
    {cmd::kViewRefresh, 0, VK_F5, L"F5"},
    {cmd::kRunStart, FCONTROL, VK_F5, L"Ctrl+F5"},
    {cmd::kRunSuspend, 0, VK_F7, L"F7"},
    {cmd::kRunResume, 0, VK_F8, L"F8"},
    {cmd::kRunStopSelected, FCONTROL | FSHIFT, VK_F5, L"Ctrl+Shift+F5"},
    {cmd::kRunStopAll, FSHIFT, VK_F5, L"Shift+F5"},
};

} // namespace

const wchar_t* ShortcutText(int command)
{
    // table handles Space, an accelerator would steal it from edits
    if (command == cmd::kThreadActive)
    {
        return L"Space";
    }
    return win32ui::ShortcutText(kShortcuts, command);
}

std::wstring ActivityMenuText(int command)
{
    if (command == cmd::kThreadActivityCustom)
    {
        return L"&Custom...";
    }
    static const wchar_t* const kTexts[] = {L"&Low", L"&Medium", L"&Busy", L"Ma&ximum"};
    const int index = command - cmd::kThreadActivityFirst;
    return std::wstring(kTexts[index]) + L" (" + std::to_wstring(engine::ActivityLevelWork(static_cast<engine::ActivityLevel>(index))) + L" %)";
}

std::wstring CustomValueMenuText(const std::wstring& value)
{
    return value.empty() ? L"&Custom..." : L"&Custom (" + value + L")...";
}

void MainMenu::Build(HWND window)
{
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    Append(file, cmd::kFileNew, L"&New");
    Append(file, cmd::kFileOpen, L"&Open...");
    Append(file, cmd::kFileSave, L"&Save");
    Append(file, cmd::kFileSaveAs, L"Save &as...");
    Separator(file);
    Append(file, cmd::kFileExport, L"&Export results...");
    Separator(file);
    Append(file, cmd::kFileExit, L"E&xit");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");

    HMENU edit = CreatePopupMenu();
    Append(edit, cmd::kEditCopy, L"&Copy");
    Append(edit, cmd::kEditCopyIds, L"Copy &TIDs");
    Append(edit, cmd::kEditSelectAll, L"Select &all");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");

    HMENU threads = CreatePopupMenu();
    Append(threads, cmd::kThreadAdd, L"&Add");
    Append(threads, cmd::kThreadDuplicate, L"&Duplicate");
    Append(threads, cmd::kThreadRemove, L"&Remove");
    Separator(threads);
    Append(threads, cmd::kThreadMoveUp, L"Move &up");
    Append(threads, cmd::kThreadMoveDown, L"Move do&wn");
    Separator(threads);
    Append(threads, cmd::kThreadRename, L"Re&name");
    Append(threads, cmd::kThreadActive, L"Activ&e");
    HMENU activity = CreatePopupMenu();
    for (int command = cmd::kThreadActivityFirst; command <= cmd::kThreadActivityLast; ++command)
    {
        AppendRadio(activity, command, ActivityMenuText(command));
    }
    Separator(activity);
    AppendRadio(activity, cmd::kThreadActivityCustom, ActivityMenuText(cmd::kThreadActivityCustom));
    AppendMenuW(threads, MF_POPUP, reinterpret_cast<UINT_PTR>(activity), L"Activit&y");
    HMENU priority = CreatePopupMenu();
    for (size_t index = 0; index < std::size(engine::kAllRelativeThreadPriorities); ++index)
    {
        AppendRadio(priority, cmd::kThreadPriorityFirst + static_cast<int>(index), engine::DescribeRelativePriority(static_cast<int32_t>(engine::kAllRelativeThreadPriorities[index])));
    }
    AppendMenuW(threads, MF_POPUP, reinterpret_cast<UINT_PTR>(priority), L"&Priority");
    Append(threads, cmd::kThreadBoost, L"Priority &boost");
    Append(threads, cmd::kThreadSharedMutex, L"Shared &mutex");
    Append(threads, cmd::kThreadAffinity, L"A&ffinity...");
    Append(threads, cmd::kThreadCpuSets, L"CPU &Sets...");
    Append(threads, cmd::kThreadIdeal, L"&Ideal processor...");
    Separator(threads);
    Append(threads, cmd::kThreadDetails, L"De&tails");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(threads), L"&Threads");

    HMENU run = CreatePopupMenu();
    Append(run, cmd::kRunStart, L"&Start");
    Append(run, cmd::kRunSuspend, L"S&uspend");
    Append(run, cmd::kRunResume, L"&Resume");
    Append(run, cmd::kRunStopSelected, L"S&top");
    Append(run, cmd::kRunStopAll, L"Stop &all");
    Separator(run);
    static_assert(std::size(kDurationPresets) == cmd::kRunDurationLast - cmd::kRunDurationFirst + 1);
    duration_ = CreatePopupMenu();
    AppendRadio(duration_, cmd::kRunDurationUnlimited, L"&Unlimited");
    for (size_t index = 0; index < std::size(kDurationPresets); ++index)
    {
        AppendRadio(duration_, cmd::kRunDurationFirst + static_cast<int>(index), DescribeDuration(kDurationPresets[index]));
    }
    Separator(duration_);
    AppendRadio(duration_, cmd::kRunDurationCustom, CustomValueMenuText({}));
    AppendMenuW(run, MF_POPUP, reinterpret_cast<UINT_PTR>(duration_), L"&Duration");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(run), L"&Run");

    HMENU process = CreatePopupMenu();
    HMENU priority_class = CreatePopupMenu();
    static_assert(std::size(kPriorityClasses) == cmd::kProcessPriorityLast - cmd::kProcessPriorityFirst + 1);
    static const wchar_t* const kClassTexts[] = {L"&Idle", L"&Below normal", L"&Normal", L"&Above normal", L"&High", L"&Realtime"};
    for (int index = 0; index < static_cast<int>(std::size(kPriorityClasses)); ++index)
    {
        AppendRadio(priority_class, cmd::kProcessPriorityFirst + index, kClassTexts[index]);
    }
    AppendMenuW(process, MF_POPUP, reinterpret_cast<UINT_PTR>(priority_class), L"&Priority Class");
    Append(process, cmd::kProcessAffinity, L"&Affinity...");
    Append(process, cmd::kProcessCpuSets, L"CPU &Sets...");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(process), L"&Process");

    HMENU view = CreatePopupMenu();
    Append(view, cmd::kViewRefresh, L"Re&fresh");
    Append(view, cmd::kViewShowAllThreads, L"Show &all threads");
    Append(view, cmd::kViewGridLines, L"&Grid lines");
    Append(view, cmd::kViewToolbar, L"Tool&bar");
    Append(view, cmd::kViewStatusBar, L"&Status bar");
    Separator(view);
    HMENU theme = CreatePopupMenu();
    AppendRadio(theme, cmd::kThemeSystem, L"&System");
    AppendRadio(theme, cmd::kThemeLight, L"&Light");
    AppendRadio(theme, cmd::kThemeDark, L"&Dark");
    AppendRadio(theme, cmd::kThemeCustom, L"&Custom");
    Separator(theme);
    Append(theme, cmd::kThemePresets, L"Theme &presets...");
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(theme), L"&Theme");
    static_assert(std::size(kUpdateSpeedPresets) == cmd::kViewUpdateSpeedLast - cmd::kViewUpdateSpeedFirst + 1);
    HMENU speed = CreatePopupMenu();
    for (size_t index = 0; index < std::size(kUpdateSpeedPresets); ++index)
    {
        AppendRadio(speed, cmd::kViewUpdateSpeedFirst + static_cast<int>(index), DescribeInterval(kUpdateSpeedPresets[index]));
    }
    Separator(speed);
    AppendRadio(speed, cmd::kViewUpdateSpeedCustom, CustomValueMenuText({}));
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(speed), L"&Update speed");
    Append(view, cmd::kViewTopology, L"T&opology...");
    Separator(view);
    Append(view, cmd::kViewResetLayout, L"&Reset layout");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");

    HMENU help = CreatePopupMenu();
    Append(help, cmd::kHelpAbout, L"&About ThreadSim");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");

#if defined(THREAD_SIM_DEV)
    {
        HMENU dev = CreatePopupMenu();
        HMENU dpi = CreatePopupMenu();
        const wchar_t* dpi_names[] = {L"96 (100%)", L"120 (125%)", L"144 (150%)", L"192 (200%)"};
        for (int index = 0; index < 4; ++index)
        {
            AppendMenuW(dpi, MF_STRING, static_cast<UINT_PTR>(cmd::kDevSimulateDpiFirst + index), dpi_names[index]);
        }
        AppendMenuW(dev, MF_POPUP, reinterpret_cast<UINT_PTR>(dpi), L"Simulate &DPI");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(dev), L"&Dev");
    }
#endif

    bar_.Attach(window, bar);
    shortcuts_.Create(kShortcuts);
}

bool ShouldBypassAccelerator(const MSG& message)
{
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN)
    {
        return false;
    }
    HWND focus = GetFocus();
    wchar_t name[32] = {};
    if (!focus || !GetClassNameW(focus, name, static_cast<int>(std::size(name))) || _wcsicmp(name, WC_EDITW) != 0)
    {
        return false;
    }
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    switch (message.wParam)
    {
    case VK_DELETE:
    case VK_INSERT:
    case VK_UP:
    case VK_DOWN:
        return !alt || message.wParam == VK_DELETE || message.wParam == VK_INSERT;
    case 'A':
    case 'C':
    case 'V':
    case 'X':
    case 'Z':
        return control;
    default:
        return false;
    }
}

} // namespace thread_sim::frame

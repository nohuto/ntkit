// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <win32ui/menu/menu_bar.h>
#include <win32ui/menu/shortcuts.h>

#include <windows.h>

#include <cstdint>
#include <string>

namespace thread_sim::frame
{

const wchar_t* ShortcutText(int command);
// Activity submenu label and access key for a level or custom command
std::wstring ActivityMenuText(int command);

// Run > Duration seconds and View > Update speed milliseconds, in menu order
inline constexpr uint32_t kDurationPresets[] = {10, 30, 60, 300, 900, 3600};
inline constexpr uint32_t kUpdateSpeedPresets[] = {100, 250, 500, 1000, 2000, 5000};
// Custom label with current value if no preset matches, otherwise value is empty
std::wstring CustomValueMenuText(const std::wstring& value);

class MainMenu
{
  public:
    void Build(HWND window);
    win32ui::MenuBar& bar() noexcept
    {
        return bar_;
    }
    const win32ui::ShortcutTable& shortcuts() const noexcept
    {
        return shortcuts_;
    }
    // Run > Duration popup, also used by the Start arrow
    HMENU duration_menu() const noexcept
    {
        return duration_;
    }

  private:
    win32ui::MenuBar bar_;
    win32ui::ShortcutTable shortcuts_;
    HMENU duration_ = nullptr;
};

// keys that must reach a focused text box instead of a menu accelerator
bool ShouldBypassAccelerator(const MSG& message);

} // namespace thread_sim::frame

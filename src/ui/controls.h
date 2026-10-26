// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ui/theme.h"

#include <win32ui/accessible_name.h>
#include <win32ui/gdi/fonts.h>

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace thread_sim::ui
{

using win32ui::ReleaseAccessibility;
using win32ui::SetAccessibleName;

void SetFontTree(HWND root, HFONT font);
void ApplyFonts(HWND root, const win32ui::FontSet& fonts);
std::wstring GetText(HWND hwnd);
void SetTextIfChanged(HWND hwnd, std::wstring_view text);
void SetEnabled(HWND hwnd, bool enabled);
void SetVisible(HWND hwnd, bool visible);
// selected rows in ascending order, stops if the control stops advancing, including on destruction
std::vector<int> SelectedRows(HWND list);
// numbers compare by value, other text ignores case
int CompareCellText(const std::wstring& left, const std::wstring& right);
int ComboIndex(HWND combo);
void SetComboIndex(HWND combo, int index);

} // namespace thread_sim::ui

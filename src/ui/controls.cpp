// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/controls.h"

#include <win32ui/gdi/fonts.h>

#include <commctrl.h>

#include <algorithm>
#include <cwctype>

namespace thread_sim::ui
{

void SetFontTree(HWND root, HFONT font)
{
    SendMessageW(root, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    EnumChildWindows(
        root,
        [](HWND child, LPARAM parameter) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(parameter), FALSE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(font)
    );
}

void ApplyFonts(HWND root, const win32ui::FontSet& fonts)
{
    SetFontTree(root, fonts.ui());
}

std::wstring GetText(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0)
    {
        return {};
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(std::max(copied, 0)));
    return text;
}

void SetTextIfChanged(HWND hwnd, std::wstring_view text)
{
    if (hwnd && GetText(hwnd) != text)
    {
        const std::wstring copy(text);
        SetWindowTextW(hwnd, copy.c_str());
    }
}

void SetEnabled(HWND hwnd, bool enabled)
{
    if (hwnd && (IsWindowEnabled(hwnd) != FALSE) != enabled)
    {
        EnableWindow(hwnd, enabled);
    }
}

void SetVisible(HWND hwnd, bool visible)
{
    // check style bit, IsWindowVisible is false for all children of hidden parents
    if (hwnd && ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE) != 0) != visible)
    {
        ShowWindow(hwnd, visible ? SW_SHOWNA : SW_HIDE);
    }
}

std::vector<int> SelectedRows(HWND list)
{
    std::vector<int> rows;
    if (!list || !IsWindow(list))
    {
        return rows;
    }
    const int count = ListView_GetItemCount(list);
    int row = -1;
    while (static_cast<int>(rows.size()) < count)
    {
        const int next = ListView_GetNextItem(list, row, LVNI_SELECTED);
        if (next <= row)
        {
            break;
        }
        rows.push_back(next);
        row = next;
    }
    return rows;
}

int CompareCellText(const std::wstring& left, const std::wstring& right)
{
    const auto parse = [](const std::wstring& text, double* value) {
        size_t start = 0;
        while (start < text.size() && text[start] == L' ')
        {
            ++start;
        }
        if (start == text.size() || !(std::iswdigit(text[start]) || ((text[start] == L'-' || text[start] == L'.') && start + 1 < text.size() && std::iswdigit(text[start + 1]))))
        {
            return false;
        }
        wchar_t* end = nullptr;
        *value = wcstod(text.c_str() + start, &end);
        return end != text.c_str() + start;
    };
    double a = 0;
    double b = 0;
    const bool left_number = parse(left, &a);
    const bool right_number = parse(right, &b);
    if (left_number && right_number && a != b)
    {
        return a < b ? -1 : 1;
    }
    if (left_number != right_number)
    {
        // sort empty and text cells after numbers
        return left_number ? -1 : 1;
    }
    return _wcsicmp(left.c_str(), right.c_str());
}

int ComboIndex(HWND combo)
{
    return static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
}

void SetComboIndex(HWND combo, int index)
{
    if (combo && ComboIndex(combo) != index)
    {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
        InvalidateRect(combo, nullptr, FALSE);
    }
}

} // namespace thread_sim::ui

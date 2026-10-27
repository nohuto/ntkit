// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/text_prompt.h"

#include "frame/control_ids.h"

#include "ui/controls.h"

#include "resource.h"

#include <win32ui/dialog/dialog_layout.h>
#include <win32ui/dialog/dialog_window.h>

namespace thread_sim::frame
{

namespace
{

static_assert(IDC_TEXT_PROMPT_EDIT == ids::kTextPromptEdit);

class TextPromptDialog final : public win32ui::DialogWindow
{
  public:
    TextPromptDialog(std::wstring_view title, std::wstring_view label, std::wstring text)
        : title_(title), label_(label), text_(std::move(text))
    {
    }

    const std::wstring& text() const
    {
        return text_;
    }

  protected:
    HWND initial_focus() const override
    {
        return edit_;
    }

    void OnCreate() override
    {
        SetWindowTextW(hwnd(), title_.c_str());
        SetDlgItemTextW(hwnd(), IDC_TEXT_PROMPT_LABEL, label_.c_str());
        win32ui::FitDialogText(hwnd());
        edit_ = GetDlgItem(hwnd(), ids::kTextPromptEdit);
        ui::SetAccessibleName(edit_, label_);
        SetWindowTextW(edit_, text_.c_str());
        SendMessageW(edit_, EM_SETSEL, 0, -1);
    }

    void OnLayout(int, int) override
    {
    }

    bool OnCommand(int id, int, HWND) override
    {
        if (id != IDOK)
        {
            return false;
        }
        text_ = ui::GetText(edit_);
        End(IDOK);
        return true;
    }

  private:
    std::wstring title_;
    std::wstring label_;
    std::wstring text_;
    HWND edit_ = nullptr;
};

} // namespace

bool PromptText(HWND owner, std::wstring_view title, std::wstring_view label, std::wstring* text)
{
    TextPromptDialog dialog(title, label, *text);
    if (dialog.RunModal(owner, MAKEINTRESOURCEW(IDD_TEXT_PROMPT)) != IDOK)
    {
        return false;
    }
    *text = dialog.text();
    return true;
}

} // namespace thread_sim::frame

// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/dialog.h"

#include "ui/dialog_ids.h"

namespace thread_sim::ui
{

static_assert(ids::kDialogMessage == win32ui::kMessageTextId && ids::kDialogDetails == win32ui::kMessageDetailsId && ids::kDialogCopy == win32ui::kMessageCopyId && ids::kAboutLineFirst == win32ui::kAboutFirstRowId);

void RegisterDialogClass(HINSTANCE instance, int icon)
{
    win32ui::RegisterDialogClass(instance, kDialogClass, DialogThemeHooks(), icon);
}

INT_PTR ShowMessage(HWND owner, win32ui::MessageSpec spec)
{
    spec.copy_text = L"&Copy details";
    return win32ui::ShowMessage(owner, spec);
}

void ShowAbout(HWND owner, std::wstring_view title, std::vector<std::wstring> lines)
{
    win32ui::AboutSpec spec;
    spec.title = title;
    spec.rows = std::move(lines);
    spec.ok_text = L"OK";
    win32ui::ShowAbout(owner, spec);
}

void ShowError(HWND owner, std::wstring_view title, std::wstring_view summary, const platform::Error& error)
{
    win32ui::MessageSpec spec;
    spec.title = title;
    spec.message = summary;
    spec.details = error.Describe();
    if (!error.operation.empty())
    {
        spec.details += L"\n\nOperation: " + error.operation;
    }
    if (!error.context.empty())
    {
        spec.details += L"\nContext: " + error.context;
    }
    const std::wstring code = error.CodeText();
    if (!code.empty())
    {
        spec.details += L"\nCode: " + code;
    }
    spec.buttons = {{IDOK, L"OK"}};
    spec.default_id = IDOK;
    spec.cancel_id = IDOK;
    // qualify to avoid finding win32ui::ShowMessage through argument dependent lookup
    ui::ShowMessage(owner, std::move(spec));
}

SaveChoice AskSaveChanges(HWND owner, std::wstring_view document_name)
{
    win32ui::MessageSpec spec;
    spec.title = L"ThreadSim";
    spec.message = L"Save changes to " + std::wstring(document_name) + L"?";
    spec.buttons = {{IDYES, L"&Save"}, {IDNO, L"Do&n't save"}, {IDCANCEL, L"Cancel"}};
    spec.default_id = IDYES;
    spec.cancel_id = IDCANCEL;
    const INT_PTR result = ui::ShowMessage(owner, std::move(spec));
    return result == IDYES ? SaveChoice::kSave : result == IDNO ? SaveChoice::kDiscard
                                                                : SaveChoice::kCancel;
}

} // namespace thread_sim::ui

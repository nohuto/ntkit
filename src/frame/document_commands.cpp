// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/main_window.h"

#include "frame/window_detail.h"

#include "ui/dialog.h"

#include "engine/config_io.h"
#include "engine/results_export.h"

#include "platform/environment.h"

#include <shobjidl.h>

namespace thread_sim::frame
{

using namespace ui;
using namespace window_detail;

std::optional<std::wstring> window_detail::FileDialog(HWND owner, bool save, const wchar_t* title, const wchar_t* filter_name, const wchar_t* filter_spec, const wchar_t* extension, const std::wstring& initial_name, const std::wstring& folder)
{
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        return std::nullopt;
    }
    const COMDLG_FILTERSPEC filters[] = {{filter_name, filter_spec}, {L"All files", L"*.*"}};
    dialog->SetFileTypes(2, filters);
    dialog->SetTitle(title);
    dialog->SetDefaultExtension(extension);
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    if (!initial_name.empty())
    {
        dialog->SetFileName(initial_name.c_str());
    }
    if (!folder.empty())
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item))))
        {
            dialog->SetFolder(item);
            item->Release();
        }
    }
    std::optional<std::wstring> result;
    const win32ui::ModalScope modal;
    if (SUCCEEDED(dialog->Show(owner)))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::wstring window_detail::DirectoryOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring window_detail::FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool MainWindow::ConfirmDiscard()
{
    if (config_ == saved_config_)
    {
        return true;
    }
    switch (AskSaveChanges(hwnd_, DocumentName()))
    {
    case SaveChoice::kSave:
        return SaveDocument(false);
    case SaveChoice::kDiscard:
        return true;
    default:
        return false;
    }
}

void MainWindow::RequestClose()
{
    // defer close requests until modal UI ends, nested modal loops must not use destroyed windows
    if (win32ui::ModalDepth() > 0)
    {
        win32ui::CancelOpenDialogs();
        SetTimer(hwnd_, kCloseRetryTimer, 50, nullptr);
        return;
    }
    if (close_pending_)
    {
        if (!RunBusy())
        {
            SaveSettings();
            DestroyWindow(hwnd_);
        }
        return;
    }
    if (!ConfirmDiscard())
    {
        return;
    }
    if (RunBusy())
    {
        close_pending_ = true;
        controller_->RequestStopAll();
        UpdateStatus();
        SetTimer(hwnd_, kReapTimer, 20, nullptr);
        return;
    }
    SaveSettings();
    DestroyWindow(hwnd_);
}

void MainWindow::NewDocument()
{
    if (!ConfirmDiscard())
    {
        return;
    }
    config_ = engine::MakeDefaultRun();
    saved_config_ = config_;
    document_path_.clear();
    ResetThreadKeys();
    Revalidate();
    table_.SetData(&config_, nullptr);
    results_stale_ = snapshot_ != nullptr;
    table_.RefreshAll();
    table_.SelectThread(0, true);
    UpdateDetails();
    UpdateTitle();
    FollowSampleInterval();
}

void MainWindow::OpenDocument(const std::wstring& path)
{
    auto loaded = engine::LoadRunConfig(path);
    if (!loaded)
    {
        ShowError(hwnd_, L"Open config", L"The config could not be opened.", loaded.error());
        return;
    }
    config_ = loaded->config;
    saved_config_ = config_;
    document_path_ = path;
    settings_.last_directory = DirectoryOf(path);
    ResetThreadKeys();
    Revalidate();
    results_stale_ = snapshot_ != nullptr;
    table_.SetData(&config_, nullptr);
    table_.RefreshAll();
    table_.SelectThread(0, true);
    UpdateDetails();
    UpdateTitle();
    FollowSampleInterval();
    std::wstring notes;
    for (const auto& warning : loaded->warnings)
    {
        notes += warning + L"\n";
    }
    if (loaded->saved_topology && !(*loaded->saved_topology == engine::FingerprintOf(environment_.topology)))
    {
        notes += L"The config was saved on a machine with another processor topology, check its processor numbers and CPU Set IDs.\n";
    }
    if (!notes.empty())
    {
        win32ui::MessageSpec spec;
        spec.title = L"Open config";
        spec.message = L"Opened with notes.";
        spec.details = notes;
        spec.buttons = {{IDOK, L"OK"}};
        ui::ShowMessage(hwnd_, std::move(spec));
    }
    SetStatusMessage(L"Opened " + FileNameOf(path));
}

bool MainWindow::SaveDocument(bool save_as)
{
    std::wstring path = document_path_;
    if (save_as || path.empty())
    {
        const std::wstring suggested = (config_.name.empty() ? std::wstring(L"thread-sim-run") : config_.name) + L".jsonc";
        std::wstring sanitized;
        for (wchar_t ch : suggested)
        {
            sanitized.push_back(wcschr(L"\\/:*?\"<>|", ch) ? L'_' : ch);
        }
        const auto chosen = FileDialog(hwnd_, true, L"Save config", L"ThreadSim config (*.jsonc)", L"*.jsonc", L"jsonc", sanitized, settings_.last_directory);
        if (!chosen)
        {
            return false;
        }
        path = *chosen;
    }
    const platform::Status status = engine::SaveRunConfig(path, config_, environment_.topology);
    if (!status)
    {
        ShowError(hwnd_, L"Save config", L"The config could not be saved.", status.error());
        return false;
    }
    document_path_ = path;
    saved_config_ = config_;
    settings_.last_directory = DirectoryOf(path);
    UpdateTitle();
    SetStatusMessage(L"Saved " + FileNameOf(path));
    return true;
}

void MainWindow::ExportResults()
{
    if (!snapshot_ || !snapshot_->final)
    {
        return;
    }
    const std::wstring suggested = L"thread-sim-" + snapshot_->run_id + L".csv";
    const auto chosen = FileDialog(hwnd_, true, L"Export results", L"CSV results (*.csv)", L"*.csv", L"csv", suggested, settings_.last_directory);
    if (!chosen)
    {
        return;
    }
    std::wstring base = *chosen;
    if (base.size() > 4 && _wcsicmp(base.c_str() + base.size() - 4, L".csv") == 0)
    {
        base.resize(base.size() - 4);
    }
    // save dialog confirmed only the chosen file, export also writes adjacent metadata
    std::wstring existing;
    int existing_count = 0;
    for (const std::wstring& path : {base + L".csv", base + L".json"})
    {
        if (_wcsicmp(path.c_str(), chosen->c_str()) != 0 && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            existing += (existing.empty() ? L"" : L" and ") + FileNameOf(path);
            ++existing_count;
        }
    }
    if (existing_count > 0)
    {
        win32ui::MessageSpec spec;
        spec.title = L"Export results";
        spec.message = existing + (existing_count == 1 ? L" already exists. Replace it?" : L" already exist. Replace them?");
        spec.buttons = {{IDYES, L"&Replace"}, {IDCANCEL, L"Cancel"}};
        spec.default_id = IDCANCEL;
        spec.cancel_id = IDCANCEL;
        if (ui::ShowMessage(hwnd_, std::move(spec)) != IDYES || !hwnd_)
        {
            return;
        }
    }
    engine::ExportInput input;
    input.config = &run_config_;
    input.snapshot = snapshot_.get();
    input.topology = &environment_.topology;
    // export uses environment captured at Start, query again only if missing
    if (!snapshot_ || !snapshot_->environment)
    {
        input.environment = platform::QueryEnvironment();
    }
    input.app_version = THREAD_SIM_VERSION_TEXT;
    const platform::Status status = engine::ExportResults(base, input);
    if (!status)
    {
        ShowError(hwnd_, L"Export results", L"The results could not be exported.", status.error());
        return;
    }
    settings_.last_directory = DirectoryOf(base);
    SetStatusMessage(L"Exported " + FileNameOf(base) + L".csv and .json");
}

} // namespace thread_sim::frame

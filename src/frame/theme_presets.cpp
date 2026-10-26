// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/main_window.h"

#include "frame/text_prompt.h"
#include "frame/window_detail.h"

#include "ui/dialog.h"
#include "ui/theme.h"

#include <win32ui/theme/preset_editor.h>

namespace thread_sim::frame
{

using namespace window_detail;

namespace
{

constexpr wchar_t kPresetFile[] = L"theme_presets.rktheme";

const wchar_t* PresetEditorText(win32ui::PresetEditorText text)
{
    using Text = win32ui::PresetEditorText;
    switch (text)
    {
    case Text::title:
        return L"Theme presets";
    case Text::presets:
        return L"Presets";
    case Text::colors:
        return L"Colors";
    case Text::templates:
        return L"Templates";
    case Text::color_column:
        return L"Color";
    case Text::hex_column:
        return L"Hex";
    case Text::dark:
        return L"Dark theme";
    case Text::new_preset:
        return L"New...";
    case Text::duplicate:
        return L"Duplicate";
    case Text::rename:
        return L"Rename...";
    case Text::delete_preset:
        return L"Delete";
    case Text::import_presets:
        return L"Import...";
    case Text::export_presets:
        return L"Export...";
    case Text::edit_color:
        return L"Edit color...";
    case Text::apply_template:
        return L"Apply template";
    case Text::apply:
        return L"Apply";
    case Text::ok:
        return L"OK";
    case Text::cancel:
        return L"Cancel";
    // editing a built in creates a new preset
    case Text::new_title:
    case Text::copy_title:
        return L"New preset";
    case Text::rename_title:
        return L"Rename preset";
    case Text::copy_suffix:
        return L" copy";
    case Text::background:
        return L"Background";
    case Text::panel:
        return L"Panel";
    case Text::surface:
        return L"Surface";
    case Text::field:
        return L"Field";
    case Text::header:
        return L"Header";
    case Text::border:
        return L"Border";
    case Text::text:
        return L"Text";
    case Text::muted_text:
        return L"Muted text";
    case Text::accent:
        return L"Accent";
    case Text::selection:
        return L"Selection";
    case Text::selection_text:
        return L"Selection text";
    case Text::hover:
        return L"Hover";
    case Text::focus:
        return L"Focus";
    }
    return L"";
}

std::wstring ProblemText(const win32ui::ThemePresetResult& result)
{
    const wchar_t* problem = L"The file could not be read.";
    switch (result.error)
    {
    case win32ui::ThemePresetError::too_large:
        problem = L"The file is larger than 4 MiB.";
        break;
    case win32ui::ThemePresetError::bad_encoding:
        problem = L"The file is not text in a known encoding.";
        break;
    case win32ui::ThemePresetError::missing_name:
        problem = L"A preset has no name.";
        break;
    case win32ui::ThemePresetError::bad_line:
        problem = L"A line is not a valid entry.";
        break;
    case win32ui::ThemePresetError::write_failed:
        problem = L"The file could not be written.";
        break;
    default:
        break;
    }
    return result.line > 0 ? std::wstring(problem) + L" Line " + std::to_wstring(result.line) + L"." : std::wstring(problem);
}

void ShowPresetProblem(HWND owner, std::wstring_view message, const std::wstring& path, const win32ui::ThemePresetResult& result)
{
    win32ui::MessageSpec spec;
    spec.title = L"ThreadSim";
    spec.message = message;
    spec.details = ProblemText(result) + L"\n\nFile: " + path;
    spec.buttons = {{IDOK, L"OK"}};
    spec.cancel_id = IDOK;
    spec.icon = IDI_WARNING;
    ui::ShowMessage(owner, std::move(spec));
}

// create missing files only, leave other access failures alone
bool FileMissing(const std::wstring& path)
{
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

} // namespace

std::wstring MainWindow::ThemePresetsPath() const
{
    return environment_.data_dir.empty() ? std::wstring() : environment_.data_dir + L"\\" + kPresetFile;
}

void MainWindow::LoadThemePresets()
{
    if (presets_loaded_)
    {
        return;
    }
    presets_loaded_ = true;
    const std::wstring path = ThemePresetsPath();
    const win32ui::ThemePresetResult result = path.empty() ? win32ui::ThemePresetResult{win32ui::ThemePresetError::read_failed} : win32ui::ReadThemePresets(path, &theme_presets_);
    if (result.error == win32ui::ThemePresetError::none && !theme_presets_.empty())
    {
        if (win32ui::MergeBuiltInThemePresets(&theme_presets_, settings_.theme_preset))
        {
            SaveThemePresets();
        }
        return;
    }
    theme_presets_ = win32ui::BuiltInThemePresets();
    // unreadable files stay unchanged until presets are saved
    if (!path.empty() && result.error == win32ui::ThemePresetError::read_failed && FileMissing(path))
    {
        SaveThemePresets();
    }
    else if (!path.empty())
    {
        presets_problem_ = result;
    }
}

void MainWindow::ShowThemePresetsProblem()
{
    if (hwnd_ && presets_problem_)
    {
        const win32ui::ThemePresetResult problem = *presets_problem_;
        presets_problem_.reset();
        ShowPresetProblem(hwnd_, L"The theme presets file could not be read, the built-in presets are used until the presets are saved.", ThemePresetsPath(), problem);
    }
    if (hwnd_ && presets_save_problem_)
    {
        const win32ui::ThemePresetResult problem = *presets_save_problem_;
        presets_save_problem_.reset();
        ShowPresetProblem(hwnd_, L"The theme presets could not be saved.", ThemePresetsPath(), problem);
    }
}

void MainWindow::SaveThemePresets()
{
    const std::wstring path = ThemePresetsPath();
    if (path.empty())
    {
        return;
    }
    const win32ui::ThemePresetResult result = win32ui::WriteThemePresets(path, theme_presets_);
    if (result.error == win32ui::ThemePresetError::none)
    {
        return;
    }
    // startup load saves before window creation
    if (hwnd_)
    {
        ShowPresetProblem(hwnd_, L"The theme presets could not be saved.", path, result);
    }
    else
    {
        presets_save_problem_ = result;
    }
}

void MainWindow::ApplyThemePreset(const std::wstring& name)
{
    LoadThemePresets();
    const win32ui::ThemePreset* preset = win32ui::FindThemePreset(theme_presets_, name);
    settings_.theme = win32ui::ThemeMode::custom;
    settings_.theme_preset = preset ? preset->name : name;
    win32ui::SetThemeMode(win32ui::ThemeMode::custom, preset ? &preset->colors : nullptr);
    if (hwnd_)
    {
        ApplyTheme();
        // View > Theme > Custom can trigger the first preset load
        ShowThemePresetsProblem();
    }
}

void MainWindow::ShowThemePresets()
{
    LoadThemePresets();
    ShowThemePresetsProblem();
    win32ui::PresetEditorHooks hooks;
    hooks.text = [](win32ui::PresetEditorText text) { return PresetEditorText(text); };
    hooks.prompt_name = [](HWND owner, const wchar_t* title, const std::wstring& initial, std::wstring* name) {
        *name = initial;
        return PromptText(owner, title, L"Name", name);
    };
    hooks.confirm_delete = [](HWND owner, const std::wstring& name) {
        win32ui::MessageSpec spec;
        spec.title = L"ThreadSim";
        spec.message = L"Delete the theme preset " + name + L"?";
        spec.buttons = {{IDYES, L"&Delete"}, {IDCANCEL, L"Cancel"}};
        spec.default_id = IDCANCEL;
        spec.cancel_id = IDCANCEL;
        return ui::ShowMessage(owner, std::move(spec)) == IDYES;
    };
    hooks.import_presets = [](HWND owner, std::vector<win32ui::ThemePreset>* presets) {
        const std::optional<std::wstring> path = FileDialog(owner, false, L"Import theme presets", L"Theme presets", L"*.rktheme", L"rktheme", L"", L"");
        if (!path)
        {
            return false;
        }
        const win32ui::ThemePresetResult result = win32ui::ReadThemePresets(*path, presets);
        if (result.error == win32ui::ThemePresetError::none && !presets->empty())
        {
            return true;
        }
        ShowPresetProblem(owner, L"The file has no theme presets ThreadSim can read.", *path, result);
        return false;
    };
    hooks.export_presets = [](HWND owner, const std::vector<win32ui::ThemePreset>& presets) {
        const std::optional<std::wstring> path = FileDialog(owner, true, L"Export theme presets", L"Theme presets", L"*.rktheme", L"rktheme", kPresetFile, L"");
        if (!path)
        {
            return;
        }
        if (const win32ui::ThemePresetResult result = win32ui::WriteThemePresets(*path, presets); result.error != win32ui::ThemePresetError::none)
        {
            ShowPresetProblem(owner, L"The theme presets could not be exported.", *path, result);
        }
    };
    hooks.apply = [](void* context, const std::vector<win32ui::ThemePreset>& presets, const std::wstring& active_name) {
        auto* window = static_cast<MainWindow*>(context);
        window->theme_presets_ = presets;
        window->SaveThemePresets();
        window->ApplyThemePreset(active_name);
        window->SaveSettings();
        window->UpdateCommandStates();
    };
    hooks.list_hooks = ui::ListHooks();
    hooks.context = this;
    win32ui::ShowThemePresetEditor(hwnd_, theme_presets_, settings_.theme_preset, &hooks);
}

} // namespace thread_sim::frame

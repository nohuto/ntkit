// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/window_settings.h"

#include "records/json.h"

#include "platform/file_io.h"

#include <win32ui/json/reader.h>
#include <win32ui/json/writer.h>

#include <algorithm>
#include <set>

namespace thread_sim::frame
{

namespace
{

constexpr wchar_t kFormat[] = L"thread-sim.ui-settings";
// dont save oversized preset names so other settings remain readable
constexpr size_t kMaxThemePreset = 256;

bool ReadInt(win32ui::JsonReader& reader, int* out, int64_t low, int64_t high)
{
    int64_t value = 0;
    if (!reader.Signed(&value))
    {
        return false;
    }
    if (value < low || value > high)
    {
        return reader.Fail(L"A layout value is out of range.");
    }
    *out = static_cast<int>(value);
    return true;
}

} // namespace

platform::Result<UiSettings> LoadUiSettings(const std::wstring& path)
{
    auto text = platform::ReadUtf8File(path, 1024 * 1024);
    if (!text)
    {
        const platform::Error& failure = text.error();
        // a missing file means defaults
        if (failure.domain == platform::ErrorDomain::kWin32 && (failure.code == ERROR_FILE_NOT_FOUND || failure.code == ERROR_PATH_NOT_FOUND))
        {
            return UiSettings{};
        }
        return std::move(text).error();
    }
    UiSettings settings;
    if (text.value().empty())
    {
        return settings;
    }
    std::wstring error;
    win32ui::JsonReader reader(text.value(), json::Errors(&error));
    bool format_ok = false;
    std::set<std::wstring> seen;
    const bool ok = reader.Object([&](const std::wstring& key) {
        if (!seen.insert(key).second)
        {
            return reader.Fail(L"Duplicate key.");
        }
        if (key == L"format")
        {
            std::wstring format;
            if (!reader.String(&format, 64))
            {
                return false;
            }
            format_ok = format == kFormat;
            return true;
        }
        if (key == L"theme")
        {
            std::wstring mode;
            if (!reader.String(&mode, 16))
            {
                return false;
            }
            if (!win32ui::ParseThemeMode(mode, &settings.theme))
            {
                settings.theme = win32ui::ThemeMode::system;
            }
            return true;
        }
        if (key == L"theme_preset")
        {
            return reader.String(&settings.theme_preset, kMaxThemePreset);
        }
        if (key == L"window")
        {
            settings.has_window = true;
            return reader.Object([&](const std::wstring& field) {
                int value = 0;
                if (field == L"left" || field == L"top" || field == L"right" || field == L"bottom")
                {
                    if (!ReadInt(reader, &value, -100000, 100000))
                    {
                        return false;
                    }
                    (field == L"left" ? settings.window.left : field == L"top" ? settings.window.top
                                                           : field == L"right" ? settings.window.right
                                                                               : settings.window.bottom) = value;
                    return true;
                }
                if (field == L"dpi")
                {
                    if (!ReadInt(reader, &value, 48, 960))
                    {
                        return false;
                    }
                    settings.window_dpi = static_cast<UINT>(value);
                    return true;
                }
                if (field == L"maximized")
                {
                    return reader.Bool(&settings.maximized);
                }
                return reader.Skip();
            });
        }
        if (key == L"last_directory")
        {
            return reader.String(&settings.last_directory, 4096);
        }
        if (key == L"show_all_threads")
        {
            return reader.Bool(&settings.show_all_threads);
        }
        if (key == L"show_grid_lines")
        {
            return reader.Bool(&settings.show_grid_lines);
        }
        if (key == L"show_toolbar")
        {
            return reader.Bool(&settings.show_toolbar);
        }
        if (key == L"show_status_bar")
        {
            return reader.Bool(&settings.show_status_bar);
        }
        if (key == L"column_widths_dip")
        {
            settings.column_widths_dip.clear();
            return reader.Array([&] {
                int width = 0;
                if (settings.column_widths_dip.size() >= 64 || !ReadInt(reader, &width, 0, 4000))
                {
                    return settings.column_widths_dip.size() >= 64 ? reader.Fail(L"Too many columns.") : false;
                }
                settings.column_widths_dip.push_back(width);
                return true;
            });
        }
        return reader.Skip();
    });
    if (!ok || !reader.End())
    {
        return platform::FormatError(L"Read UI settings", error).WithContext(path);
    }
    if (!format_ok)
    {
        return platform::FormatError(L"Read UI settings", L"The file is not a ThreadSim settings file.").WithContext(path);
    }
    if (settings.has_window && (settings.window.right - settings.window.left < 100 || settings.window.bottom - settings.window.top < 100))
    {
        settings.has_window = false;
    }
    return settings;
}

platform::Status SaveUiSettings(const std::wstring& path, const UiSettings& settings)
{
    win32ui::JsonWriter writer;
    writer.BeginObject();
    writer.String(L"format", kFormat);
    writer.String(L"theme", win32ui::ThemeModeName(settings.theme));
    writer.String(L"theme_preset", settings.theme_preset.size() <= kMaxThemePreset ? settings.theme_preset : std::wstring());
    if (settings.has_window)
    {
        writer.BeginObject(L"window");
        writer.Signed(L"left", settings.window.left);
        writer.Signed(L"top", settings.window.top);
        writer.Signed(L"right", settings.window.right);
        writer.Signed(L"bottom", settings.window.bottom);
        writer.Unsigned(L"dpi", settings.window_dpi);
        writer.Bool(L"maximized", settings.maximized);
        writer.EndObject();
    }
    writer.BeginArray(L"column_widths_dip");
    for (int width : settings.column_widths_dip)
    {
        // the reader accepts 0 to 4000
        writer.Signed({}, std::clamp(width, 0, 4000));
    }
    writer.EndArray();
    writer.String(L"last_directory", settings.last_directory);
    writer.Bool(L"show_all_threads", settings.show_all_threads);
    writer.Bool(L"show_grid_lines", settings.show_grid_lines);
    writer.Bool(L"show_toolbar", settings.show_toolbar);
    writer.Bool(L"show_status_bar", settings.show_status_bar);
    writer.EndObject();
    return platform::WriteUtf8FileAtomic(path, writer.Take());
}

} // namespace thread_sim::frame

// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/application.h"

#include "platform/text.h"

#include <shellapi.h>

#include <string>
#include <vector>

namespace
{

struct ParseResult
{
    thread_sim::frame::AppOptions options;
    std::wstring error;
};

ParseResult ParseCommandLine()
{
    ParseResult result;
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
    {
        result.error = L"The command line couldnt be parsed";
        return result;
    }
    std::vector<std::wstring> args(arguments + 1, arguments + count);
    LocalFree(arguments);

    auto& options = result.options;
    for (size_t index = 0; index < args.size() && result.error.empty(); ++index)
    {
        const std::wstring& name = args[index];
        auto value = [&]() -> const std::wstring* {
            if (index + 1 >= args.size())
            {
                result.error = name + L" requires a value";
                return nullptr;
            }
            return &args[++index];
        };
        if (name == L"--data-dir")
        {
            if (const auto* text = value())
            {
                options.data_dir = *text;
            }
        }
        else if (name == L"--config")
        {
            if (const auto* text = value())
            {
                options.config_path = *text;
            }
        }
#if defined(THREAD_SIM_DEV)
        else if (name == L"--dev-fail-setter")
        {
            if (const auto* text = value())
            {
                const auto parsed = thread_sim::platform::ParseUnsigned(*text);
                if (!parsed || *parsed < 1)
                {
                    result.error = L"--dev-fail-setter needs a thread number from 1";
                }
                else
                {
                    options.dev_fail_setter = static_cast<size_t>(*parsed - 1);
                }
            }
        }
        else if (name == L"--dev-dpi")
        {
            if (const auto* text = value())
            {
                const auto parsed = thread_sim::platform::ParseUnsigned(*text);
                if (!parsed || *parsed < 72 || *parsed > 480)
                {
                    result.error = L"--dev-dpi must be 72-480";
                }
                else
                {
                    options.dev_dpi = static_cast<UINT>(*parsed);
                }
            }
        }
        else if (name == L"--dev-font")
        {
            if (const auto* text = value())
            {
                const auto parsed = thread_sim::platform::ParseUnsigned(*text);
                if (!parsed || *parsed < 6 || *parsed > 72)
                {
                    result.error = L"--dev-font must be 6-72 points";
                }
                else
                {
                    options.dev_font_points = static_cast<int>(*parsed);
                }
            }
        }
        else if (name == L"--dev-groups")
        {
            if (const auto* text = value())
            {
                const auto parsed = thread_sim::platform::ParseUnsigned(*text);
                if (!parsed || *parsed < 2 || *parsed > 4)
                {
                    result.error = L"--dev-groups must be 2-4";
                }
                else
                {
                    options.dev_groups = static_cast<uint16_t>(*parsed);
                }
            }
        }
        else if (name == L"--dev-perf-log")
        {
            if (const auto* text = value())
            {
                options.dev_perf_log = *text;
            }
        }
#endif
        else
        {
            result.error = L"Unknown argument: " + name;
        }
    }
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    const ParseResult parsed = ParseCommandLine();
    if (!parsed.error.empty())
    {
        const std::wstring text = parsed.error +
                                  L"\n\nUsage: thread-sim [--data-dir <dir>] [--config <file.jsonc>]"
#if defined(THREAD_SIM_DEV)
                                  L"\nDevelopment: [--dev-fail-setter <thread>] [--dev-dpi <dpi>] [--dev-font <points>] [--dev-groups <count>] [--dev-perf-log <file>]"
#endif
            ;
        MessageBoxW(nullptr, text.c_str(), L"ThreadSim", MB_OK | MB_ICONERROR);
        return 2;
    }
    return thread_sim::frame::RunApplication(instance, parsed.options, show_command);
}

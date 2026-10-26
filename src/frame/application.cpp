// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame/application.h"

#include "frame/main_window.h"
#include "frame/processor_picker.h"

#include "ui/controls.h"
#include "ui/dialog.h"

#include "platform/file_io.h"

#include "resource.h"

#include <win32ui/gdi/fonts.h>
#include <win32ui/window_metrics.h>

#include <commctrl.h>
#include <objbase.h>
#include <uxtheme.h>

namespace thread_sim::frame
{

using namespace ui;

namespace
{

int Fail(const std::wstring& text)
{
    MessageBoxW(nullptr, text.c_str(), L"ThreadSim", MB_OK | MB_ICONERROR);
    return 3;
}

} // namespace

int RunApplication(HINSTANCE instance, const AppOptions& options, int show_command)
{
    // thread name shown by View > Show all threads
    SetThreadDescription(GetCurrentThread(), L"GUI");
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_LINK_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    BufferedPaintInit();

    ApplicationEnvironment environment;
    environment.data_dir = options.data_dir.empty() ? platform::DefaultDataDirectory() : options.data_dir;
    if (!environment.data_dir.empty())
    {
        if (platform::Status status = platform::EnsureDirectory(environment.data_dir); !status)
        {
            if (!options.data_dir.empty())
            {
                // explicit data directories must be usable, no silent fallback
                BufferedPaintUnInit();
                if (SUCCEEDED(com))
                {
                    CoUninitialize();
                }
                return Fail(L"The data directory cannot be used:\n" + status.error().Describe());
            }
            environment.data_dir.clear();
        }
    }
    if (!environment.data_dir.empty())
    {
        environment.settings_path = platform::JoinPath(environment.data_dir, L"settings.json");
    }

#if defined(THREAD_SIM_DEV)
    if (options.dev_dpi)
    {
        win32ui::SetDpiOverride(options.dev_dpi);
    }
    if (options.dev_font_points)
    {
        LOGFONTW font = win32ui::MessageFontLogFont(0);
        font.lfHeight = win32ui::FontHeight(options.dev_font_points);
        win32ui::SetCustomFont(&font);
    }
    SetDevProcessorGroups(options.dev_groups);
#endif
    {
        auto topology = platform::QueryTopology();
        if (topology)
        {
            environment.topology = std::move(topology).value();
        }
        else
        {
            // without topology, affinities cannot be validated, keep the app open and report it
            environment.topology_warning = L"Processor topology query failed: " + topology.error().Describe();
            environment.topology.source_label = L"unavailable";
        }
    }

    RegisterDialogClass(instance, IDI_APPICON);
    MainWindow::RegisterClasses(instance);

    int exit_code = 0;
    {
        auto window = std::make_unique<MainWindow>(options, std::move(environment));
        if (!window->Create(show_command))
        {
            return Fail(L"The main window could not be created.");
        }
        bool idle_logged = false;
        MSG message = {};
        for (;;)
        {
            if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (!idle_logged)
                {
                    idle_logged = true;
                    window->perf().Event("first_idle_since_process_start", MsSinceProcessStart());
                }
                WaitMessage();
                continue;
            }
            if (message.message == WM_QUIT)
            {
                exit_code = static_cast<int>(message.wParam);
                break;
            }
            if (window->PreTranslate(&message))
            {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    ReleaseAccessibility();
    BufferedPaintUnInit();
    if (SUCCEEDED(com))
    {
        CoUninitialize();
    }
    return exit_code;
}

} // namespace thread_sim::frame

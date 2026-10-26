// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "frame/application.h"
#include "frame/details_window.h"
#include "frame/main_menu.h"
#include "frame/window_settings.h"
#include "frame/thread_table.h"

#include "engine/config.h"
#include "engine/process_threads.h"
#include "engine/run_controller.h"
#include "engine/snapshot.h"
#include "engine/validation.h"

#include "platform/topology.h"

#include <win32ui/gdi/fonts.h>
#include <win32ui/status_bar/status_bar.h>
#include <win32ui/theme/theme_presets.h>
#include <win32ui/toolbar/toolbar.h>

#include <windows.h>

#include <commctrl.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace thread_sim::frame
{

inline constexpr wchar_t kMainWindowClass[] = L"thread-sim.MainWindow";
inline constexpr UINT kSnapshotMessage = WM_APP + 1;

// milliseconds since process creation, for timing logs
double MsSinceProcessStart();

// development timing log as JSON lines, --dev perf log
class PerfLog
{
  public:
    ~PerfLog();
    bool Open(const std::wstring& path);
    void Event(const char* name, double milliseconds, const char* detail_key = nullptr, long long detail = 0);
    void Raw(const std::string& line);

  private:
    FILE* file_ = nullptr;
};

// startup environment, distinct from platform::Environment captured for each run
struct ApplicationEnvironment
{
    platform::Topology topology;
    std::wstring data_dir;
    std::wstring settings_path;
    std::wstring topology_warning;
};

class MainWindow final : public engine::NotificationSink
{
  public:
    MainWindow(const AppOptions& options, ApplicationEnvironment environment);
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    static void RegisterClasses(HINSTANCE instance);
    bool Create(int show_command);
    HWND hwnd() const noexcept
    {
        return hwnd_;
    }
    bool PreTranslate(MSG* message);
    PerfLog& perf() noexcept
    {
        return perf_;
    }

    bool NotifySnapshotReady() noexcept override;

  private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

    void CreateChildren();
    void CreateToolbar();
    // toolbar tooltip, Start includes duration and sample interval
    std::wstring ToolTipText(int command) const;
    void Layout();
    void ShowBars();
    void ApplyTheme();
    void ApplyFontsAndMetrics();
    void ReplaceFonts(UINT dpi);
    void OnDpiChanged(UINT dpi, const RECT* suggested);
    void OnCommand(int id);
    bool CommandEnabled(int id) const;
    // selected is sorted, read once when checking multiple commands
    bool CommandEnabled(int id, const std::vector<uint32_t>& selected) const;
    bool CommandChecked(int id) const;
    bool CommandChecked(int id, const std::vector<uint32_t>& selected) const;
    // label for menu items showing current values, empty for other items
    std::wstring CommandText(int id) const;
    // setting commands use the focused selected thread, else the first selected thread
    int RepresentativeThread(const std::vector<uint32_t>& selected) const;
    void UpdateCommandStates();
    void UpdateStatus();
    void UpdateTitle();
    void OnConfigChanged(bool structural);
    void Revalidate();
    void OnSelectionChanged();
    // assign fresh keys when loading or creating a config
    void ResetThreadKeys();
    // index in current or last run, -1 if added afterward
    int RunIndexOfRow(size_t row) const;
    void ShowDetails();
    // refreshes details, closes windows for removed threads
    void UpdateDetails();
    std::wstring RunTipText() const;
    void OnSnapshotReady();
    void OnSnapshot();
    void OnRunFinished();
    void StartRun();
    void RequestClose();
    bool ConfirmDiscard();
    bool SaveDocument(bool save_as);
    void OpenDocument(const std::wstring& path);
    void NewDocument();
    void ExportResults();
    void AddThread(bool duplicate);
    void RemoveThreads();
    void MoveThread(int delta);
    void SetThreadRelativePriority(engine::RelativeThreadPriority relative_priority);
    void ToggleThreadPriorityBoost();
    void ToggleThreadSharedMutex();
    // focused thread in threads determines whether all become active or inactive
    void ToggleThreadActive(const std::vector<uint32_t>& threads);
    void SetThreadActivityLevel(engine::ActivityLevel level);
    void EditThreadActivity(engine::Field focus = engine::Field::kNone);
    void EditThreadAffinity(int command);
    void RenameThread(int thread, std::wstring name);
    bool ThreadSettingEditable() const;
    bool ThreadSettingEditable(const std::vector<uint32_t>& selected) const;
    void FocusFirstIssue();
    HMENU BuildPriorityMenu(const std::vector<uint32_t>& selected) const;
    HMENU BuildActivityMenu(const std::vector<uint32_t>& selected) const;
    void ShowContextMenu(POINT screen);
    // Run > Duration under Start, button uses screen coordinates
    void ShowDurationMenu(const RECT& button);
    void ShowAbout();
    void RefreshEnvironment();
    // other process threads after configured threads, View > Show all threads
    void ShowAllThreads(bool show);
    // samples now between runs, coordinator includes them in run samples
    void SampleOtherThreads();
    // idle sampling adopts interval changes immediately
    void FollowSampleInterval();
    void SetProcessPriorityClass(DWORD priority_class);
    void EditProcessPlacement(int command);
    void SaveSettings();
    std::wstring ThemePresetsPath() const;
    // once per run, adds missing built ins and writes a missing file
    void LoadThemePresets();
    // reports prior file read/write failures once the window exists
    void ShowThemePresetsProblem();
    void SaveThemePresets();
    // selects the named custom preset or the first, caller saves settings
    void ApplyThemePreset(const std::wstring& name);
    void ShowThemePresets();
    void RestorePlacement(int show_command);
    bool RunBusy() const;
    std::wstring DocumentName() const;
    void SetStatusMessage(std::wstring text);

    AppOptions options_;
    ApplicationEnvironment environment_;
    HWND hwnd_ = nullptr;
    win32ui::FontSet fonts_;
    UINT dpi_ = 96;
    MainMenu menu_;
    UiSettings settings_;
    PerfLog perf_;
    std::vector<win32ui::ThemePreset> theme_presets_;
    bool presets_loaded_ = false;
    std::optional<win32ui::ThemePresetResult> presets_problem_;
    std::optional<win32ui::ThemePresetResult> presets_save_problem_;

    // library retains a pointer to these hooks
    win32ui::ToolbarHooks toolbar_hooks_;
    HWND toolbar_ = nullptr;
    win32ui::StatusBarHooks status_hooks_;
    HWND status_ = nullptr;
    ThreadTable table_;
    std::vector<std::unique_ptr<DetailsWindow>> details_;

    engine::RunConfig config_;
    // keys follow config_ threads through reorders so earlier results stay with their thread
    std::vector<uint64_t> thread_keys_;
    std::vector<uint64_t> run_keys_;
    uint64_t next_thread_key_ = 1;
    engine::RunConfig saved_config_;
    std::wstring document_path_;
    engine::ValidationResult validation_;

    std::unique_ptr<engine::RunController> controller_;
    engine::ProcessThreadSampler other_sampler_;
    engine::RunConfig run_config_; // config used by the current or last run
    std::shared_ptr<const engine::RunSnapshot> snapshot_;
    uint64_t ui_generation_ = 0;
    uint64_t last_sequence_ = 0;
    int64_t last_apply_qpc_ = 0;
    bool apply_timer_armed_ = false;
    std::atomic<bool> window_alive_{false};
    std::atomic<HWND> post_target_{nullptr};
    bool results_stale_ = false;
    bool close_pending_ = false;
    bool selection_update_pending_ = false;
    bool first_paint_logged_ = false;
    bool children_ready_ = false;
    // minimized, other threads are not visible
    bool other_sampling_paused_ = false;
    // idle timer interval, 0 while stopped
    uint32_t other_threads_interval_ms_ = 0;
    uint64_t snapshots_applied_ = 0;
    uint64_t snapshots_stale_ = 0;
    double snapshot_apply_max_ms_ = 0;
};

} // namespace thread_sim::frame

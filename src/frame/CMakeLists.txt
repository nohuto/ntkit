thread_sim_add_library(thread_sim_frame
    activity_dialog.cpp
    application.cpp
    details_window.cpp
    document_commands.cpp
    formatting.cpp
    main_menu.cpp
    main_window.cpp
    processor_picker.cpp
    run_value_dialog.cpp
    text_prompt.cpp
    theme_presets.cpp
    thread_table.cpp
    window_layout.cpp
    window_settings.cpp
)
target_include_directories(thread_sim_frame PRIVATE "${PROJECT_SOURCE_DIR}/resources")
target_link_libraries(thread_sim_frame PUBLIC
    thread_sim_ui
    thread_sim_engine
    thread_sim_records
    thread_sim_platform
    comctl32
    dwmapi
    uxtheme
)

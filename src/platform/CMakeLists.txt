thread_sim_add_library(thread_sim_platform
    process_settings.cpp
    system_threads.cpp
    environment.cpp
    error.cpp
    file_io.cpp
    text.cpp
    topology.cpp
)
target_link_libraries(thread_sim_platform PUBLIC
    win32ui::win32ui
)

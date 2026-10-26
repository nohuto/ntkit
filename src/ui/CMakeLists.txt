thread_sim_add_library(thread_sim_ui
    controls.cpp
    dialog.cpp
    theme.cpp
)
target_link_libraries(thread_sim_ui PUBLIC
    thread_sim_platform
    comctl32
)

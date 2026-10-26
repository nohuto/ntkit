thread_sim_add_library(thread_sim_records
    json.cpp
)
target_link_libraries(thread_sim_records PUBLIC
    thread_sim_platform
)

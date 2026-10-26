thread_sim_add_library(thread_sim_engine
    workload_backend.cpp
    process_threads.cpp
    workload_metrics.cpp
    thread_policy.cpp
    config.cpp
    config_io.cpp
    results_export.cpp
    run_controller.cpp
    snapshot.cpp
    validation.cpp
)
target_link_libraries(thread_sim_engine PUBLIC
    thread_sim_records
    thread_sim_platform
    bcrypt
)

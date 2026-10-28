cmake_minimum_required(VERSION 3.30)
project(thread-sim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)

option(THREAD_SIM_DEV "Build development-only test hooks" OFF)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "thread-sim targets 64-bit Windows only.")
endif()

include(FetchContent)
FetchContent_Declare(win32ui
    GIT_REPOSITORY https://github.com/nohuto/win32-ui.git
    GIT_TAG 788b364f05b01be01efc448b998d5a7051709b8f
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(win32ui)

add_library(thread_sim_compile_settings INTERFACE)
target_compile_features(thread_sim_compile_settings INTERFACE cxx_std_20)
target_compile_definitions(thread_sim_compile_settings INTERFACE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    NTDDI_VERSION=0x0A00000C
    THREAD_SIM_VERSION_TEXT=L"${PROJECT_VERSION}"
    $<$<BOOL:${THREAD_SIM_DEV}>:THREAD_SIM_DEV=1>
)
if (MSVC)
    target_compile_options(thread_sim_compile_settings INTERFACE
        /W4
        /sdl
        /guard:cf
        /utf-8
    )
    target_link_options(thread_sim_compile_settings INTERFACE
        /GUARD:CF
        /DEBUG
        $<$<CONFIG:Release>:/CETCOMPAT>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/PDBALTPATH:%_PDB%>
    )
endif()

include(cmake/ThreadSimTarget.cmake)
include(cmake/VerifyTargetGraph.cmake)

add_subdirectory(src)

add_executable(thread-sim WIN32
    src/app/main.cpp
    resources/app.rc
)
thread_sim_configure_executable(thread-sim)
target_include_directories(thread-sim PRIVATE "${PROJECT_SOURCE_DIR}/resources")
target_link_libraries(thread-sim PRIVATE
    thread_sim_frame
    thread_sim_ui
    thread_sim_platform
)
target_link_options(thread-sim PRIVATE /MANIFEST:NO)
set_property(TARGET thread-sim PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

if (EXISTS "${PROJECT_SOURCE_DIR}/tests/unit/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests/unit)
endif()

thread_sim_verify_layers(
    "platform:"
    "records:platform"
    "engine:records,platform"
    "ui:platform"
    "frame:ui,engine,records,platform"
    "app:frame,ui,platform"
)

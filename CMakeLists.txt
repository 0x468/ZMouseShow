cmake_minimum_required(VERSION 4.4)

project(
    ZMouseShow
    VERSION 0.1.0
    DESCRIPTION "Portable mouse locator for Windows"
    LANGUAGES CXX
)

if(NOT MSVC)
    message(FATAL_ERROR "ZMouseShow currently requires the Visual Studio 2026 MSVC toolchain.")
endif()

if(MSVC_VERSION LESS 1950)
    message(FATAL_ERROR "ZMouseShow requires the VS 2026 v145 toolset (MSVC 19.50 or newer).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)

add_library(zmouse_core STATIC
    src/config/settings.cpp
    src/diagnostics/report.cpp
    src/input/double_ctrl_detector.cpp
    src/input/hotkey_detector.cpp
    src/input/shake_detector.cpp
    src/overlay/locator_animation.cpp
    src/render/ring_rasterizer.cpp
)
add_library(ZMouseShow::Core ALIAS zmouse_core)

target_include_directories(zmouse_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_include_directories(zmouse_core SYSTEM
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus"
)
zmouse_apply_project_options(zmouse_core)

add_executable(ZMouseShow WIN32
    src/platform/display_simulator.cpp
    src/platform/global_hotkey_registration.cpp
    src/platform/overlay_manager.cpp
    src/platform/settings_dialog.cpp
    src/platform/spotlight_region.cpp
    src/platform/win32_main.cpp
    resources/ZMouseShow.rc
    resources/ZMouseShow.manifest
)

target_link_libraries(ZMouseShow
    PRIVATE
        ZMouseShow::Core
        comctl32
        gdi32
        shell32
        shcore
        user32
)
target_compile_definitions(ZMouseShow PRIVATE ZMOUSE_VERSION="${PROJECT_VERSION}")
zmouse_apply_project_options(ZMouseShow)
zmouse_apply_windows_hardening(ZMouseShow)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ZMouseShow)

include(CTest)
if(BUILD_TESTING)
    add_executable(zmouse_tests
        tests/input_detector_tests.cpp
    )
    target_link_libraries(zmouse_tests PRIVATE ZMouseShow::Core)
    zmouse_apply_project_options(zmouse_tests)
    add_test(NAME input_detectors COMMAND zmouse_tests)

    add_executable(zmouse_config_tests
        tests/config_tests.cpp
    )
    target_link_libraries(zmouse_config_tests PRIVATE ZMouseShow::Core)
    zmouse_apply_project_options(zmouse_config_tests)
    add_test(NAME configuration COMMAND zmouse_config_tests)

    add_executable(zmouse_render_tests
        tests/ring_rasterizer_tests.cpp
    )
    target_link_libraries(zmouse_render_tests PRIVATE ZMouseShow::Core)
    zmouse_apply_project_options(zmouse_render_tests)
    add_test(NAME ring_rasterizer COMMAND zmouse_render_tests)

    add_executable(zmouse_global_hotkey_tests
        src/platform/global_hotkey_registration.cpp
        tests/global_hotkey_registration_tests.cpp
    )
    target_link_libraries(zmouse_global_hotkey_tests PRIVATE ZMouseShow::Core user32)
    zmouse_apply_project_options(zmouse_global_hotkey_tests)
    add_test(NAME global_hotkey_registration COMMAND zmouse_global_hotkey_tests)

    add_executable(zmouse_spotlight_region_tests
        src/platform/spotlight_region.cpp
        tests/spotlight_region_tests.cpp
    )
    target_link_libraries(zmouse_spotlight_region_tests PRIVATE ZMouseShow::Core gdi32)
    zmouse_apply_project_options(zmouse_spotlight_region_tests)
    add_test(NAME spotlight_regions COMMAND zmouse_spotlight_region_tests)

    add_executable(zmouse_render_benchmark
        benchmarks/ring_render_benchmark.cpp
    )
    target_link_libraries(zmouse_render_benchmark PRIVATE ZMouseShow::Core)
    zmouse_apply_project_options(zmouse_render_benchmark)
endif()

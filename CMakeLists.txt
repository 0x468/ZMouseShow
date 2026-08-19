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
    src/policy/activation_policy.cpp
    src/render/cursor_compositor.cpp
    src/render/pointer_shape.cpp
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
    src/platform/desktop_duplication_capture.cpp
    src/platform/foreground_context.cpp
    src/platform/global_hotkey_registration.cpp
    src/platform/magnifier_window.cpp
    src/platform/overlay_manager.cpp
    src/platform/settings_dialog.cpp
    src/platform/spotlight_region.cpp
    src/platform/startup_registration.cpp
    src/platform/win32_main.cpp
    resources/ZMouseShow.rc
    resources/ZMouseShow.manifest
)

target_link_libraries(ZMouseShow
    PRIVATE
        ZMouseShow::Core
        advapi32
        comctl32
        d3d11
        d2d1
        dcomp
        dxgi
        dwmapi
        gdi32
        magnification
        shell32
        shcore
        user32
        wtsapi32
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

    add_executable(zmouse_cursor_compositor_tests
        tests/cursor_compositor_tests.cpp
    )
    target_link_libraries(zmouse_cursor_compositor_tests PRIVATE ZMouseShow::Core)
    zmouse_apply_project_options(zmouse_cursor_compositor_tests)
    add_test(NAME cursor_compositor COMMAND zmouse_cursor_compositor_tests)

    add_executable(zmouse_magnifier_tests
        tests/magnifier_tests.cpp
    )
    target_link_libraries(zmouse_magnifier_tests PRIVATE ZMouseShow::Core)
    zmouse_apply_project_options(zmouse_magnifier_tests)
    add_test(NAME magnifier_geometry COMMAND zmouse_magnifier_tests)

    add_executable(zmouse_activation_policy_tests
        tests/activation_policy_tests.cpp
    )
    target_link_libraries(zmouse_activation_policy_tests PRIVATE ZMouseShow::Core)
    zmouse_apply_project_options(zmouse_activation_policy_tests)
    add_test(NAME activation_policy COMMAND zmouse_activation_policy_tests)

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

    add_executable(zmouse_overlay_benchmark
        benchmarks/win32_overlay_benchmark.cpp
    )
    target_link_libraries(zmouse_overlay_benchmark PRIVATE ZMouseShow::Core gdi32 psapi user32)
    zmouse_apply_project_options(zmouse_overlay_benchmark)

    add_executable(zmouse_magnifier_benchmark
        benchmarks/magnifier_capture_benchmark.cpp
        src/platform/desktop_duplication_capture.cpp
        src/platform/magnifier_window.cpp
    )
    target_link_libraries(zmouse_magnifier_benchmark
        PRIVATE
            ZMouseShow::Core
            d2d1
            d3d11
            dcomp
            dxgi
            magnification
            psapi
            shcore
            user32
    )
    zmouse_apply_project_options(zmouse_magnifier_benchmark)

    add_executable(zmouse_perf_experiment
        benchmarks/perf_experiment.cpp
        src/platform/spotlight_region.cpp
    )
    target_link_libraries(zmouse_perf_experiment PRIVATE ZMouseShow::Core gdi32 user32)
    zmouse_apply_project_options(zmouse_perf_experiment)
endif()

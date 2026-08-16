#include "zmouse/platform/desktop_duplication_capture.hpp"
#include "zmouse/platform/magnifier_window.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <psapi.h>
#include <shellscalingapi.h>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

[[nodiscard]] double percentile(std::vector<double> samples, const double fraction)
{
    std::ranges::sort(samples);
    if (samples.empty())
    {
        return 0.0;
    }
    const auto index =
        static_cast<std::size_t>(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(samples.size() - 1));
    return samples[index];
}
} // namespace

int main()
{
    POINT cursor{};
    if (GetCursorPos(&cursor) == FALSE)
    {
        std::cerr << "cursor: unavailable\n";
        return 2;
    }
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    zmouse::platform::DesktopDuplicationCapture capture;
    const auto device_started_at = Clock::now();
    if (!capture.start(monitor))
    {
        std::cerr << "capture: " << zmouse::capture::failure_name(capture.diagnostics().last_failure) << '\n';
        return 2;
    }
    const auto device_ready_at = Clock::now();

    zmouse::platform::MagnifierWindow window;
    if (!window.initialize(GetModuleHandleW(nullptr)) ||
        !zmouse::platform::exclude_window_from_capture(window.window()))
    {
        std::cerr << "capture exclusion: unavailable\n";
        return 2;
    }
    zmouse::magnifier::Settings settings;
    settings.enabled = true;
    window.configure(settings);

    UINT dpi_x = 96;
    UINT dpi_y = 96;
    static_cast<void>(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y));

    std::vector<double> samples;
    samples.reserve(300);
    bool first_frame_seen = false;
    Clock::time_point first_frame_at{};
    for (std::uint32_t frame = 0; frame < 300; ++frame)
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        const auto started_at = Clock::now();
        const bool visible = window.render(capture, cursor, dpi_x);
        const auto finished_at = Clock::now();
        if (visible && !first_frame_seen)
        {
            first_frame_seen = true;
            first_frame_at = finished_at;
        }
        samples.push_back(std::chrono::duration<double, std::milli>(finished_at - started_at).count());
        Sleep(8);
    }

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    static_cast<void>(
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)));
    const auto device_ms = std::chrono::duration<double, std::milli>(device_ready_at - device_started_at).count();
    const auto first_frame_ms =
        first_frame_seen ? std::chrono::duration<double, std::milli>(first_frame_at - device_started_at).count() : 0.0;
    std::cout << "device_create_ms=" << device_ms << '\n'
              << "first_visible_frame_ms=" << first_frame_ms << '\n'
              << "render_p50_ms=" << percentile(samples, 0.50) << '\n'
              << "render_p95_ms=" << percentile(samples, 0.95) << '\n'
              << "working_set_mib=" << static_cast<double>(memory.WorkingSetSize) / (1024.0 * 1024.0) << '\n'
              << "adapter=" << capture.diagnostics().adapter << '\n'
              << "output=" << capture.diagnostics().output << '\n'
              << "format=" << capture.diagnostics().format << '\n';
    return first_frame_seen ? 0 : 2;
}

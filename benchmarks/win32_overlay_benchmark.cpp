#include <windows.h>

#include "zmouse/render/ring_rasterizer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <psapi.h>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

constexpr wchar_t benchmark_window_class[] = L"ZMouseShow.OverlayBenchmark";
constexpr std::int32_t surface_size = 560;
constexpr std::int32_t frame_count = 480;

struct SampleSummary
{
    double total_ms{};
    double mean_us{};
    double median_us{};
    double p95_us{};
    double p99_us{};
    double worst_us{};
};

struct ResourceSnapshot
{
    DWORD gdi_objects{};
    DWORD user_objects{};
    DWORD handles{};
    SIZE_T working_set_bytes{};
    SIZE_T private_bytes{};
};

[[nodiscard]] ResourceSnapshot resource_snapshot() noexcept
{
    ResourceSnapshot snapshot{
        .gdi_objects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
        .user_objects = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS),
    };
    static_cast<void>(GetProcessHandleCount(GetCurrentProcess(), &snapshot.handles));
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters)) != FALSE)
    {
        snapshot.working_set_bytes = counters.WorkingSetSize;
        snapshot.private_bytes = counters.PrivateUsage;
    }
    return snapshot;
}

[[nodiscard]] SampleSummary summarize(std::vector<double> samples, const Clock::duration total)
{
    std::ranges::sort(samples);
    const auto percentile = [&](const double value)
    {
        const auto index = static_cast<std::size_t>(std::ceil(static_cast<double>(samples.size()) * value) - 1.0);
        return samples[(std::min)(index, samples.size() - 1)];
    };
    double sum = 0.0;
    for (const double sample : samples)
    {
        sum += sample;
    }
    return {
        .total_ms = std::chrono::duration<double, std::milli>(total).count(),
        .mean_us = sum / static_cast<double>(samples.size()),
        .median_us = percentile(0.50),
        .p95_us = percentile(0.95),
        .p99_us = percentile(0.99),
        .worst_us = samples.back(),
    };
}

class DibSurface final
{
  public:
    DibSurface()
    {
        dc_ = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = surface_size;
        info.bmiHeader.biHeight = -surface_size;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels_), nullptr, 0);
        if (dc_ != nullptr && bitmap_ != nullptr)
        {
            previous_bitmap_ = SelectObject(dc_, bitmap_);
        }
    }

    ~DibSurface()
    {
        if (dc_ != nullptr && previous_bitmap_ != nullptr)
        {
            static_cast<void>(SelectObject(dc_, previous_bitmap_));
        }
        if (bitmap_ != nullptr)
        {
            static_cast<void>(DeleteObject(bitmap_));
        }
        if (dc_ != nullptr)
        {
            static_cast<void>(DeleteDC(dc_));
        }
    }

    DibSurface(const DibSurface&) = delete;
    DibSurface& operator=(const DibSurface&) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
        return dc_ != nullptr && bitmap_ != nullptr && pixels_ != nullptr && previous_bitmap_ != nullptr;
    }

    [[nodiscard]] HDC dc() const noexcept
    {
        return dc_;
    }

    [[nodiscard]] std::span<std::uint32_t> pixels() const noexcept
    {
        return {pixels_, static_cast<std::size_t>(surface_size * surface_size)};
    }

  private:
    HDC dc_{};
    HBITMAP bitmap_{};
    HGDIOBJ previous_bitmap_{};
    std::uint32_t* pixels_{};
};

class BenchmarkWindows final
{
  public:
    explicit BenchmarkWindows(const std::int32_t count)
    {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{
            .cbSize = sizeof(WNDCLASSEXW),
            .lpfnWndProc = DefWindowProcW,
            .hInstance = instance,
            .lpszClassName = benchmark_window_class,
        };
        if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return;
        }
        const auto offscreen_x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 128;
        const auto offscreen_y = GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) + 128;
        for (std::int32_t index = 0; index < count; ++index)
        {
            const HWND window = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                                benchmark_window_class, L"", WS_POPUP, offscreen_x, offscreen_y,
                                                surface_size, surface_size, nullptr, nullptr, instance, nullptr);
            if (window == nullptr)
            {
                return;
            }
            ShowWindow(window, SW_SHOWNOACTIVATE);
            windows_.push_back(window);
        }
    }

    ~BenchmarkWindows()
    {
        for (const HWND window : windows_)
        {
            static_cast<void>(DestroyWindow(window));
        }
    }

    BenchmarkWindows(const BenchmarkWindows&) = delete;
    BenchmarkWindows& operator=(const BenchmarkWindows&) = delete;

    [[nodiscard]] bool valid(const std::int32_t expected) const noexcept
    {
        return windows_.size() == static_cast<std::size_t>(expected);
    }

    [[nodiscard]] std::span<const HWND> windows() const noexcept
    {
        return windows_;
    }

  private:
    std::vector<HWND> windows_;
};

[[nodiscard]] bool update_layered_window(const HWND window, const HDC source) noexcept
{
    const HDC screen = GetDC(nullptr);
    if (screen == nullptr)
    {
        return false;
    }
    POINT source_point{};
    SIZE size{surface_size, surface_size};
    RECT bounds{};
    static_cast<void>(GetWindowRect(window, &bounds));
    POINT destination{bounds.left, bounds.top};
    BLENDFUNCTION blend{
        .BlendOp = AC_SRC_OVER,
        .SourceConstantAlpha = 255,
        .AlphaFormat = AC_SRC_ALPHA,
    };
    const BOOL updated =
        UpdateLayeredWindow(window, screen, &destination, &size, source, &source_point, 0, &blend, ULW_ALPHA);
    static_cast<void>(ReleaseDC(nullptr, screen));
    return updated != FALSE;
}

void paint_frame(const DibSurface& surface, const std::int32_t frame, std::int32_t& previous_radius) noexcept
{
    const auto radius = 120 + frame % 121;
    const zmouse::render::PixelSurface target{
        .pixels = surface.pixels(),
        .width = surface_size,
        .height = surface_size,
        .stride = surface_size,
    };
    if (previous_radius > 0)
    {
        static_cast<void>(
            zmouse::render::paint_antialiased_ring(target, surface_size, surface_size, previous_radius, 8, 0));
    }
    static_cast<void>(zmouse::render::paint_antialiased_ring(target, surface_size, surface_size, radius, 8, 235));
    previous_radius = radius;
}

[[nodiscard]] SampleSummary benchmark_layered_recreate(const std::int32_t window_count)
{
    BenchmarkWindows windows(window_count);
    if (!windows.valid(window_count))
    {
        throw std::runtime_error("cannot create layered benchmark windows");
    }
    std::vector<double> samples;
    samples.reserve(frame_count);
    const auto total_start = Clock::now();
    for (std::int32_t frame = 0; frame < frame_count; ++frame)
    {
        const auto start = Clock::now();
        for (const HWND window : windows.windows())
        {
            DibSurface surface;
            if (!surface.valid())
            {
                throw std::runtime_error("cannot create a DIB surface");
            }
            std::int32_t previous_radius = 0;
            paint_frame(surface, frame, previous_radius);
            if (!update_layered_window(window, surface.dc()))
            {
                throw std::runtime_error("UpdateLayeredWindow failed");
            }
        }
        samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
    }
    return summarize(std::move(samples), Clock::now() - total_start);
}

[[nodiscard]] SampleSummary benchmark_layered_persistent(const std::int32_t window_count)
{
    BenchmarkWindows windows(window_count);
    if (!windows.valid(window_count))
    {
        throw std::runtime_error("cannot create layered benchmark windows");
    }
    std::vector<std::unique_ptr<DibSurface>> surfaces;
    std::vector<std::int32_t> previous_radii;
    for (std::int32_t index = 0; index < window_count; ++index)
    {
        auto surface = std::make_unique<DibSurface>();
        if (!surface->valid())
        {
            throw std::runtime_error("cannot create a persistent DIB surface");
        }
        surfaces.push_back(std::move(surface));
        previous_radii.push_back(0);
    }

    std::vector<double> samples;
    samples.reserve(frame_count);
    const auto total_start = Clock::now();
    for (std::int32_t frame = 0; frame < frame_count; ++frame)
    {
        const auto start = Clock::now();
        for (std::int32_t index = 0; index < window_count; ++index)
        {
            paint_frame(*surfaces[static_cast<std::size_t>(index)], frame,
                        previous_radii[static_cast<std::size_t>(index)]);
            if (!update_layered_window(windows.windows()[static_cast<std::size_t>(index)],
                                       surfaces[static_cast<std::size_t>(index)]->dc()))
            {
                throw std::runtime_error("persistent UpdateLayeredWindow failed");
            }
        }
        samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
    }
    return summarize(std::move(samples), Clock::now() - total_start);
}

[[nodiscard]] SampleSummary benchmark_region_holes(const std::int32_t window_count)
{
    BenchmarkWindows windows(window_count);
    if (!windows.valid(window_count))
    {
        throw std::runtime_error("cannot create region benchmark windows");
    }
    const auto width = (std::max)(1, GetSystemMetrics(SM_CXSCREEN));
    const auto height = (std::max)(1, GetSystemMetrics(SM_CYSCREEN));
    std::vector<double> samples;
    samples.reserve(frame_count);
    const auto total_start = Clock::now();
    for (std::int32_t frame = 0; frame < frame_count; ++frame)
    {
        const auto start = Clock::now();
        for (const HWND window : windows.windows())
        {
            HRGN visible = CreateRectRgn(0, 0, width, height);
            const auto center_x = width / 2 + frame % 121 - 60;
            const auto center_y = height / 2;
            HRGN hole = CreateEllipticRgn(center_x - 180, center_y - 180, center_x + 180, center_y + 180);
            if (visible == nullptr || hole == nullptr || CombineRgn(visible, visible, hole, RGN_DIFF) == ERROR)
            {
                if (visible != nullptr)
                {
                    static_cast<void>(DeleteObject(visible));
                }
                if (hole != nullptr)
                {
                    static_cast<void>(DeleteObject(hole));
                }
                throw std::runtime_error("region construction failed");
            }
            static_cast<void>(DeleteObject(hole));
            if (SetWindowRgn(window, visible, FALSE) == 0)
            {
                static_cast<void>(DeleteObject(visible));
                throw std::runtime_error("SetWindowRgn failed");
            }
        }
        samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
    }
    return summarize(std::move(samples), Clock::now() - total_start);
}

void print_result(const std::int32_t windows, const std::string_view pipeline, const SampleSummary& result,
                  const ResourceSnapshot& before, const ResourceSnapshot& after)
{
    const auto signed_delta = [](const auto first, const auto second)
    { return static_cast<std::int64_t>(second) - static_cast<std::int64_t>(first); };
    std::cout << windows << ',' << pipeline << ',' << frame_count << ',' << std::fixed << std::setprecision(3)
              << result.total_ms << ',' << result.mean_us << ',' << result.median_us << ',' << result.p95_us << ','
              << result.p99_us << ',' << result.worst_us << ',' << signed_delta(before.gdi_objects, after.gdi_objects)
              << ',' << signed_delta(before.user_objects, after.user_objects) << ','
              << signed_delta(before.handles, after.handles) << ','
              << static_cast<double>(signed_delta(before.working_set_bytes, after.working_set_bytes)) / 1'048'576.0
              << ',' << static_cast<double>(signed_delta(before.private_bytes, after.private_bytes)) / 1'048'576.0
              << '\n';
}

template <typename Benchmark>
void run_benchmark(const std::int32_t windows, const std::string_view name, Benchmark&& benchmark)
{
    const auto before = resource_snapshot();
    const auto result = benchmark(windows);
    const auto after = resource_snapshot();
    print_result(windows, name, result, before, after);
}
} // namespace

int main()
{
    try
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        static_cast<void>(benchmark_layered_persistent(1));
        static_cast<void>(benchmark_region_holes(1));
        std::cout << "windows,pipeline,frames,total_ms,mean_frame_us,median_frame_us,p95_frame_us,p99_frame_us,"
                     "worst_frame_us,gdi_delta,user_delta,handle_delta,working_set_delta_mib,private_delta_mib\n";
        for (const std::int32_t windows : {1, 2, 3})
        {
            run_benchmark(windows, "layered-recreate", benchmark_layered_recreate);
            run_benchmark(windows, "layered-persistent", benchmark_layered_persistent);
            run_benchmark(windows, "region-hole", benchmark_region_holes);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}

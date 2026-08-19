// Performance experiment: measure hot-path costs and pre-allocation impact.
// Build: cmake --build --preset release --target zmouse_perf_experiment
// Run:   zmouse_perf_experiment.exe

#include <windows.h>

#include "zmouse/overlay/geometry.hpp"
#include "zmouse/overlay/spotlight_shape.hpp"
#include "zmouse/platform/spotlight_region.hpp"
#include <algorithm>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
using Clock = std::chrono::high_resolution_clock;

// ─── Helpers ───────────────────────────────────────────────────────────────────

void print_header()
{
    std::cout << "experiment,variant,iterations,mean_ns,median_ns,p95_ns,p99_ns,worst_ns\n";
}

struct Stats
{
    double mean{};
    double median{};
    double p95{};
    double p99{};
    double worst{};
};

Stats compute_stats(std::vector<double>& samples_ns)
{
    std::ranges::sort(samples_ns);
    const auto n = samples_ns.size();
    double sum = 0;
    for (auto v : samples_ns)
        sum += v;
    Stats s;
    s.mean = sum / static_cast<double>(n);
    s.median = samples_ns[n / 2];
    s.p95 = samples_ns[static_cast<std::size_t>(static_cast<double>(n) * 0.95)];
    s.p99 = samples_ns[static_cast<std::size_t>(static_cast<double>(n) * 0.99)];
    s.worst = samples_ns[n - 1];
    return s;
}

void print_row(const char* experiment, const char* variant, std::size_t iterations, Stats& s)
{
    std::cout << experiment << ',' << variant << ',' << iterations << ',' << std::fixed << std::setprecision(1)
              << s.mean << ',' << s.median << ',' << s.p95 << ',' << s.p99 << ',' << s.worst << '\n';
}

// ─── Experiment 1: GDI Region allocation per-frame vs pre-allocated ────────────
//
// Simulates apply_hole with 3 monitors, moving cursor across positions.
// Current: create 2 regions + CombineRgn + delete hole each frame per monitor.
// Optimized: pre-allocate full_region per monitor, only recreate hole + CombineRgn.

void bench_region_allocation()
{
    constexpr int iterations = 10'000;
    constexpr int monitors = 3;
    const int screen_w = GetSystemMetrics(SM_CXSCREEN);
    const int screen_h = GetSystemMetrics(SM_CYSCREEN);
    constexpr int radius = 120;

    // --- Variant A: current approach (allocate + delete every frame) ---
    {
        std::vector<double> samples;
        samples.reserve(iterations);
        for (int i = 0; i < iterations; ++i)
        {
            const auto start = Clock::now();
            for (int m = 0; m < monitors; ++m)
            {
                const int cx = screen_w / 2 + (i % 200) - 100;
                const int cy = screen_h / 2;
                HRGN full = CreateRectRgn(0, 0, screen_w, screen_h);
                HRGN hole = CreateEllipticRgn(cx - radius, cy - radius, cx + radius, cy + radius);
                if (full && hole)
                {
                    CombineRgn(full, full, hole, RGN_DIFF);
                    // Don't actually call SetWindowRgn in benchmark — just measure region ops
                }
                if (hole)
                    DeleteObject(hole);
                if (full)
                    DeleteObject(full);
            }
            samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                         Clock::now() - start)
                                                       .count()));
        }
        auto stats = compute_stats(samples);
        print_row("region-allocation", "current-alloc-every-frame", iterations, stats);
    }

    // --- Variant B: pre-allocated full regions, only recreate hole ---
    {
        HRGN full_regions[monitors]{};
        for (int m = 0; m < monitors; ++m)
            full_regions[m] = CreateRectRgn(0, 0, screen_w, screen_h);

        std::vector<double> samples;
        samples.reserve(iterations);
        for (int i = 0; i < iterations; ++i)
        {
            const auto start = Clock::now();
            for (int m = 0; m < monitors; ++m)
            {
                const int cx = screen_w / 2 + (i % 200) - 100;
                const int cy = screen_h / 2;
                // Reset full region to full-screen (no alloc)
                SetRectRgn(full_regions[m], 0, 0, screen_w, screen_h);
                // Only allocate the hole region
                HRGN hole = CreateEllipticRgn(cx - radius, cy - radius, cx + radius, cy + radius);
                if (hole)
                {
                    CombineRgn(full_regions[m], full_regions[m], hole, RGN_DIFF);
                    DeleteObject(hole);
                }
            }
            samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                         Clock::now() - start)
                                                       .count()));
        }
        for (int m = 0; m < monitors; ++m)
            DeleteObject(full_regions[m]);

        auto stats = compute_stats(samples);
        print_row("region-allocation", "prealloc-full-regions", iterations, stats);
    }

    // --- Variant C: fully pre-allocated (offset hole instead of recreate) ---
    {
        HRGN full_regions[monitors]{};
        HRGN hole_regions[monitors]{};
        for (int m = 0; m < monitors; ++m)
        {
            full_regions[m] = CreateRectRgn(0, 0, screen_w, screen_h);
            hole_regions[m] = CreateEllipticRgn(0, 0, radius * 2, radius * 2);
        }

        std::vector<double> samples;
        samples.reserve(iterations);
        for (int i = 0; i < iterations; ++i)
        {
            const auto start = Clock::now();
            for (int m = 0; m < monitors; ++m)
            {
                const int cx = screen_w / 2 + (i % 200) - 100;
                const int cy = screen_h / 2;
                SetRectRgn(full_regions[m], 0, 0, screen_w, screen_h);
                // Offset pre-existing hole region (no alloc, no delete)
                OffsetRgn(hole_regions[m], cx - radius - (screen_w / 2 - 100 - radius),
                          cy - radius - (screen_h / 2 - radius));
                CombineRgn(full_regions[m], full_regions[m], hole_regions[m], RGN_DIFF);
            }
            samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                         Clock::now() - start)
                                                       .count()));
        }
        for (int m = 0; m < monitors; ++m)
        {
            DeleteObject(full_regions[m]);
            DeleteObject(hole_regions[m]);
        }

        auto stats = compute_stats(samples);
        print_row("region-allocation", "fully-preallocated-offset", iterations, stats);
    }
}

// ─── Experiment 2: spotlight_region shape creation cost ────────────────────────

void bench_spotlight_shapes()
{
    constexpr int iterations = 50'000;
    const zmouse::overlay::Rect bounds{0, 0, 240, 240};

    for (auto shape : {zmouse::overlay::SpotlightShape::circle,
                       zmouse::overlay::SpotlightShape::rounded_square,
                       zmouse::overlay::SpotlightShape::diamond})
    {
        const char* name = shape == zmouse::overlay::SpotlightShape::circle       ? "circle"
                           : shape == zmouse::overlay::SpotlightShape::rounded_square ? "rounded-square"
                                                                                      : "diamond";
        std::vector<double> samples;
        samples.reserve(iterations);
        for (int i = 0; i < iterations; ++i)
        {
            const auto start = Clock::now();
            HRGN rgn = zmouse::platform::create_spotlight_region(bounds, shape);
            samples.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
            if (rgn)
                DeleteObject(rgn);
        }
        auto stats = compute_stats(samples);
        print_row("spotlight-shape-create", name, iterations, stats);
    }
}

// ─── Experiment 3: key_down scan (O(512) bitset vs counter) ───────────────────

void bench_key_scan()
{
    constexpr int iterations = 500'000;
    std::bitset<512> key_down{};
    // Simulate some keys pressed
    key_down.set(VK_CONTROL);
    key_down.set(VK_LCONTROL + 256);
    key_down.set(0x41); // 'A'

    // --- Variant A: scan all 512 bits ---
    {
        std::vector<double> samples;
        samples.reserve(iterations);
        for (int i = 0; i < iterations; ++i)
        {
            const auto start = Clock::now();
            bool any_other = false;
            bool any_non_modifier = false;
            for (std::size_t idx = 0; idx < key_down.size(); ++idx)
            {
                if (idx != 0x41 && key_down[idx])
                {
                    any_other = true;
                    const auto vk = static_cast<UINT>(idx % 256U);
                    if (vk != VK_CONTROL && vk != VK_LCONTROL && vk != VK_RCONTROL && vk != VK_MENU &&
                        vk != VK_LMENU && vk != VK_RMENU && vk != VK_SHIFT && vk != VK_LSHIFT && vk != VK_RSHIFT &&
                        vk != VK_LWIN && vk != VK_RWIN)
                        any_non_modifier = true;
                }
            }
            samples.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
        }
        auto stats = compute_stats(samples);
        print_row("key-scan", "bitset-scan-512", iterations, stats);
    }

    // --- Variant B: counter (O(1)) ---
    {
        int non_modifier_down_count = 3; // simulate pre-counted
        std::vector<double> samples;
        samples.reserve(iterations);
        for (int i = 0; i < iterations; ++i)
        {
            const auto start = Clock::now();
            bool any_other = non_modifier_down_count > 1;
            bool any_non_modifier = non_modifier_down_count > 1;
            (void)any_other;
            (void)any_non_modifier;
            samples.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
        }
        auto stats = compute_stats(samples);
        print_row("key-scan", "counter-O1", iterations, stats);
    }
}

// ─── Experiment 4: SetRectRgn + CombineRgn cost (no alloc) ────────────────────
// Measures just the GDI region math without allocation overhead.

void bench_region_math_only()
{
    constexpr int iterations = 50'000;
    const int screen_w = GetSystemMetrics(SM_CXSCREEN);
    const int screen_h = GetSystemMetrics(SM_CYSCREEN);
    constexpr int radius = 120;

    HRGN full = CreateRectRgn(0, 0, screen_w, screen_h);
    HRGN hole = CreateEllipticRgn(0, 0, radius * 2, radius * 2);

    std::vector<double> samples;
    samples.reserve(iterations);
    for (int i = 0; i < iterations; ++i)
    {
        const int cx = screen_w / 2 + (i % 200) - 100;
        const int cy = screen_h / 2;
        const auto start = Clock::now();
        SetRectRgn(full, 0, 0, screen_w, screen_h);
        OffsetRgn(hole, cx - radius, cy - radius);
        CombineRgn(full, full, hole, RGN_DIFF);
        samples.push_back(
            static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
    }
    DeleteObject(full);
    DeleteObject(hole);

    auto stats = compute_stats(samples);
    print_row("region-math", "SetRectRgn+OffsetRgn+CombineRgn", iterations, stats);
}

// ─── Experiment 5: std::hypot vs fast approximate ─────────────────────────────

inline double fast_hypot(double x, double y)
{
    x = std::abs(x);
    y = std::abs(y);
    if (x < y)
        std::swap(x, y);
    // Alpha max plus beta min approximation (alpha=1, beta=0.4)
    return x + 0.4 * y;
}

void bench_hypot()
{
    constexpr int iterations = 500'000;
    volatile double sink = 0;

    // std::hypot
    {
        std::vector<double> samples;
        samples.reserve(iterations);
        for (int i = 0; i < iterations; ++i)
        {
            const double dx = static_cast<double>(i % 200) - 100.0;
            const double dy = static_cast<double>(i % 150) - 75.0;
            const auto start = Clock::now();
            sink = std::hypot(dx, dy);
            samples.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
        }
        auto stats = compute_stats(samples);
        print_row("hypot", "std-hypot", iterations, stats);
    }

    // fast approximate
    {
        std::vector<double> samples;
        samples.reserve(iterations);
        for (int i = 0; i < iterations; ++i)
        {
            const double dx = static_cast<double>(i % 200) - 100.0;
            const double dy = static_cast<double>(i % 150) - 75.0;
            const auto start = Clock::now();
            sink = fast_hypot(dx, dy);
            samples.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
        }
        auto stats = compute_stats(samples);
        print_row("hypot", "alpha-max-beta-min", iterations, stats);
    }
}

// ─── Experiment 6: Full overlay pipeline under rapid mouse movement ───────────
// Simulates the real move_to path: region holes + UpdateLayeredWindow for
// ring and cursor windows, with cursor positions changing every frame.
// This is the scenario that matters most for perceived smoothness.

void bench_rapid_mouse_pipeline()
{
    constexpr int frames = 600;     // ~10 seconds at 60 Hz
    constexpr int monitors = 3;
    constexpr int radius = 120;
    constexpr int ring_size = 280;  // diameter
    const int screen_w = (std::max)(1, GetSystemMetrics(SM_CXSCREEN));
    const int screen_h = (std::max)(1, GetSystemMetrics(SM_CYSCREEN));

    // Create offscreen windows for the benchmark
    const int off_x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 256;
    const int off_y = GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) + 256;
    auto create_window = [&](int w, int h) -> HWND {
        HWND wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                   L"static", L"", WS_POPUP, off_x, off_y, w, h,
                                   nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (wnd) ShowWindow(wnd, SW_SHOWNOACTIVATE);
        return wnd;
    };

    // Pre-allocate a DIB surface for the ring
    auto create_dib = [](int w, int h, HDC& dc, HBITMAP& bmp, HGDIOBJ& old, std::uint32_t*& px) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* mem = nullptr;
        bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &mem, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        if (!bmp || !dc || !mem) return false;
        old = SelectObject(dc, bmp);
        px = static_cast<std::uint32_t*>(mem);
        std::fill_n(px, static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0x80FF0000u);
        return true;
    };

    // --- Variant A: current approach (alloc regions every frame) ---
    {
        HWND overlay_wnds[3]{};
        HWND ring_wnd = create_window(ring_size, ring_size);
        for (int m = 0; m < monitors; ++m)
            overlay_wnds[m] = create_window(screen_w, screen_h);

        HDC ring_dc{}; HBITMAP ring_bmp{}; HGDIOBJ ring_old{}; std::uint32_t* ring_px{};
        create_dib(ring_size, ring_size, ring_dc, ring_bmp, ring_old, ring_px);

        std::vector<double> samples;
        samples.reserve(frames);
        for (int f = 0; f < frames; ++f)
        {
            // Simulate fast mouse: move in a circle pattern
            const double angle = static_cast<double>(f) * 0.1;
            const int cx = screen_w / 2 + static_cast<int>(std::cos(angle) * 300);
            const int cy = screen_h / 2 + static_cast<int>(std::sin(angle) * 200);

            const auto start = Clock::now();

            // 1. apply_hole per monitor (current: alloc+delete each)
            for (int m = 0; m < monitors; ++m)
            {
                HRGN full = CreateRectRgn(0, 0, screen_w, screen_h);
                HRGN hole = CreateEllipticRgn(cx - radius, cy - radius, cx + radius, cy + radius);
                if (full && hole)
                {
                    CombineRgn(full, full, hole, RGN_DIFF);
                    SetWindowRgn(overlay_wnds[m], full, FALSE);
                    // SetWindowRgn takes ownership of full
                }
                if (hole) DeleteObject(hole);
            }

            // 2. UpdateLayeredWindow for ring
            POINT dst{cx - ring_size / 2, cy - ring_size / 2};
            POINT src{};
            SIZE sz{ring_size, ring_size};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(ring_wnd, nullptr, &dst, &sz, ring_dc, &src, 0, &blend, ULW_ALPHA);

            samples.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
        }
        for (int m = 0; m < monitors; ++m) DestroyWindow(overlay_wnds[m]);
        DestroyWindow(ring_wnd);
        SelectObject(ring_dc, ring_old);
        DeleteObject(ring_bmp);
        DeleteDC(ring_dc);

        auto stats = compute_stats(samples);
        print_row("rapid-mouse-pipeline", "current-alloc-regions", frames, stats);
    }

    // --- Variant B: pre-allocated regions + persistent ring surface ---
    {
        HWND overlay_wnds[3]{};
        HWND ring_wnd = create_window(ring_size, ring_size);
        for (int m = 0; m < monitors; ++m)
            overlay_wnds[m] = create_window(screen_w, screen_h);

        HDC ring_dc{}; HBITMAP ring_bmp{}; HGDIOBJ ring_old{}; std::uint32_t* ring_px{};
        create_dib(ring_size, ring_size, ring_dc, ring_bmp, ring_old, ring_px);

        // Pre-allocate regions
        HRGN full_regions[3]{};
        HRGN hole_regions[3]{};
        for (int m = 0; m < monitors; ++m)
        {
            full_regions[m] = CreateRectRgn(0, 0, screen_w, screen_h);
            hole_regions[m] = CreateEllipticRgn(0, 0, radius * 2, radius * 2);
        }

        std::vector<double> samples;
        samples.reserve(frames);
        for (int f = 0; f < frames; ++f)
        {
            const double angle = static_cast<double>(f) * 0.1;
            const int cx = screen_w / 2 + static_cast<int>(std::cos(angle) * 300);
            const int cy = screen_h / 2 + static_cast<int>(std::sin(angle) * 200);

            const auto start = Clock::now();

            // 1. apply_hole per monitor (pre-allocated: SetRectRgn + OffsetRgn + CombineRgn)
            for (int m = 0; m < monitors; ++m)
            {
                SetRectRgn(full_regions[m], 0, 0, screen_w, screen_h);
                OffsetRgn(hole_regions[m], cx - radius, cy - radius);
                CombineRgn(full_regions[m], full_regions[m], hole_regions[m], RGN_DIFF);
                SetWindowRgn(overlay_wnds[m], full_regions[m], FALSE);
                // SetWindowRgn takes ownership — must re-create for next frame
                full_regions[m] = CreateRectRgn(0, 0, screen_w, screen_h);
            }

            // 2. UpdateLayeredWindow for ring (same as variant A)
            POINT dst{cx - ring_size / 2, cy - ring_size / 2};
            POINT src{};
            SIZE sz{ring_size, ring_size};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(ring_wnd, nullptr, &dst, &sz, ring_dc, &src, 0, &blend, ULW_ALPHA);

            samples.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
        }
        for (int m = 0; m < monitors; ++m)
        {
            if (full_regions[m]) DeleteObject(full_regions[m]);
            if (hole_regions[m]) DeleteObject(hole_regions[m]);
            DestroyWindow(overlay_wnds[m]);
        }
        DestroyWindow(ring_wnd);
        SelectObject(ring_dc, ring_old);
        DeleteObject(ring_bmp);
        DeleteDC(ring_dc);

        auto stats = compute_stats(samples);
        print_row("rapid-mouse-pipeline", "prealloc-regions", frames, stats);
    }

    // --- Variant C: pre-allocated + skip apply_hole when cursor unchanged ---
    {
        HWND overlay_wnds[3]{};
        HWND ring_wnd = create_window(ring_size, ring_size);
        for (int m = 0; m < monitors; ++m)
            overlay_wnds[m] = create_window(screen_w, screen_h);

        HDC ring_dc{}; HBITMAP ring_bmp{}; HGDIOBJ ring_old{}; std::uint32_t* ring_px{};
        create_dib(ring_size, ring_size, ring_dc, ring_bmp, ring_old, ring_px);

        std::vector<double> samples;
        samples.reserve(frames);
        int last_cx = -1, last_cy = -1;
        for (int f = 0; f < frames; ++f)
        {
            // Simulate: cursor only moves on 2/3 of frames (rest are timer ticks with no movement)
            int cx, cy;
            if (f % 3 == 0)
            {
                cx = last_cx < 0 ? screen_w / 2 : last_cx;
                cy = last_cy < 0 ? screen_h / 2 : last_cy;
            }
            else
            {
                const double angle = static_cast<double>(f) * 0.1;
                cx = screen_w / 2 + static_cast<int>(std::cos(angle) * 300);
                cy = screen_h / 2 + static_cast<int>(std::sin(angle) * 200);
            }

            const auto start = Clock::now();

            if (cx != last_cx || cy != last_cy)
            {
                for (int m = 0; m < monitors; ++m)
                {
                    HRGN full = CreateRectRgn(0, 0, screen_w, screen_h);
                    HRGN hole = CreateEllipticRgn(cx - radius, cy - radius, cx + radius, cy + radius);
                    if (full && hole)
                    {
                        CombineRgn(full, full, hole, RGN_DIFF);
                        SetWindowRgn(overlay_wnds[m], full, FALSE);
                    }
                    if (hole) DeleteObject(hole);
                }
                last_cx = cx;
                last_cy = cy;

                POINT dst{cx - ring_size / 2, cy - ring_size / 2};
                POINT src{};
                SIZE sz{ring_size, ring_size};
                BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
                UpdateLayeredWindow(ring_wnd, nullptr, &dst, &sz, ring_dc, &src, 0, &blend, ULW_ALPHA);
            }
            // else: cursor unchanged, skip everything — near zero cost

            samples.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
        }
        for (int m = 0; m < monitors; ++m) DestroyWindow(overlay_wnds[m]);
        DestroyWindow(ring_wnd);
        SelectObject(ring_dc, ring_old);
        DeleteObject(ring_bmp);
        DeleteDC(ring_dc);

        auto stats = compute_stats(samples);
        print_row("rapid-mouse-pipeline", "skip-when-unchanged", frames, stats);
    }
}

} // namespace

int main()
{
    try
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        print_header();
        bench_region_allocation();
        bench_spotlight_shapes();
        bench_region_math_only();
        bench_key_scan();
        bench_hypot();
        bench_rapid_mouse_pipeline();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}

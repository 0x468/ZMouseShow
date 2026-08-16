#include "zmouse/render/ring_rasterizer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

constexpr std::int32_t frame_count = 24;
constexpr std::int32_t maximum_animated_radius = 768;
constexpr double initial_ring_scale = 2.25;

struct Scenario
{
    const char* name;
    std::int32_t base_radius;
    std::int32_t margin;
    std::int32_t stroke;
};

struct Result
{
    double total_ms{};
    double mean_frame_us{};
    double median_frame_us{};
    double p95_frame_us{};
    double p99_frame_us{};
    double worst_frame_us{};
    double surface_mib{};
    std::uint64_t surface_allocations{};
    std::uint64_t checksum{};
};

[[nodiscard]] double ease_out(const double value) noexcept
{
    const double remaining = 1.0 - (std::clamp)(value, 0.0, 1.0);
    return 1.0 - remaining * remaining * remaining;
}

[[nodiscard]] std::int32_t radius_for_frame(const Scenario& scenario, const std::int32_t frame) noexcept
{
    const double linear = static_cast<double>(frame) / static_cast<double>(frame_count - 1);
    const double scale = 1.0 + (initial_ring_scale - 1.0) * (1.0 - ease_out(linear));
    const auto requested = static_cast<std::int32_t>(std::lround(static_cast<double>(scenario.base_radius) * scale));
    return (std::clamp)(requested, scenario.base_radius, (std::max)(scenario.base_radius, maximum_animated_radius));
}

[[nodiscard]] std::uint32_t pixel_value(const std::uint8_t alpha) noexcept
{
    return static_cast<std::uint32_t>(alpha) << 24U | static_cast<std::uint32_t>(alpha) << 16U |
           static_cast<std::uint32_t>(alpha) << 8U | static_cast<std::uint32_t>(alpha);
}

void legacy_rasterize(std::span<std::uint32_t> pixels, const std::int32_t size, const std::int32_t radius,
                      const std::int32_t stroke)
{
    const double center = static_cast<double>(size) / 2.0 - 0.5;
    const double half_stroke = static_cast<double>(stroke) / 2.0;
    for (std::int32_t y = 0; y < size; ++y)
    {
        for (std::int32_t x = 0; x < size; ++x)
        {
            const double dx = static_cast<double>(x) - center;
            const double dy = static_cast<double>(y) - center;
            const double distance = std::hypot(dx, dy);
            const double coverage = (std::clamp)(half_stroke + 1.0 - std::abs(distance - radius), 0.0, 1.0);
            const auto alpha = static_cast<std::uint8_t>(coverage * 235.0);
            pixels[static_cast<std::size_t>(y * size + x)] = pixel_value(alpha);
        }
    }
}

[[nodiscard]] Result summarize(std::vector<double> frame_times_us, const Clock::duration total,
                               const std::size_t surface_bytes, const std::uint64_t surface_allocations,
                               const std::uint64_t checksum)
{
    std::ranges::sort(frame_times_us);
    const auto percentile = [&](const double value)
    {
        const auto index =
            static_cast<std::size_t>(std::ceil(static_cast<double>(frame_times_us.size()) * value) - 1.0);
        return frame_times_us[(std::min)(index, frame_times_us.size() - 1)];
    };
    double sum = 0.0;
    for (const double duration : frame_times_us)
    {
        sum += duration;
    }
    return {
        .total_ms = std::chrono::duration<double, std::milli>(total).count(),
        .mean_frame_us = sum / static_cast<double>(frame_times_us.size()),
        .median_frame_us = percentile(0.50),
        .p95_frame_us = percentile(0.95),
        .p99_frame_us = percentile(0.99),
        .worst_frame_us = frame_times_us.back(),
        .surface_mib = static_cast<double>(surface_bytes) / (1024.0 * 1024.0),
        .surface_allocations = surface_allocations,
        .checksum = checksum,
    };
}

[[nodiscard]] Result benchmark_legacy(const Scenario& scenario, const std::int32_t animations)
{
    std::vector<double> frame_times_us;
    frame_times_us.reserve(static_cast<std::size_t>(animations * frame_count));
    std::uint64_t checksum = 0;
    std::size_t maximum_bytes = 0;
    const auto total_start = Clock::now();
    for (std::int32_t animation = 0; animation < animations; ++animation)
    {
        for (std::int32_t frame = 0; frame < frame_count; ++frame)
        {
            const auto radius = radius_for_frame(scenario, frame);
            const auto size = (radius + scenario.margin) * 2;
            const auto start = Clock::now();
            std::vector<std::uint32_t> pixels(static_cast<std::size_t>(size * size));
            legacy_rasterize(pixels, size, radius, scenario.stroke);
            const auto end = Clock::now();
            frame_times_us.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            maximum_bytes = (std::max)(maximum_bytes, pixels.size() * sizeof(std::uint32_t));
            checksum = checksum * 1'099'511'628'211ULL ^
                       pixels[static_cast<std::size_t>((size / 2) * size + size / 2 + radius)];
        }
    }
    return summarize(std::move(frame_times_us), Clock::now() - total_start, maximum_bytes,
                     static_cast<std::uint64_t>(animations) * frame_count, checksum);
}

[[nodiscard]] Result benchmark_persistent(const Scenario& scenario, const std::int32_t animations)
{
    const auto maximum_radius = radius_for_frame(scenario, 0);
    const auto capacity = (maximum_radius + scenario.margin) * 2;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(capacity * capacity));
    const zmouse::render::PixelSurface surface{
        .pixels = pixels,
        .width = capacity,
        .height = capacity,
        .stride = capacity,
    };

    std::vector<double> frame_times_us;
    frame_times_us.reserve(static_cast<std::size_t>(animations * frame_count));
    std::uint64_t checksum = 0;
    std::int32_t previous_radius = 0;
    std::int32_t previous_size = 0;
    const auto total_start = Clock::now();
    for (std::int32_t animation = 0; animation < animations; ++animation)
    {
        for (std::int32_t frame = 0; frame < frame_count; ++frame)
        {
            const auto radius = radius_for_frame(scenario, frame);
            const auto size = (radius + scenario.margin) * 2;
            const auto start = Clock::now();
            if (previous_radius > 0)
            {
                static_cast<void>(zmouse::render::paint_antialiased_ring(surface, previous_size, previous_size,
                                                                         previous_radius, scenario.stroke, 0));
            }
            static_cast<void>(
                zmouse::render::paint_antialiased_ring(surface, size, size, radius, scenario.stroke, 235));
            const auto end = Clock::now();
            frame_times_us.push_back(std::chrono::duration<double, std::micro>(end - start).count());
            checksum = checksum * 1'099'511'628'211ULL ^
                       pixels[static_cast<std::size_t>((size / 2) * capacity + size / 2 + radius)];
            previous_radius = radius;
            previous_size = size;
        }
    }
    return summarize(std::move(frame_times_us), Clock::now() - total_start, pixels.size() * sizeof(std::uint32_t), 1,
                     checksum);
}

[[nodiscard]] std::int32_t parse_animations(const int argument_count, char** arguments)
{
    if (argument_count != 3 || std::string_view(arguments[1]) != "--animations")
    {
        return 20;
    }
    const long parsed = std::strtol(arguments[2], nullptr, 10);
    return parsed > 0 && parsed <= 10'000 ? static_cast<std::int32_t>(parsed) : 20;
}

void print_result(const Scenario& scenario, const char* implementation, const std::int32_t animations,
                  const Result& result)
{
    std::cout << scenario.name << ',' << implementation << ',' << animations << ',' << animations * frame_count << ','
              << std::fixed << std::setprecision(3) << result.total_ms << ',' << result.mean_frame_us << ','
              << result.median_frame_us << ',' << result.p95_frame_us << ',' << result.p99_frame_us << ','
              << result.worst_frame_us << ',' << result.surface_mib << ',' << result.surface_allocations << ','
              << result.checksum << '\n';
}
} // namespace

int main(const int argument_count, char** arguments)
{
    const auto animations = parse_animations(argument_count, arguments);
    constexpr Scenario scenarios[]{
        {.name = "96dpi-radius120", .base_radius = 120, .margin = 18, .stroke = 5},
        {.name = "125dpi-radius150", .base_radius = 150, .margin = 23, .stroke = 6},
        {.name = "150dpi-radius180", .base_radius = 180, .margin = 27, .stroke = 7},
        {.name = "200dpi-radius240", .base_radius = 240, .margin = 36, .stroke = 10},
        {.name = "maximum-radius512", .base_radius = 512, .margin = 48, .stroke = 12},
    };

    std::cout << "scenario,implementation,animations,frames,total_ms,mean_frame_us,median_frame_us,p95_frame_us,"
                 "p99_frame_us,worst_frame_us,surface_mib,surface_allocations,checksum\n";
    for (const auto& scenario : scenarios)
    {
        print_result(scenario, "legacy-full-raster", animations, benchmark_legacy(scenario, animations));
        print_result(scenario, "persistent-circumference", animations, benchmark_persistent(scenario, animations));
    }
    return 0;
}

#include "zmouse/render/ring_rasterizer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <tuple>
#include <vector>

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::vector<std::uint32_t> legacy_ring(const std::int32_t size, const std::int32_t radius,
                                                     const std::int32_t stroke)
{
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(size * size));
    const double center = static_cast<double>(size) / 2.0 - 0.5;
    const double half_stroke = static_cast<double>(stroke) / 2.0;
    for (std::int32_t y = 0; y < size; ++y)
    {
        for (std::int32_t x = 0; x < size; ++x)
        {
            const double distance = std::hypot(static_cast<double>(x) - center, static_cast<double>(y) - center);
            const double coverage = (std::clamp)(half_stroke + 1.0 - std::abs(distance - radius), 0.0, 1.0);
            const auto alpha = static_cast<std::uint8_t>(coverage * 235.0);
            pixels[static_cast<std::size_t>(y * size + x)] =
                static_cast<std::uint32_t>(alpha) << 24U | static_cast<std::uint32_t>(alpha) << 16U |
                static_cast<std::uint32_t>(alpha) << 8U | static_cast<std::uint32_t>(alpha);
        }
    }
    return pixels;
}

void test_ring_is_drawn_and_erased_without_touching_capacity_padding()
{
    constexpr std::int32_t capacity = 80;
    constexpr std::int32_t active = 64;
    constexpr std::uint32_t sentinel = 0xDEADBEEFU;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(capacity * capacity), sentinel);
    for (std::int32_t y = 0; y < active; ++y)
    {
        auto* row = pixels.data() + static_cast<std::size_t>(y * capacity);
        std::fill(row, row + active, 0U);
    }

    const zmouse::render::PixelSurface surface{
        .pixels = pixels,
        .width = capacity,
        .height = capacity,
        .stride = capacity,
    };
    check(zmouse::render::paint_antialiased_ring(surface, active, active, 20, 4, 235), "valid ring is drawn");
    check(pixels[static_cast<std::size_t>(32 * capacity + 52)] != 0U, "ring circumference contains alpha");
    check(pixels[static_cast<std::size_t>(32 * capacity + 32)] == 0U, "ring center stays transparent");
    check(pixels[static_cast<std::size_t>(79 * capacity + 79)] == sentinel,
          "pixels outside the active rectangle are untouched");

    check(zmouse::render::paint_antialiased_ring(surface, active, active, 20, 4, 0), "ring can be erased");
    bool active_is_clear = true;
    for (std::int32_t y = 0; y < active; ++y)
    {
        for (std::int32_t x = 0; x < active; ++x)
        {
            active_is_clear = active_is_clear && pixels[static_cast<std::size_t>(y * capacity + x)] == 0U;
        }
    }
    check(active_is_clear, "erasing the same ring restores a transparent active rectangle");
}

void test_invalid_surfaces_are_rejected()
{
    std::vector<std::uint32_t> pixels(16U, 0U);
    const zmouse::render::PixelSurface surface{
        .pixels = pixels,
        .width = 4,
        .height = 4,
        .stride = 4,
    };
    check(!zmouse::render::paint_antialiased_ring(surface, 5, 4, 2, 2, 235),
          "active width beyond capacity is rejected");
    check(!zmouse::render::paint_antialiased_ring(surface, 4, 4, 0, 2, 235), "zero radius is rejected");
    check(!zmouse::render::paint_antialiased_ring(surface, 4, 4, 2, 0, 235), "zero stroke is rejected");
    check(!zmouse::render::paint_crosshair(surface, 4, 4, 0, 1, 1, 235), "zero-length crosshair is rejected");
}

void test_crosshair_has_a_clear_center_and_four_arms()
{
    constexpr std::int32_t size = 32;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(size * size));
    const zmouse::render::PixelSurface surface{.pixels = pixels, .width = size, .height = size, .stride = size};
    check(zmouse::render::paint_crosshair(surface, size, size, 6, 3, 2, 220), "valid crosshair is drawn");
    const auto at = [&](const std::int32_t x, const std::int32_t y)
    { return pixels[static_cast<std::size_t>(y * size + x)]; };
    check(at(16, 16) == 0U, "crosshair center remains clear for the cursor");
    check(at(8, 16) != 0U && at(24, 16) != 0U && at(16, 8) != 0U && at(16, 24) != 0U, "crosshair paints all four arms");
    check(zmouse::render::paint_crosshair(surface, size, size, 6, 3, 2, 0), "crosshair can be erased");
    check(std::ranges::all_of(pixels, [](const std::uint32_t pixel) { return pixel == 0U; }),
          "erasing the crosshair restores transparency");
}

void test_optimized_raster_matches_the_legacy_pixels()
{
    for (const auto [size, radius, stroke] : {std::tuple{64, 20, 4}, std::tuple{96, 35, 5}, std::tuple{128, 51, 7}})
    {
        std::vector<std::uint32_t> actual(static_cast<std::size_t>(size * size));
        const zmouse::render::PixelSurface surface{
            .pixels = actual,
            .width = size,
            .height = size,
            .stride = size,
        };
        check(zmouse::render::paint_antialiased_ring(surface, size, size, radius, stroke, 235),
              "optimized comparison ring is drawn");
        check(actual == legacy_ring(size, radius, stroke), "optimized raster exactly matches legacy pixels");
    }
}
} // namespace

int main()
{
    test_ring_is_drawn_and_erased_without_touching_capacity_padding();
    test_invalid_surfaces_are_rejected();
    test_crosshair_has_a_clear_center_and_four_arms();
    test_optimized_raster_matches_the_legacy_pixels();

    if (failures == 0)
    {
        std::cout << "All ring rasterizer tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}

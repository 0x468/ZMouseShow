#include "zmouse/render/ring_rasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace zmouse::render
{
namespace
{
[[nodiscard]] bool valid_surface(const PixelSurface& surface, const std::int32_t active_width,
                                 const std::int32_t active_height) noexcept
{
    if (surface.width <= 0 || surface.height <= 0 || surface.stride < surface.width || active_width <= 0 ||
        active_height <= 0 || active_width > surface.width || active_height > surface.height)
    {
        return false;
    }

    const auto required = static_cast<std::uint64_t>(surface.stride) * static_cast<std::uint64_t>(surface.height);
    return required <= surface.pixels.size() && required <= (std::numeric_limits<std::size_t>::max)();
}

void paint_span(const PixelSurface& surface, const std::int32_t y, const std::int32_t first_x,
                const std::int32_t last_x, const double center_x, const double center_y, const double radius,
                const double half_stroke, const std::uint8_t maximum_alpha) noexcept
{
    if (first_x > last_x)
    {
        return;
    }

    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(surface.stride);
    const double dy = static_cast<double>(y) - center_y;
    for (std::int32_t x = first_x; x <= last_x; ++x)
    {
        const double dx = static_cast<double>(x) - center_x;
        const double distance = std::hypot(dx, dy);
        const double coverage = (std::clamp)(half_stroke + 1.0 - std::abs(distance - radius), 0.0, 1.0);
        const auto alpha = static_cast<std::uint8_t>(coverage * static_cast<double>(maximum_alpha));
        surface.pixels[row + static_cast<std::size_t>(x)] =
            static_cast<std::uint32_t>(alpha) << 24U | static_cast<std::uint32_t>(alpha) << 16U |
            static_cast<std::uint32_t>(alpha) << 8U | static_cast<std::uint32_t>(alpha);
    }
}
} // namespace

bool paint_antialiased_ring(const PixelSurface& surface, const std::int32_t active_width,
                            const std::int32_t active_height, const std::int32_t radius,
                            const std::int32_t stroke_width, const std::uint8_t maximum_alpha) noexcept
{
    if (!valid_surface(surface, active_width, active_height) || radius <= 0 || stroke_width <= 0)
    {
        return false;
    }

    const double center_x = static_cast<double>(active_width) / 2.0 - 0.5;
    const double center_y = static_cast<double>(active_height) / 2.0 - 0.5;
    const double ring_radius = static_cast<double>(radius);
    const double half_stroke = static_cast<double>(stroke_width) / 2.0;
    const double outer_radius = ring_radius + half_stroke + 1.0;
    const double inner_radius = (std::max)(0.0, ring_radius - half_stroke - 1.0);

    const auto first_y = (std::max)(0, static_cast<std::int32_t>(std::floor(center_y - outer_radius)));
    const auto last_y = (std::min)(active_height - 1, static_cast<std::int32_t>(std::ceil(center_y + outer_radius)));
    for (std::int32_t y = first_y; y <= last_y; ++y)
    {
        const double dy = static_cast<double>(y) - center_y;
        const double outer_squared = outer_radius * outer_radius - dy * dy;
        if (outer_squared < 0.0)
        {
            continue;
        }

        const double outer_x = std::sqrt(outer_squared);
        const auto outer_left = (std::max)(0, static_cast<std::int32_t>(std::floor(center_x - outer_x)));
        const auto outer_right = (std::min)(active_width - 1, static_cast<std::int32_t>(std::ceil(center_x + outer_x)));

        const double inner_squared = inner_radius * inner_radius - dy * dy;
        if (inner_squared <= 0.0)
        {
            paint_span(surface, y, outer_left, outer_right, center_x, center_y, ring_radius, half_stroke,
                       maximum_alpha);
            continue;
        }

        const double inner_x = std::sqrt(inner_squared);
        const auto left_inner = (std::min)(outer_right, static_cast<std::int32_t>(std::ceil(center_x - inner_x)));
        const auto right_inner = (std::max)(outer_left, static_cast<std::int32_t>(std::floor(center_x + inner_x)));
        paint_span(surface, y, outer_left, left_inner, center_x, center_y, ring_radius, half_stroke, maximum_alpha);
        paint_span(surface, y, right_inner, outer_right, center_x, center_y, ring_radius, half_stroke, maximum_alpha);
    }
    return true;
}
} // namespace zmouse::render

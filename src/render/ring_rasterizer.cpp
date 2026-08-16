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

[[nodiscard]] double shape_signed_distance(const overlay::SpotlightShape shape, const double x, const double y,
                                           const double radius) noexcept
{
    const double absolute_x = std::abs(x);
    const double absolute_y = std::abs(y);
    switch (shape)
    {
    case overlay::SpotlightShape::circle:
        return std::hypot(x, y) - radius;
    case overlay::SpotlightShape::rounded_square:
    {
        const double corner_radius = radius / 2.0;
        const double straight_half_extent = radius - corner_radius;
        const double qx = absolute_x - straight_half_extent;
        const double qy = absolute_y - straight_half_extent;
        const double outside = std::hypot((std::max)(qx, 0.0), (std::max)(qy, 0.0));
        const double inside = (std::min)((std::max)(qx, qy), 0.0);
        return outside + inside - corner_radius;
    }
    case overlay::SpotlightShape::diamond:
    {
        const double segment_x = radius;
        const double segment_y = -radius;
        const double from_top_x = absolute_x;
        const double from_top_y = absolute_y - radius;
        const double projection = (std::clamp)((from_top_x * segment_x + from_top_y * segment_y) /
                                                   (segment_x * segment_x + segment_y * segment_y),
                                               0.0, 1.0);
        const double closest_x = projection * segment_x;
        const double closest_y = radius + projection * segment_y;
        const double distance = std::hypot(absolute_x - closest_x, absolute_y - closest_y);
        return absolute_x + absolute_y <= radius ? -distance : distance;
    }
    }
    return 0.0;
}

[[nodiscard]] double shape_half_extent_at_y(const overlay::SpotlightShape shape, const double radius,
                                            const double y) noexcept
{
    const double absolute_y = std::abs(y);
    if (radius <= 0.0 || absolute_y > radius)
    {
        return -1.0;
    }

    switch (shape)
    {
    case overlay::SpotlightShape::circle:
        return std::sqrt((std::max)(0.0, radius * radius - absolute_y * absolute_y));
    case overlay::SpotlightShape::rounded_square:
    {
        const double corner_radius = radius / 2.0;
        const double straight_half_extent = radius - corner_radius;
        if (absolute_y <= straight_half_extent)
        {
            return radius;
        }
        const double corner_y = absolute_y - straight_half_extent;
        return straight_half_extent + std::sqrt((std::max)(0.0, corner_radius * corner_radius - corner_y * corner_y));
    }
    case overlay::SpotlightShape::diamond:
        return radius - absolute_y;
    }
    return -1.0;
}

void paint_shape_span(const PixelSurface& surface, const std::int32_t y, const std::int32_t first_x,
                      const std::int32_t last_x, const double center_x, const double center_y,
                      const overlay::SpotlightShape shape, const double radius, const double half_stroke,
                      const std::uint8_t maximum_alpha) noexcept
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
        const double distance = shape_signed_distance(shape, dx, dy, radius);
        const double coverage = (std::clamp)(half_stroke + 1.0 - std::abs(distance), 0.0, 1.0);
        const auto alpha = static_cast<std::uint8_t>(coverage * static_cast<double>(maximum_alpha));
        surface.pixels[row + static_cast<std::size_t>(x)] =
            static_cast<std::uint32_t>(alpha) << 24U | static_cast<std::uint32_t>(alpha) << 16U |
            static_cast<std::uint32_t>(alpha) << 8U | static_cast<std::uint32_t>(alpha);
    }
}

[[nodiscard]] bool paint_crosshair_pixel(const PixelSurface& surface, const std::int32_t active_width,
                                         const std::int32_t active_height, const std::int32_t arm_length,
                                         const std::int32_t center_gap, const std::int32_t thickness,
                                         const std::uint32_t pixel) noexcept
{
    if (!valid_surface(surface, active_width, active_height) || arm_length <= 0 || center_gap < 0 || thickness <= 0)
    {
        return false;
    }

    const std::int32_t center_x = active_width / 2;
    const std::int32_t center_y = active_height / 2;
    const std::int32_t half_thickness = thickness / 2;
    const auto fill = [&](const std::int32_t left, const std::int32_t top, const std::int32_t right,
                          const std::int32_t bottom) noexcept
    {
        const auto clipped_left = (std::clamp)(left, 0, active_width);
        const auto clipped_top = (std::clamp)(top, 0, active_height);
        const auto clipped_right = (std::clamp)(right, 0, active_width);
        const auto clipped_bottom = (std::clamp)(bottom, 0, active_height);
        for (std::int32_t row_index = clipped_top; row_index < clipped_bottom; ++row_index)
        {
            auto* row = surface.pixels.data() + static_cast<std::size_t>(row_index * surface.stride);
            std::fill(row + clipped_left, row + clipped_right, pixel);
        }
    };

    fill(center_x - center_gap - arm_length, center_y - half_thickness, center_x - center_gap,
         center_y - half_thickness + thickness);
    fill(center_x + center_gap + 1, center_y - half_thickness, center_x + center_gap + arm_length + 1,
         center_y - half_thickness + thickness);
    fill(center_x - half_thickness, center_y - center_gap - arm_length, center_x - half_thickness + thickness,
         center_y - center_gap);
    fill(center_x - half_thickness, center_y + center_gap + 1, center_x - half_thickness + thickness,
         center_y + center_gap + arm_length + 1);
    return true;
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

bool paint_antialiased_outline(const PixelSurface& surface, const std::int32_t active_width,
                               const std::int32_t active_height, const overlay::SpotlightShape shape,
                               const std::int32_t radius, const std::int32_t stroke_width,
                               const std::uint8_t maximum_alpha) noexcept
{
    if (shape == overlay::SpotlightShape::circle)
    {
        return paint_antialiased_ring(surface, active_width, active_height, radius, stroke_width, maximum_alpha);
    }
    if (!valid_surface(surface, active_width, active_height) || radius <= 0 || stroke_width <= 0)
    {
        return false;
    }

    const double center_x = static_cast<double>(active_width) / 2.0 - 0.5;
    const double center_y = static_cast<double>(active_height) / 2.0 - 0.5;
    const double shape_radius = static_cast<double>(radius);
    const double half_stroke = static_cast<double>(stroke_width) / 2.0;
    const double outer_radius = shape_radius + half_stroke + 1.0;
    const double inner_radius = (std::max)(0.0, shape_radius - half_stroke - 1.0);
    const auto first_y = (std::max)(0, static_cast<std::int32_t>(std::floor(center_y - outer_radius)));
    const auto last_y = (std::min)(active_height - 1, static_cast<std::int32_t>(std::ceil(center_y + outer_radius)));

    for (std::int32_t y = first_y; y <= last_y; ++y)
    {
        const double dy = static_cast<double>(y) - center_y;
        const double outer_extent = shape_half_extent_at_y(shape, outer_radius, dy);
        if (outer_extent < 0.0)
        {
            continue;
        }
        const auto outer_left = (std::max)(0, static_cast<std::int32_t>(std::floor(center_x - outer_extent)));
        const auto outer_right =
            (std::min)(active_width - 1, static_cast<std::int32_t>(std::ceil(center_x + outer_extent)));
        const double inner_extent = shape_half_extent_at_y(shape, inner_radius, dy);
        if (inner_extent < 0.0)
        {
            paint_shape_span(surface, y, outer_left, outer_right, center_x, center_y, shape, shape_radius, half_stroke,
                             maximum_alpha);
            continue;
        }

        const auto left_inner = (std::min)(outer_right, static_cast<std::int32_t>(std::ceil(center_x - inner_extent)));
        const auto right_inner = (std::max)(outer_left, static_cast<std::int32_t>(std::floor(center_x + inner_extent)));
        paint_shape_span(surface, y, outer_left, left_inner, center_x, center_y, shape, shape_radius, half_stroke,
                         maximum_alpha);
        paint_shape_span(surface, y, right_inner, outer_right, center_x, center_y, shape, shape_radius, half_stroke,
                         maximum_alpha);
    }
    return true;
}

bool paint_crosshair(const PixelSurface& surface, const std::int32_t active_width, const std::int32_t active_height,
                     const std::int32_t arm_length, const std::int32_t center_gap, const std::int32_t thickness,
                     const std::uint8_t maximum_alpha) noexcept
{
    const auto pixel = static_cast<std::uint32_t>(maximum_alpha) << 24U |
                       static_cast<std::uint32_t>(maximum_alpha) << 16U |
                       static_cast<std::uint32_t>(maximum_alpha) << 8U | static_cast<std::uint32_t>(maximum_alpha);
    return paint_crosshair_pixel(surface, active_width, active_height, arm_length, center_gap, thickness, pixel);
}

bool paint_contrast_crosshair(const PixelSurface& surface, const std::int32_t active_width,
                              const std::int32_t active_height, const std::int32_t arm_length,
                              const std::int32_t center_gap, const std::int32_t thickness,
                              const std::int32_t outline_width, const std::uint8_t maximum_alpha) noexcept
{
    if (outline_width <= 0)
    {
        return false;
    }
    const auto outline_pixel = static_cast<std::uint32_t>(maximum_alpha) << 24U;
    const auto inner_pixel =
        static_cast<std::uint32_t>(maximum_alpha) << 24U | static_cast<std::uint32_t>(maximum_alpha) << 16U |
        static_cast<std::uint32_t>(maximum_alpha) << 8U | static_cast<std::uint32_t>(maximum_alpha);
    const auto outline_gap = (std::max)(0, center_gap - outline_width);
    return paint_crosshair_pixel(surface, active_width, active_height, arm_length + outline_width, outline_gap,
                                 thickness + outline_width * 2, outline_pixel) &&
           paint_crosshair_pixel(surface, active_width, active_height, arm_length, center_gap, thickness, inner_pixel);
}
} // namespace zmouse::render

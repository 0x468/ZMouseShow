#pragma once

#include "zmouse/magnifier/settings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace zmouse::magnifier
{
struct Point
{
    std::int32_t x{};
    std::int32_t y{};
};

struct Rect
{
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};

    [[nodiscard]] constexpr std::int32_t width() const noexcept
    {
        return right - left;
    }
    [[nodiscard]] constexpr std::int32_t height() const noexcept
    {
        return bottom - top;
    }
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return right <= left || bottom <= top;
    }
};

struct Geometry
{
    Rect source{};
    Rect destination{};
    Point hotspot{};
    float scale{1.0F};
};

[[nodiscard]] inline std::int32_t dip_to_pixels(const std::uint32_t dip, const std::uint32_t dpi) noexcept
{
    const auto safe_dpi = std::max<std::uint32_t>(dpi, 1U);
    return std::max<std::int32_t>(
        1, static_cast<std::int32_t>(std::lround(static_cast<double>(dip) * static_cast<double>(safe_dpi) / 96.0)));
}

[[nodiscard]] inline Rect clamp_rect(const Rect rect, const Rect bounds) noexcept
{
    if (bounds.empty() || rect.empty())
    {
        return {};
    }
    const auto width = std::min(rect.width(), bounds.width());
    const auto height = std::min(rect.height(), bounds.height());
    const auto left = std::clamp(rect.left, bounds.left, bounds.right - width);
    const auto top = std::clamp(rect.top, bounds.top, bounds.bottom - height);
    return {left, top, left + width, top + height};
}

// Computes a pixel-aligned source crop and the destination lens bounds. The crop is
// clamped to one output; callers should switch outputs before invoking this function.
[[nodiscard]] inline Geometry compute_geometry(const Point cursor, const Rect output, const std::uint32_t dpi,
                                               const std::uint32_t diameter_dip,
                                               const std::uint32_t zoom_percent) noexcept
{
    const auto destination_size = dip_to_pixels(diameter_dip, dpi);
    const auto zoom = std::clamp(zoom_percent, 125U, 400U);
    const auto source_size = std::max<std::int32_t>(
        1, static_cast<std::int32_t>(std::lround(static_cast<double>(destination_size) * 100.0 / zoom)));
    const Rect unclamped{cursor.x - source_size / 2, cursor.y - source_size / 2, cursor.x + (source_size + 1) / 2,
                         cursor.y + (source_size + 1) / 2};
    const auto source = clamp_rect(unclamped, output);
    // Keep the lens centered on the cursor. The window layer clips the part
    // that falls outside the active output, so the lens can visibly leave the
    // screen at an edge instead of jumping inward.
    const Rect destination{cursor.x - destination_size / 2, cursor.y - destination_size / 2,
                           cursor.x - destination_size / 2 + destination_size,
                           cursor.y - destination_size / 2 + destination_size};
    return {source, destination, cursor, static_cast<float>(zoom) / 100.0F};
}

[[nodiscard]] inline bool contains(const Rect rect, const Point point) noexcept
{
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}
} // namespace zmouse::magnifier

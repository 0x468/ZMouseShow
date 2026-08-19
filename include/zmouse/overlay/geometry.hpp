#pragma once

#include <cstdint>

namespace zmouse::overlay
{
struct Point
{
    std::int32_t x{};
    std::int32_t y{};

    [[nodiscard]] friend constexpr bool operator==(const Point&, const Point&) noexcept = default;
};

struct Rect
{
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
};

[[nodiscard]] constexpr std::int32_t width(const Rect& rect) noexcept
{
    return rect.right - rect.left;
}

[[nodiscard]] constexpr std::int32_t height(const Rect& rect) noexcept
{
    return rect.bottom - rect.top;
}

[[nodiscard]] constexpr Rect hole_bounds_in_monitor(const Point screen_point, const Rect monitor,
                                                    const std::int32_t radius_px) noexcept
{
    const auto local_x = screen_point.x - monitor.left;
    const auto local_y = screen_point.y - monitor.top;
    return {
        .left = local_x - radius_px,
        .top = local_y - radius_px,
        .right = local_x + radius_px,
        .bottom = local_y + radius_px,
    };
}

[[nodiscard]] constexpr std::int32_t dip_to_pixels(const std::int32_t dip, const std::uint32_t dpi) noexcept
{
    const auto safe_dpi = dpi < 1 ? std::uint32_t{1} : dpi;
    const auto raw = static_cast<std::int32_t>((static_cast<std::int64_t>(dip) * safe_dpi + 48) / 96);
    return raw < 1 ? 1 : raw;
}
} // namespace zmouse::overlay

#pragma once

#include <cstdint>

namespace zmouse::magnifier
{
enum class Shape
{
    circle,
    rounded_rectangle,
};

enum class FollowMode
{
    centered,
};

enum class EdgeEffect
{
    off,
    subtle,
};

struct Settings
{
    bool enabled{};
    std::uint32_t zoom_percent{200};
    std::uint32_t diameter_dip{280};
    Shape shape{Shape::circle};
    FollowMode follow_mode{FollowMode::centered};
    EdgeEffect edge_effect{EdgeEffect::subtle};
};

[[nodiscard]] constexpr bool valid(const Settings& settings) noexcept
{
    return settings.zoom_percent >= 125 && settings.zoom_percent <= 400 && settings.diameter_dip >= 160 &&
           settings.diameter_dip <= 640;
}
} // namespace zmouse::magnifier

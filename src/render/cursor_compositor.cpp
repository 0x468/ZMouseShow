#include "zmouse/render/cursor_compositor.hpp"

#include <algorithm>

namespace zmouse::render
{
std::uint32_t compose_cursor_pixel(const std::uint32_t over_black, const std::uint32_t over_white) noexcept
{
    const auto black_blue = static_cast<std::uint8_t>(over_black);
    const auto black_green = static_cast<std::uint8_t>(over_black >> 8U);
    const auto black_red = static_cast<std::uint8_t>(over_black >> 16U);
    const auto white_blue = static_cast<std::uint8_t>(over_white);
    const auto white_green = static_cast<std::uint8_t>(over_white >> 8U);
    const auto white_red = static_cast<std::uint8_t>(over_white >> 16U);

    const auto background_contribution = (std::max)({static_cast<int>(white_blue) - static_cast<int>(black_blue),
                                                     static_cast<int>(white_green) - static_cast<int>(black_green),
                                                     static_cast<int>(white_red) - static_cast<int>(black_red), 0});
    const auto alpha = static_cast<std::uint8_t>(255 - (std::min)(background_contribution, 255));
    const auto blue = (std::min)(black_blue, alpha);
    const auto green = (std::min)(black_green, alpha);
    const auto red = (std::min)(black_red, alpha);
    return static_cast<std::uint32_t>(alpha) << 24U | static_cast<std::uint32_t>(red) << 16U |
           static_cast<std::uint32_t>(green) << 8U | static_cast<std::uint32_t>(blue);
}

bool compose_cursor_images(const std::span<const std::uint32_t> over_black,
                           const std::span<const std::uint32_t> over_white,
                           const std::span<std::uint32_t> destination) noexcept
{
    if (over_black.size() != over_white.size() || over_black.size() != destination.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < destination.size(); ++index)
    {
        destination[index] = compose_cursor_pixel(over_black[index], over_white[index]);
    }
    return true;
}
} // namespace zmouse::render

#pragma once

#include <cstdint>
#include <span>

namespace zmouse::render
{
[[nodiscard]] std::uint32_t compose_cursor_pixel(std::uint32_t over_black, std::uint32_t over_white) noexcept;
[[nodiscard]] bool compose_cursor_images(std::span<const std::uint32_t> over_black,
                                         std::span<const std::uint32_t> over_white,
                                         std::span<std::uint32_t> destination) noexcept;
} // namespace zmouse::render

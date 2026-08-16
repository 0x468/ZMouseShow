#pragma once

#include "zmouse/overlay/spotlight_shape.hpp"
#include <cstdint>
#include <span>

namespace zmouse::render
{
struct PixelSurface
{
    std::span<std::uint32_t> pixels;
    std::int32_t width{};
    std::int32_t height{};
    std::int32_t stride{};
};

[[nodiscard]] bool paint_antialiased_ring(const PixelSurface& surface, std::int32_t active_width,
                                          std::int32_t active_height, std::int32_t radius, std::int32_t stroke_width,
                                          std::uint8_t maximum_alpha) noexcept;
[[nodiscard]] bool paint_antialiased_outline(const PixelSurface& surface, std::int32_t active_width,
                                             std::int32_t active_height, overlay::SpotlightShape shape,
                                             std::int32_t radius, std::int32_t stroke_width,
                                             std::uint8_t maximum_alpha) noexcept;
[[nodiscard]] bool paint_crosshair(const PixelSurface& surface, std::int32_t active_width, std::int32_t active_height,
                                   std::int32_t arm_length, std::int32_t center_gap, std::int32_t thickness,
                                   std::uint8_t maximum_alpha) noexcept;
[[nodiscard]] bool paint_contrast_crosshair(const PixelSurface& surface, std::int32_t active_width,
                                            std::int32_t active_height, std::int32_t arm_length,
                                            std::int32_t center_gap, std::int32_t thickness, std::int32_t outline_width,
                                            std::uint8_t maximum_alpha) noexcept;
} // namespace zmouse::render

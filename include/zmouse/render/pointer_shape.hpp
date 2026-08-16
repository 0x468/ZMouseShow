#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace zmouse::render
{
enum class PointerShapeType
{
    color,
    monochrome,
    masked_color,
};

struct PointerShape
{
    PointerShapeType type{PointerShapeType::color};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t pitch{};
    std::int32_t hotspot_x{};
    std::int32_t hotspot_y{};
    std::span<const std::byte> pixels;
};

// Composes a Desktop Duplication pointer shape over opaque BGRA desktop
// pixels. Monochrome masks preserve the Win32 AND/XOR behavior, including the
// background-dependent I-beam colors.
[[nodiscard]] bool compose_pointer_shape(const PointerShape& shape, std::span<const std::uint32_t> background,
                                         std::span<std::uint32_t> output) noexcept;
} // namespace zmouse::render

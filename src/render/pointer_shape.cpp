#include "zmouse/render/pointer_shape.hpp"

#include <limits>

namespace zmouse::render
{
namespace
{
[[nodiscard]] constexpr std::uint8_t channel(const std::uint32_t pixel, const std::uint32_t shift) noexcept
{
    return static_cast<std::uint8_t>((pixel >> shift) & 0xFFU);
}

[[nodiscard]] constexpr std::uint8_t blend_channel(const std::uint8_t foreground, const std::uint8_t background,
                                                   const std::uint8_t alpha) noexcept
{
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(foreground) * alpha +
                                      static_cast<std::uint32_t>(background) * (255U - alpha) + 127U) /
                                     255U);
}

[[nodiscard]] constexpr std::uint32_t alpha_blend(const std::uint32_t foreground,
                                                  const std::uint32_t background) noexcept
{
    const auto alpha = channel(foreground, 24U);
    const auto blue = blend_channel(channel(foreground, 0U), channel(background, 0U), alpha);
    const auto green = blend_channel(channel(foreground, 8U), channel(background, 8U), alpha);
    const auto red = blend_channel(channel(foreground, 16U), channel(background, 16U), alpha);
    return 0xFF000000U | static_cast<std::uint32_t>(red) << 16U | static_cast<std::uint32_t>(green) << 8U | blue;
}

[[nodiscard]] bool payload_size(const PointerShape& shape, const std::uint32_t planes, std::size_t& required) noexcept
{
    if (shape.width == 0 || shape.height == 0 || shape.pitch == 0 ||
        shape.height > (std::numeric_limits<std::size_t>::max)() / shape.pitch)
    {
        return false;
    }
    required = static_cast<std::size_t>(shape.pitch) * shape.height;
    if (planes > 1 && required > (std::numeric_limits<std::size_t>::max)() / planes)
    {
        return false;
    }
    required *= planes;
    return required <= shape.pixels.size();
}

[[nodiscard]] bool mask_bit(const PointerShape& shape, const std::size_t plane_offset, const std::uint32_t x,
                            const std::uint32_t y) noexcept
{
    const auto byte =
        std::to_integer<std::uint8_t>(shape.pixels[plane_offset + static_cast<std::size_t>(y) * shape.pitch + x / 8U]);
    return (byte & (0x80U >> (x % 8U))) != 0;
}
} // namespace

bool compose_pointer_shape(const PointerShape& shape, const std::span<const std::uint32_t> background,
                           const std::span<std::uint32_t> output) noexcept
{
    if (shape.width == 0 || shape.height == 0 || shape.width > (std::numeric_limits<std::size_t>::max)() / shape.height)
    {
        return false;
    }
    const auto pixel_count = static_cast<std::size_t>(shape.width) * shape.height;
    if (background.size() != pixel_count || output.size() != pixel_count)
    {
        return false;
    }

    std::size_t required = 0;
    const auto planes = shape.type == PointerShapeType::monochrome ? 2U : 1U;
    if (!payload_size(shape, planes, required))
    {
        return false;
    }
    if (shape.type != PointerShapeType::monochrome &&
        static_cast<std::size_t>(shape.pitch) < static_cast<std::size_t>(shape.width) * sizeof(std::uint32_t))
    {
        return false;
    }
    if (shape.type == PointerShapeType::monochrome && shape.pitch < (shape.width + 7U) / 8U)
    {
        return false;
    }

    const auto xor_offset = static_cast<std::size_t>(shape.pitch) * shape.height;
    for (std::uint32_t y = 0; y < shape.height; ++y)
    {
        for (std::uint32_t x = 0; x < shape.width; ++x)
        {
            const auto index = static_cast<std::size_t>(y) * shape.width + x;
            const auto desktop = background[index] | 0xFF000000U;
            if (shape.type == PointerShapeType::monochrome)
            {
                const auto and_mask = mask_bit(shape, 0, x, y) ? 0x00FFFFFFU : 0U;
                const auto xor_mask = mask_bit(shape, xor_offset, x, y) ? 0x00FFFFFFU : 0U;
                output[index] = 0xFF000000U | ((desktop & and_mask) ^ xor_mask);
                continue;
            }

            const auto byte_offset = static_cast<std::size_t>(y) * shape.pitch + x * sizeof(std::uint32_t);
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(shape.pixels.data() + byte_offset);
            const auto pointer = static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8U |
                                 static_cast<std::uint32_t>(bytes[2]) << 16U |
                                 static_cast<std::uint32_t>(bytes[3]) << 24U;
            if (shape.type == PointerShapeType::masked_color)
            {
                output[index] = channel(pointer, 24U) == 0 ? 0xFF000000U | (pointer & 0x00FFFFFFU)
                                                           : desktop ^ (pointer & 0x00FFFFFFU);
            }
            else
            {
                output[index] = alpha_blend(pointer, desktop);
            }
        }
    }
    return true;
}
} // namespace zmouse::render

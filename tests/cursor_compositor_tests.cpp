#include "zmouse/render/cursor_compositor.hpp"
#include "zmouse/render/pointer_shape.hpp"
#include <array>
#include <iostream>
#include <string_view>

namespace
{
int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void test_pixel_reconstruction()
{
    check(zmouse::render::compose_cursor_pixel(0x00000000U, 0x00FFFFFFU) == 0x00000000U,
          "transparent cursor pixels remain transparent");
    check(zmouse::render::compose_cursor_pixel(0x00000000U, 0x00000000U) == 0xFF000000U,
          "opaque black cursor pixels retain full alpha");
    check(zmouse::render::compose_cursor_pixel(0x00FFFFFFU, 0x00FFFFFFU) == 0xFFFFFFFFU,
          "opaque white cursor pixels retain full alpha");
    check(zmouse::render::compose_cursor_pixel(0x00800000U, 0x00FF7F7FU) == 0x80800000U,
          "semi-transparent red cursor pixels are reconstructed as premultiplied BGRA");
}

void test_image_validation_and_composition()
{
    constexpr std::array black{0x00000000U, 0x00FFFFFFU};
    constexpr std::array white{0x00FFFFFFU, 0x00FFFFFFU};
    std::array<std::uint32_t, 2> output{};
    check(zmouse::render::compose_cursor_images(black, white, output), "equal-sized cursor images compose");
    check(output[0] == 0x00000000U && output[1] == 0xFFFFFFFFU,
          "cursor image composition reconstructs transparent and opaque pixels");
    check(!zmouse::render::compose_cursor_images(std::span(black).first(1), white, output),
          "mismatched cursor image sizes are rejected");
}

void test_desktop_duplication_pointer_shapes()
{
    using zmouse::render::PointerShape;
    using zmouse::render::PointerShapeType;
    constexpr std::array<std::uint32_t, 4> background{
        0xFF102030U,
        0xFFFFFFFFU,
        0xFF000000U,
        0xFF123456U,
    };
    std::array<std::uint32_t, 4> output{};

    // AND: 0,0,1,1; XOR: 0,1,0,1 => black, white, unchanged, inverted.
    constexpr std::array monochrome_bytes{std::byte{0x30}, std::byte{0x50}};
    const PointerShape monochrome{
        .type = PointerShapeType::monochrome,
        .width = 4,
        .height = 1,
        .pitch = 1,
        .pixels = monochrome_bytes,
    };
    check(zmouse::render::compose_pointer_shape(monochrome, background, output), "monochrome pointer masks compose");
    check(output[0] == 0xFF000000U && output[1] == 0xFFFFFFFFU && output[2] == background[2] &&
              output[3] == 0xFFEDCBA9U,
          "AND/XOR pointer semantics preserve light and dark background behavior");

    constexpr std::array masked_bytes{
        std::byte{0x33}, std::byte{0x22}, std::byte{0x11}, std::byte{0x00},
        std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
    };
    const PointerShape masked{
        .type = PointerShapeType::masked_color,
        .width = 2,
        .height = 1,
        .pitch = 8,
        .pixels = masked_bytes,
    };
    check(zmouse::render::compose_pointer_shape(masked, std::span(background).first(2), std::span(output).first(2)),
          "masked-color pointer shapes compose");
    check(output[0] == 0xFF112233U && output[1] == 0xFFFFFF00U,
          "masked-color pixels replace or XOR the desktop according to alpha");

    constexpr std::array color_bytes{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0xFF},
        std::byte{0x80},
    };
    const PointerShape color{
        .type = PointerShapeType::color,
        .width = 1,
        .height = 1,
        .pitch = 4,
        .pixels = color_bytes,
    };
    check(zmouse::render::compose_pointer_shape(color, std::span(background).first(1), std::span(output).first(1)),
          "color pointer shapes alpha blend");
    check(output[0] == 0xFF881018U, "color pointer alpha is blended over the captured desktop");
}
} // namespace

int main()
{
    test_pixel_reconstruction();
    test_image_validation_and_composition();
    test_desktop_duplication_pointer_shapes();
    if (failures == 0)
    {
        std::cout << "All cursor compositor tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}

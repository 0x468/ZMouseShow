#include "zmouse/render/cursor_compositor.hpp"
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
} // namespace

int main()
{
    test_pixel_reconstruction();
    test_image_validation_and_composition();
    if (failures == 0)
    {
        std::cout << "All cursor compositor tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}

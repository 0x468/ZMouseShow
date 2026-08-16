#include "zmouse/platform/spotlight_region.hpp"

#include <algorithm>
#include <array>

namespace zmouse::platform
{
HRGN create_spotlight_region(const overlay::Rect& bounds, const overlay::SpotlightShape shape) noexcept
{
    const auto width = overlay::width(bounds);
    const auto height = overlay::height(bounds);
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    switch (shape)
    {
    case overlay::SpotlightShape::circle:
        return CreateEllipticRgn(bounds.left, bounds.top, bounds.right, bounds.bottom);
    case overlay::SpotlightShape::rounded_square:
    {
        const auto corner_diameter = (std::max)(1L, (std::min)(width, height) / 2L);
        return CreateRoundRectRgn(bounds.left, bounds.top, bounds.right, bounds.bottom, corner_diameter,
                                  corner_diameter);
    }
    case overlay::SpotlightShape::diamond:
    {
        const LONG center_x = bounds.left + width / 2L;
        const LONG center_y = bounds.top + height / 2L;
        const std::array points{
            POINT{center_x, bounds.top},
            POINT{bounds.right - 1L, center_y},
            POINT{center_x, bounds.bottom - 1L},
            POINT{bounds.left, center_y},
        };
        return CreatePolygonRgn(points.data(), static_cast<int>(points.size()), WINDING);
    }
    }
    return nullptr;
}
} // namespace zmouse::platform

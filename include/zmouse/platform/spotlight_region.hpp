#pragma once

#include <windows.h>

#include "zmouse/overlay/geometry.hpp"
#include "zmouse/overlay/spotlight_shape.hpp"

namespace zmouse::platform
{
[[nodiscard]] HRGN create_spotlight_region(const overlay::Rect& bounds, overlay::SpotlightShape shape) noexcept;
} // namespace zmouse::platform

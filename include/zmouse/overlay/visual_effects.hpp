#pragma once

#include <cstdint>

namespace zmouse::overlay
{
struct VisualEffects
{
    bool focus_ring_enabled{true};
    bool ripple_enabled{true};
    bool crosshair_enabled{};
    bool enlarged_cursor_enabled{};
    std::uint32_t cursor_scale_percent{200};
};
} // namespace zmouse::overlay

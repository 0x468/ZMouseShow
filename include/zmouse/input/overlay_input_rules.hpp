#pragma once

namespace zmouse::input
{
struct OverlayInputState
{
    bool paused{};
    bool activation_pending{};
    bool overlay_visible{};
    bool tray_menu_open{};
    bool settings_dialog_open{};
};

[[nodiscard]] constexpr bool triggers_armed(const OverlayInputState state) noexcept
{
    return !state.paused && !state.activation_pending && !state.overlay_visible && !state.tray_menu_open &&
           !state.settings_dialog_open;
}

[[nodiscard]] constexpr bool should_dismiss_overlay_for_key_event(const bool overlay_visible, const bool pressed,
                                                                  const bool repeated) noexcept
{
    return overlay_visible && pressed && !repeated;
}
} // namespace zmouse::input

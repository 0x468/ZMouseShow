#pragma once

#include <windows.h>

#include "zmouse/overlay/geometry.hpp"
#include "zmouse/overlay/spotlight_shape.hpp"
#include "zmouse/overlay/visual_effects.hpp"
#include <cstdint>
#include <vector>

namespace zmouse::platform
{
class OverlayManager final
{
  public:
    OverlayManager() = default;
    OverlayManager(const OverlayManager&) = delete;
    OverlayManager& operator=(const OverlayManager&) = delete;
    ~OverlayManager();

    void configure(std::int32_t spotlight_radius_dip, overlay::SpotlightShape spotlight_shape,
                   const overlay::VisualEffects& effects, std::uint32_t dim_opacity_percent) noexcept;
    [[nodiscard]] bool initialize(HINSTANCE instance);
    [[nodiscard]] bool rebuild();
    [[nodiscard]] bool show_at(overlay::Point cursor);
    void move_to(overlay::Point cursor);
    void set_animation_frame(double dim_progress, double focus_opacity, double ripple_scale, double ripple_opacity);
    void hide() noexcept;

    [[nodiscard]] bool visible() const noexcept;

  private:
    struct MonitorOverlay
    {
        HMONITOR monitor{};
        RECT bounds{};
        HWND window{};
        UINT dpi{96};
    };

    static LRESULT CALLBACK overlay_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) noexcept;
    static LRESULT CALLBACK ring_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) noexcept;
    static BOOL CALLBACK enum_monitor_proc(HMONITOR monitor, HDC, LPRECT, LPARAM data) noexcept;

    [[nodiscard]] bool register_window_classes();
    [[nodiscard]] bool create_monitor_overlay(HMONITOR monitor);
    [[nodiscard]] bool create_ring_window();
    [[nodiscard]] bool create_cursor_window();
    [[nodiscard]] bool apply_hole(MonitorOverlay& overlay, overlay::Point cursor, std::int32_t radius_px);
    [[nodiscard]] bool ensure_ring_bitmap(std::int32_t base_radius_px, double scale);
    [[nodiscard]] bool ensure_cursor_bitmap(UINT dpi);
    [[nodiscard]] bool update_ring_position(overlay::Point cursor) const noexcept;
    [[nodiscard]] bool update_cursor_position(overlay::Point cursor);
    [[nodiscard]] UINT dpi_at(overlay::Point cursor) const noexcept;
    void apply_dim_progress() const noexcept;
    void destroy_windows() noexcept;
    void destroy_ring_bitmap() noexcept;
    void destroy_cursor_bitmap() noexcept;

    HINSTANCE instance_{};
    ATOM overlay_class_{};
    ATOM ring_class_{};
    std::vector<MonitorOverlay> overlays_;
    HWND ring_window_{};
    HDC ring_dc_{};
    HBITMAP ring_bitmap_{};
    HGDIOBJ ring_old_bitmap_{};
    std::uint32_t* ring_pixels_{};
    SIZE ring_bitmap_capacity_{};
    SIZE ring_size_{};
    std::int32_t ring_base_radius_px_{};
    std::int32_t painted_ripple_radius_px_{};
    std::int32_t ring_stroke_px_{};
    std::uint8_t painted_focus_alpha_{};
    std::uint8_t painted_ripple_alpha_{};
    std::uint8_t painted_crosshair_alpha_{};
    HWND cursor_window_{};
    HDC cursor_dc_{};
    HBITMAP cursor_bitmap_{};
    HGDIOBJ cursor_old_bitmap_{};
    std::uint32_t* cursor_pixels_{};
    SIZE cursor_size_{};
    POINT cursor_hotspot_{};
    HCURSOR rendered_cursor_{};
    UINT rendered_cursor_dpi_{};
    std::uint32_t rendered_cursor_scale_percent_{};
    bool cursor_drawable_{};
    std::int32_t spotlight_radius_dip_{120};
    overlay::SpotlightShape spotlight_shape_{overlay::SpotlightShape::circle};
    overlay::VisualEffects effects_{};
    BYTE dim_alpha_{153};
    double dim_progress_{1.0};
    double focus_opacity_{1.0};
    double ripple_scale_{1.0};
    double ripple_opacity_{};
    overlay::Point last_cursor_{};
    bool visible_{};
};
} // namespace zmouse::platform

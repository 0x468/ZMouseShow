#pragma once

#include <windows.h>

#include "zmouse/overlay/geometry.hpp"
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

    [[nodiscard]] bool initialize(HINSTANCE instance);
    [[nodiscard]] bool rebuild();
    [[nodiscard]] bool show_at(overlay::Point cursor);
    void move_to(overlay::Point cursor);
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
    [[nodiscard]] bool apply_hole(MonitorOverlay& overlay, overlay::Point cursor, std::int32_t radius_px);
    [[nodiscard]] bool ensure_ring_bitmap(std::int32_t radius_px);
    [[nodiscard]] bool update_ring_position(overlay::Point cursor) const noexcept;
    [[nodiscard]] UINT dpi_at(overlay::Point cursor) const noexcept;
    void destroy_windows() noexcept;
    void destroy_ring_bitmap() noexcept;

    HINSTANCE instance_{};
    ATOM overlay_class_{};
    ATOM ring_class_{};
    std::vector<MonitorOverlay> overlays_;
    HWND ring_window_{};
    HDC ring_dc_{};
    HBITMAP ring_bitmap_{};
    HGDIOBJ ring_old_bitmap_{};
    SIZE ring_size_{};
    std::int32_t ring_radius_px_{};
    bool visible_{};
};
} // namespace zmouse::platform

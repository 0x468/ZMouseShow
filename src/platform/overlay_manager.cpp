#include "overlay_manager.hpp"

#include <windows.h>

#include "zmouse/platform/spotlight_region.hpp"
#include "zmouse/render/cursor_compositor.hpp"
#include "zmouse/render/ring_rasterizer.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <magnification.h>
#include <span>

namespace zmouse::platform
{
namespace
{
constexpr wchar_t overlay_class_name[] = L"ZMouseShow.MonitorOverlay";
constexpr wchar_t ring_class_name[] = L"ZMouseShow.CursorRing";
constexpr std::int32_t ring_margin_dip = 18;
constexpr std::int32_t ring_stroke_dip = 5;
constexpr std::int32_t maximum_animated_ring_radius_px = 768;

[[nodiscard]] std::int32_t animated_ring_radius(const std::int32_t base_radius_px, const double scale) noexcept
{
    const auto requested =
        static_cast<std::int32_t>(std::lround(static_cast<double>(base_radius_px) * (std::clamp)(scale, 1.0, 4.0)));
    return (std::clamp)(requested, base_radius_px, (std::max)(base_radius_px, maximum_animated_ring_radius_px));
}

} // namespace

OverlayManager::DibSurface::DibSurface(DibSurface&& other) noexcept
    : dc(other.dc), bitmap(other.bitmap), old_bitmap(other.old_bitmap), pixels(other.pixels), size(other.size)
{
    other.dc = nullptr;
    other.bitmap = nullptr;
    other.old_bitmap = nullptr;
    other.pixels = nullptr;
    other.size = {};
}

OverlayManager::DibSurface& OverlayManager::DibSurface::operator=(DibSurface&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        dc = other.dc;
        bitmap = other.bitmap;
        old_bitmap = other.old_bitmap;
        pixels = other.pixels;
        size = other.size;
        other.dc = nullptr;
        other.bitmap = nullptr;
        other.old_bitmap = nullptr;
        other.pixels = nullptr;
        other.size = {};
    }
    return *this;
}

bool OverlayManager::DibSurface::create(const SIZE s) noexcept
{
    destroy();
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = s.cx;
    bitmap_info.bmiHeader.biHeight = -s.cy;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* pixel_memory = nullptr;
    bitmap = CreateDIBSection(nullptr, &bitmap_info, DIB_RGB_COLORS, &pixel_memory, nullptr, 0);
    dc = CreateCompatibleDC(nullptr);
    if (bitmap == nullptr || dc == nullptr || pixel_memory == nullptr)
    {
        destroy();
        return false;
    }
    old_bitmap = SelectObject(dc, bitmap);
    if (old_bitmap == nullptr || old_bitmap == HGDI_ERROR)
    {
        destroy();
        return false;
    }
    pixels = static_cast<std::uint32_t*>(pixel_memory);
    size = s;
    return true;
}

void OverlayManager::DibSurface::destroy() noexcept
{
    if (dc != nullptr && old_bitmap != nullptr && old_bitmap != HGDI_ERROR)
    {
        static_cast<void>(SelectObject(dc, old_bitmap));
    }
    if (bitmap != nullptr)
    {
        static_cast<void>(DeleteObject(bitmap));
    }
    if (dc != nullptr)
    {
        static_cast<void>(DeleteDC(dc));
    }
    dc = nullptr;
    bitmap = nullptr;
    old_bitmap = nullptr;
    pixels = nullptr;
    size = {};
}

OverlayManager::~OverlayManager()
{
    restore_system_cursor();
    destroy_windows();
    destroy_ring_bitmap();
    destroy_cursor_bitmap();
    if (ring_class_ != 0)
    {
        UnregisterClassW(ring_class_name, instance_);
    }
    if (overlay_class_ != 0)
    {
        UnregisterClassW(overlay_class_name, instance_);
    }
    uninitialize_magnification();
}

void OverlayManager::configure(const std::int32_t spotlight_radius_dip, const overlay::SpotlightShape spotlight_shape,
                               const overlay::VisualEffects& effects, const std::uint32_t dim_opacity_percent,
                               const bool dim_enabled) noexcept
{
    spotlight_radius_dip_ = (std::clamp)(spotlight_radius_dip, 32, 512);
    spotlight_shape_ = spotlight_shape;
    effects_ = effects;
    if (!effects_.enlarged_cursor_enabled)
    {
        restore_system_cursor();
    }
    const auto opacity = (std::clamp)(dim_opacity_percent, 10U, 90U);
    dim_alpha_ = dim_enabled ? static_cast<BYTE>((opacity * 255U + 50U) / 100U) : 0U;
    destroy_ring_bitmap();
    destroy_cursor_bitmap();

    apply_dim_progress();

    if (visible_)
    {
        POINT cursor{};
        if (GetCursorPos(&cursor) != FALSE)
        {
            move_to({cursor.x, cursor.y});
        }
    }
}

bool OverlayManager::initialize(const HINSTANCE instance)
{
    instance_ = instance;
    return register_window_classes() && rebuild();
}

bool OverlayManager::rebuild()
{
    const bool was_visible = visible_;
    POINT cursor{};
    static_cast<void>(GetCursorPos(&cursor));

    hide();
    destroy_windows();

    if (EnumDisplayMonitors(nullptr, nullptr, enum_monitor_proc, reinterpret_cast<LPARAM>(this)) == FALSE ||
        overlays_.empty() || !create_ring_window() || !create_cursor_window())
    {
        destroy_windows();
        return false;
    }

    if (was_visible)
    {
        return show_at({cursor.x, cursor.y});
    }
    return true;
}

bool OverlayManager::show_at(const overlay::Point cursor)
{
    const auto radius_px = overlay::dip_to_pixels(spotlight_radius_dip_, dpi_at(cursor));
    if (!ensure_ring_bitmap(radius_px, ripple_scale_))
    {
        return false;
    }

    apply_dim_progress();
    for (auto& monitor_overlay : overlays_)
    {
        if (!apply_hole(monitor_overlay, cursor, radius_px))
        {
            hide();
            return false;
        }

        const auto width = monitor_overlay.bounds.right - monitor_overlay.bounds.left;
        const auto height = monitor_overlay.bounds.bottom - monitor_overlay.bounds.top;
        static_cast<void>(SetWindowPos(monitor_overlay.window, HWND_TOPMOST, monitor_overlay.bounds.left,
                                       monitor_overlay.bounds.top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW));
        static_cast<void>(RedrawWindow(monitor_overlay.window, nullptr, nullptr,
                                       RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN));
    }

    if (!update_ring_position(cursor) || !update_cursor_position(cursor))
    {
        hide();
        return false;
    }

    last_cursor_ = cursor;
    visible_ = true;
    return true;
}

void OverlayManager::move_to(const overlay::Point cursor)
{
    if (!visible_)
    {
        return;
    }

    const auto radius_px = overlay::dip_to_pixels(spotlight_radius_dip_, dpi_at(cursor));
    if (!ensure_ring_bitmap(radius_px, ripple_scale_))
    {
        hide();
        return;
    }

    for (auto& monitor_overlay : overlays_)
    {
        if (!apply_hole(monitor_overlay, cursor, radius_px))
        {
            hide();
            return;
        }
    }

    last_cursor_ = cursor;
    if (!update_ring_position(cursor) || !update_cursor_position(cursor))
    {
        hide();
    }
}

void OverlayManager::set_animation_frame(const double dim_progress, const double focus_opacity,
                                         const double ripple_scale, const double ripple_opacity)
{
    dim_progress_ = (std::clamp)(dim_progress, 0.0, 1.0);
    focus_opacity_ = (std::clamp)(focus_opacity, 0.0, 1.0);
    ripple_scale_ = (std::clamp)(ripple_scale, 1.0, 2.0);
    ripple_opacity_ = (std::clamp)(ripple_opacity, 0.0, 1.0);
    apply_dim_progress();

    if (!visible_)
    {
        return;
    }

    const auto radius_px = overlay::dip_to_pixels(spotlight_radius_dip_, dpi_at(last_cursor_));
    if (!ensure_ring_bitmap(radius_px, ripple_scale_) || !update_ring_position(last_cursor_) ||
        !update_cursor_position(last_cursor_))
    {
        hide();
    }
}

void OverlayManager::hide() noexcept
{
    if (ring_window_ != nullptr)
    {
        ShowWindow(ring_window_, SW_HIDE);
    }
    if (cursor_window_ != nullptr)
    {
        ShowWindow(cursor_window_, SW_HIDE);
    }
    restore_system_cursor();
    for (const auto& overlay : overlays_)
    {
        ShowWindow(overlay.window, SW_HIDE);
    }
    visible_ = false;
    ring_window_pos_ = {-1, -1};
}

bool OverlayManager::visible() const noexcept
{
    return visible_;
}

bool OverlayManager::exclude_from_capture() const noexcept
{
    if (ring_window_ == nullptr || cursor_window_ == nullptr)
    {
        return false;
    }
    bool applied = true;
    for (const auto& overlay : overlays_)
    {
        applied = SetWindowDisplayAffinity(overlay.window, WDA_EXCLUDEFROMCAPTURE) != FALSE && applied;
    }
    applied = SetWindowDisplayAffinity(ring_window_, WDA_EXCLUDEFROMCAPTURE) != FALSE && applied;
    applied = SetWindowDisplayAffinity(cursor_window_, WDA_EXCLUDEFROMCAPTURE) != FALSE && applied;
    return applied;
}

LRESULT CALLBACK OverlayManager::overlay_window_proc(const HWND window, const UINT message, const WPARAM w_param,
                                                     const LPARAM l_param) noexcept
{
    switch (message)
    {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_ERASEBKGND:
    {
        RECT client{};
        static_cast<void>(GetClientRect(window, &client));
        static_cast<void>(
            FillRect(reinterpret_cast<HDC>(w_param), &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))));
        return 1;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        static_cast<void>(FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))));
        EndPaint(window, &paint);
        return 0;
    }
    default:
        return DefWindowProcW(window, message, w_param, l_param);
    }
}

LRESULT CALLBACK OverlayManager::ring_window_proc(const HWND window, const UINT message, const WPARAM w_param,
                                                  const LPARAM l_param) noexcept
{
    if (message == WM_NCHITTEST)
    {
        return HTTRANSPARENT;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

BOOL CALLBACK OverlayManager::enum_monitor_proc(const HMONITOR monitor, HDC, LPRECT, const LPARAM data) noexcept
{
    auto* manager = reinterpret_cast<OverlayManager*>(data);
    return manager->create_monitor_overlay(monitor) ? TRUE : FALSE;
}

bool OverlayManager::register_window_classes()
{
    WNDCLASSEXW overlay_class{};
    overlay_class.cbSize = sizeof(overlay_class);
    overlay_class.lpfnWndProc = overlay_window_proc;
    overlay_class.hInstance = instance_;
    overlay_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    overlay_class.lpszClassName = overlay_class_name;
    overlay_class_ = RegisterClassExW(&overlay_class);
    if (overlay_class_ == 0)
    {
        return false;
    }

    WNDCLASSEXW ring_class{};
    ring_class.cbSize = sizeof(ring_class);
    ring_class.lpfnWndProc = ring_window_proc;
    ring_class.hInstance = instance_;
    ring_class.lpszClassName = ring_class_name;
    ring_class_ = RegisterClassExW(&ring_class);
    return ring_class_ != 0;
}

bool OverlayManager::create_monitor_overlay(const HMONITOR monitor)
{
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE)
    {
        return false;
    }

    const auto width = info.rcMonitor.right - info.rcMonitor.left;
    const auto height = info.rcMonitor.bottom - info.rcMonitor.top;
    HWND window = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
                                  overlay_class_name, nullptr, WS_POPUP, info.rcMonitor.left, info.rcMonitor.top, width,
                                  height, nullptr, nullptr, instance_, nullptr);
    if (window == nullptr || SetLayeredWindowAttributes(window, 0, dim_alpha_, LWA_ALPHA) == FALSE)
    {
        if (window != nullptr)
        {
            DestroyWindow(window);
        }
        return false;
    }

    const UINT window_dpi = GetDpiForWindow(window);
    overlays_.push_back(
        {.monitor = monitor, .bounds = info.rcMonitor, .window = window, .dpi = window_dpi == 0 ? 96U : window_dpi});
    return true;
}

bool OverlayManager::create_ring_window()
{
    ring_window_ =
        CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, ring_class_name,
                        nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, nullptr);
    return ring_window_ != nullptr;
}

bool OverlayManager::create_cursor_window()
{
    cursor_window_ =
        CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, ring_class_name,
                        nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, nullptr);
    return cursor_window_ != nullptr;
}

bool OverlayManager::apply_hole(MonitorOverlay& monitor_overlay, const overlay::Point cursor,
                                const std::int32_t radius_px)
{
    const overlay::Rect monitor_bounds{
        .left = monitor_overlay.bounds.left,
        .top = monitor_overlay.bounds.top,
        .right = monitor_overlay.bounds.right,
        .bottom = monitor_overlay.bounds.bottom,
    };
    const auto hole = overlay::hole_bounds_in_monitor(cursor, monitor_bounds, radius_px);
    const auto monitor_width = overlay::width(monitor_bounds);
    const auto monitor_height = overlay::height(monitor_bounds);

    // Create or reuse the hole region at the origin, then offset to target position
    if (monitor_overlay.cached_hole == nullptr || monitor_overlay.cached_hole_shape != spotlight_shape_ ||
        monitor_overlay.cached_hole_radius != radius_px)
    {
        if (monitor_overlay.cached_hole != nullptr)
        {
            DeleteObject(monitor_overlay.cached_hole);
        }
        const overlay::Rect origin_hole{0, 0, radius_px * 2, radius_px * 2};
        monitor_overlay.cached_hole = create_spotlight_region(origin_hole, spotlight_shape_);
        monitor_overlay.cached_hole_shape = spotlight_shape_;
        monitor_overlay.cached_hole_radius = radius_px;
        monitor_overlay.cached_hole_at = {0, 0};
    }
    if (monitor_overlay.cached_hole == nullptr)
    {
        return false;
    }

    // Offset cached hole to the target position
    const overlay::Point target{hole.left, hole.top};
    if (!(target == monitor_overlay.cached_hole_at))
    {
        const auto dx = target.x - monitor_overlay.cached_hole_at.x;
        const auto dy = target.y - monitor_overlay.cached_hole_at.y;
        OffsetRgn(monitor_overlay.cached_hole, dx, dy);
        monitor_overlay.cached_hole_at = target;
    }

    // SetWindowRgn takes ownership — must create a fresh full_region each call
    HRGN full_region = CreateRectRgn(0, 0, monitor_width, monitor_height);
    if (full_region == nullptr)
    {
        return false;
    }

    const int combine_result = CombineRgn(full_region, full_region, monitor_overlay.cached_hole, RGN_DIFF);
    if (combine_result == ERROR)
    {
        DeleteObject(full_region);
        return false;
    }

    if (SetWindowRgn(monitor_overlay.window, full_region, TRUE) == 0)
    {
        DeleteObject(full_region);
        return false;
    }
    return true;
}

bool OverlayManager::ensure_ring_bitmap(const std::int32_t base_radius_px, const double scale)
{
    if (!effects_.focus_ring_enabled && !effects_.ripple_enabled && !effects_.crosshair_enabled)
    {
        return true;
    }

    const auto ripple_radius = effects_.ripple_enabled ? animated_ring_radius(base_radius_px, scale) : 0;
    const auto margin = (std::max)(1, ring_margin_dip * base_radius_px / spotlight_radius_dip_);
    const auto stroke = (std::max)(2, ring_stroke_dip * base_radius_px / spotlight_radius_dip_);
    const auto ripple_stroke = (std::max)(2, stroke * 2 / 3);
    const auto crosshair_arm = (std::max)(8, base_radius_px / 4);
    const auto crosshair_gap = (std::max)(4, base_radius_px / 14);
    const auto crosshair_thickness = (std::max)(2, stroke / 2);
    const auto crosshair_outline = (std::max)(1, crosshair_thickness / 2);
    const auto focus_alpha =
        effects_.focus_ring_enabled ? static_cast<std::uint8_t>(std::lround(focus_opacity_ * 235.0)) : std::uint8_t{};
    const auto ripple_alpha =
        effects_.ripple_enabled ? static_cast<std::uint8_t>(std::lround(ripple_opacity_ * 180.0)) : std::uint8_t{};
    const auto crosshair_alpha =
        effects_.crosshair_enabled ? static_cast<std::uint8_t>(std::lround(focus_opacity_ * 220.0)) : std::uint8_t{};

    const auto maximum_visual_radius =
        effects_.ripple_enabled ? animated_ring_radius(base_radius_px, 1.75) : base_radius_px;
    const auto capacity_extent = maximum_visual_radius + margin;
    const SIZE required_capacity{.cx = capacity_extent * 2, .cy = capacity_extent * 2};
    const auto active_extent = ripple_alpha > 0 ? capacity_extent : base_radius_px + margin;
    const SIZE requested_size{.cx = active_extent * 2, .cy = active_extent * 2};
    if (ring_surface_.bitmap == nullptr || ring_base_radius_px_ != base_radius_px ||
        ring_bitmap_capacity_.cx < required_capacity.cx || ring_bitmap_capacity_.cy < required_capacity.cy)
    {
        destroy_ring_bitmap();

        if (!ring_surface_.create(required_capacity))
        {
            return false;
        }
        ring_bitmap_capacity_ = required_capacity;
        ring_base_radius_px_ = base_radius_px;
        std::fill_n(ring_surface_.pixels,
                    static_cast<std::size_t>(required_capacity.cx) * static_cast<std::size_t>(required_capacity.cy),
                    0U);
    }

    if (painted_ripple_radius_px_ == ripple_radius && ring_stroke_px_ == stroke &&
        painted_focus_alpha_ == focus_alpha && painted_ripple_alpha_ == ripple_alpha &&
        painted_crosshair_alpha_ == crosshair_alpha && ring_size_.cx == requested_size.cx &&
        ring_size_.cy == requested_size.cy)
    {
        return true;
    }

    const zmouse::render::PixelSurface surface{
        .pixels = std::span(ring_surface_.pixels, static_cast<std::size_t>(ring_bitmap_capacity_.cx) *
                                              static_cast<std::size_t>(ring_bitmap_capacity_.cy)),
        .width = ring_bitmap_capacity_.cx,
        .height = ring_bitmap_capacity_.cy,
        .stride = ring_bitmap_capacity_.cx,
    };
    if (painted_crosshair_alpha_ > 0 &&
        !zmouse::render::paint_contrast_crosshair(surface, ring_size_.cx, ring_size_.cy, crosshair_arm, crosshair_gap,
                                                  crosshair_thickness, crosshair_outline, 0))
    {
        destroy_ring_bitmap();
        return false;
    }
    if (painted_focus_alpha_ > 0 &&
        !zmouse::render::paint_antialiased_outline(surface, ring_size_.cx, ring_size_.cy, spotlight_shape_,
                                                   base_radius_px, ring_stroke_px_, 0))
    {
        destroy_ring_bitmap();
        return false;
    }
    if (painted_ripple_alpha_ > 0 && painted_ripple_radius_px_ > 0 &&
        !zmouse::render::paint_antialiased_outline(surface, ring_size_.cx, ring_size_.cy, spotlight_shape_,
                                                   painted_ripple_radius_px_, (std::max)(2, ring_stroke_px_ * 2 / 3),
                                                   0))
    {
        destroy_ring_bitmap();
        return false;
    }

    if (ripple_alpha > 0 &&
        !zmouse::render::paint_antialiased_outline(surface, requested_size.cx, requested_size.cy, spotlight_shape_,
                                                   ripple_radius, ripple_stroke, ripple_alpha))
    {
        destroy_ring_bitmap();
        return false;
    }
    if (focus_alpha > 0 &&
        !zmouse::render::paint_antialiased_outline(surface, requested_size.cx, requested_size.cy, spotlight_shape_,
                                                   base_radius_px, stroke, focus_alpha))
    {
        destroy_ring_bitmap();
        return false;
    }
    if (crosshair_alpha > 0 && !zmouse::render::paint_contrast_crosshair(
                                   surface, requested_size.cx, requested_size.cy, crosshair_arm, crosshair_gap,
                                   crosshair_thickness, crosshair_outline, crosshair_alpha))
    {
        destroy_ring_bitmap();
        return false;
    }

    ring_size_ = requested_size;
    painted_ripple_radius_px_ = ripple_radius;
    ring_stroke_px_ = stroke;
    painted_focus_alpha_ = focus_alpha;
    painted_ripple_alpha_ = ripple_alpha;
    painted_crosshair_alpha_ = crosshair_alpha;
    ring_bitmap_dirty_ = true;
    return true;
}

bool OverlayManager::update_ring_position(const overlay::Point cursor) noexcept
{
    if (ring_window_ == nullptr)
    {
        return false;
    }
    if (!effects_.focus_ring_enabled && !effects_.ripple_enabled && !effects_.crosshair_enabled)
    {
        ShowWindow(ring_window_, SW_HIDE);
        return true;
    }
    if (ring_surface_.dc == nullptr)
    {
        return false;
    }

    POINT destination{cursor.x - ring_size_.cx / 2, cursor.y - ring_size_.cy / 2};
    if (ring_bitmap_dirty_)
    {
        POINT source{};
        SIZE size = ring_size_;
        BLENDFUNCTION blend{
            .BlendOp = AC_SRC_OVER,
            .BlendFlags = 0,
            .SourceConstantAlpha = 255,
            .AlphaFormat = AC_SRC_ALPHA,
        };
        if (UpdateLayeredWindow(ring_window_, nullptr, &destination, &size, ring_surface_.dc, &source, 0, &blend,
                                ULW_ALPHA) == FALSE)
        {
            return false;
        }
        if (SetWindowPos(ring_window_, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) == FALSE)
        {
            return false;
        }
        ring_bitmap_dirty_ = false;
        ring_window_pos_ = destination;
        return true;
    }

    // Skip SetWindowPos if position hasn't changed
    if (destination.x == ring_window_pos_.x && destination.y == ring_window_pos_.y)
    {
        return true;
    }
    if (SetWindowPos(ring_window_, HWND_TOPMOST, destination.x, destination.y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) == FALSE)
    {
        return false;
    }
    ring_window_pos_ = destination;
    return true;
}

bool OverlayManager::ensure_cursor_bitmap(const UINT dpi)
{
    CURSORINFO cursor_info{.cbSize = sizeof(CURSORINFO)};
    if (GetCursorInfo(&cursor_info) == FALSE || cursor_info.hCursor == nullptr ||
        ((cursor_info.flags & CURSOR_SHOWING) == 0 && !system_cursor_hidden_))
    {
        cursor_drawable_ = false;
        return true;
    }

    const auto scale_percent = (std::clamp)(effects_.cursor_scale_percent, 125U, 400U);
    if (cursor_surface_.bitmap != nullptr && rendered_cursor_ == cursor_info.hCursor && rendered_cursor_dpi_ == dpi &&
        rendered_cursor_scale_percent_ == scale_percent)
    {
        cursor_drawable_ = true;
        return true;
    }

    ICONINFO icon_info{};
    if (GetIconInfo(cursor_info.hCursor, &icon_info) == FALSE)
    {
        return false;
    }

    LONG base_width = 0;
    LONG base_height = 0;
    BITMAP bitmap_details{};
    if (icon_info.hbmColor != nullptr &&
        GetObjectW(icon_info.hbmColor, sizeof(bitmap_details), &bitmap_details) == sizeof(bitmap_details))
    {
        base_width = bitmap_details.bmWidth;
        base_height = bitmap_details.bmHeight;
    }
    else if (icon_info.hbmMask != nullptr &&
             GetObjectW(icon_info.hbmMask, sizeof(bitmap_details), &bitmap_details) == sizeof(bitmap_details))
    {
        base_width = bitmap_details.bmWidth;
        base_height = bitmap_details.bmHeight / 2;
    }
    if (icon_info.hbmColor != nullptr)
    {
        static_cast<void>(DeleteObject(icon_info.hbmColor));
    }
    if (icon_info.hbmMask != nullptr)
    {
        static_cast<void>(DeleteObject(icon_info.hbmMask));
    }

    if (base_width <= 0 || base_height <= 0)
    {
        base_width = GetSystemMetricsForDpi(SM_CXCURSOR, dpi);
        base_height = GetSystemMetricsForDpi(SM_CYCURSOR, dpi);
    }
    const SIZE target_size{
        .cx = (std::max)(1, MulDiv(base_width, static_cast<int>(scale_percent), 100)),
        .cy = (std::max)(1, MulDiv(base_height, static_cast<int>(scale_percent), 100)),
    };

    destroy_cursor_bitmap();
    if (!cursor_surface_.create(target_size))
    {
        destroy_cursor_bitmap();
        return false;
    }

    DibSurface black_surface;
    DibSurface white_surface;
    if (!black_surface.create(target_size) || !white_surface.create(target_size))
    {
        destroy_cursor_bitmap();
        return false;
    }

    const auto pixel_count = static_cast<std::size_t>(target_size.cx) * static_cast<std::size_t>(target_size.cy);
    std::fill_n(black_surface.pixels, pixel_count, 0x00000000U);
    std::fill_n(white_surface.pixels, pixel_count, 0x00FFFFFFU);
    const bool drawn =
        DrawIconEx(black_surface.dc, 0, 0, cursor_info.hCursor, target_size.cx, target_size.cy, 0, nullptr, DI_NORMAL) !=
            FALSE &&
        DrawIconEx(white_surface.dc, 0, 0, cursor_info.hCursor, target_size.cx, target_size.cy, 0, nullptr, DI_NORMAL) != FALSE;
    const bool composed = drawn && zmouse::render::compose_cursor_images(std::span(black_surface.pixels, pixel_count),
                                                                         std::span(white_surface.pixels, pixel_count),
                                                                         std::span(cursor_surface_.pixels, pixel_count));
    if (!composed)
    {
        destroy_cursor_bitmap();
        return false;
    }

    cursor_size_ = target_size;
    cursor_hotspot_ = {
        MulDiv(static_cast<int>(icon_info.xHotspot), static_cast<int>(scale_percent), 100),
        MulDiv(static_cast<int>(icon_info.yHotspot), static_cast<int>(scale_percent), 100),
    };
    rendered_cursor_ = cursor_info.hCursor;
    rendered_cursor_dpi_ = dpi;
    rendered_cursor_scale_percent_ = scale_percent;
    cursor_drawable_ = true;
    return true;
}

bool OverlayManager::hide_system_cursor()
{
    if (system_cursor_hidden_)
    {
        return true;
    }
    if (!magnification_initialized_)
    {
        if (MagInitialize() == FALSE)
        {
            return false;
        }
        magnification_initialized_ = true;
    }
    if (MagShowSystemCursor(FALSE) == FALSE)
    {
        return false;
    }
    system_cursor_hidden_ = true;
    return true;
}

bool OverlayManager::update_cursor_position(const overlay::Point cursor)
{
    if (cursor_window_ == nullptr)
    {
        return false;
    }
    if (!effects_.enlarged_cursor_enabled)
    {
        ShowWindow(cursor_window_, SW_HIDE);
        restore_system_cursor();
        return true;
    }
    if (!ensure_cursor_bitmap(dpi_at(cursor)))
    {
        return false;
    }
    if (!cursor_drawable_)
    {
        ShowWindow(cursor_window_, SW_HIDE);
        restore_system_cursor();
        return true;
    }

    if (!hide_system_cursor())
    {
        ShowWindow(cursor_window_, SW_HIDE);
        return true;
    }

    POINT destination{cursor.x - cursor_hotspot_.x, cursor.y - cursor_hotspot_.y};
    POINT source{};
    SIZE size = cursor_size_;
    BLENDFUNCTION blend{
        .BlendOp = AC_SRC_OVER,
        .BlendFlags = 0,
        .SourceConstantAlpha = static_cast<BYTE>(std::lround(focus_opacity_ * 255.0)),
        .AlphaFormat = AC_SRC_ALPHA,
    };
    if (UpdateLayeredWindow(cursor_window_, nullptr, &destination, &size, cursor_surface_.dc, &source, 0, &blend, ULW_ALPHA) ==
        FALSE)
    {
        ShowWindow(cursor_window_, SW_HIDE);
        restore_system_cursor();
        return false;
    }
    if (SetWindowPos(cursor_window_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) == FALSE)
    {
        ShowWindow(cursor_window_, SW_HIDE);
        restore_system_cursor();
        return false;
    }
    return true;
}

UINT OverlayManager::dpi_at(const overlay::Point cursor) const noexcept
{
    const POINT point{cursor.x, cursor.y};
    const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    for (const auto& overlay : overlays_)
    {
        if (overlay.monitor == monitor)
        {
            return overlay.dpi;
        }
    }
    return 96;
}

void OverlayManager::apply_dim_progress() const noexcept
{
    const auto alpha = static_cast<BYTE>(std::lround(static_cast<double>(dim_alpha_) * dim_progress_));
    for (const auto& overlay : overlays_)
    {
        static_cast<void>(SetLayeredWindowAttributes(overlay.window, 0, alpha, LWA_ALPHA));
    }
}

void OverlayManager::destroy_windows() noexcept
{
    restore_system_cursor();
    if (cursor_window_ != nullptr)
    {
        DestroyWindow(cursor_window_);
        cursor_window_ = nullptr;
    }
    if (ring_window_ != nullptr)
    {
        DestroyWindow(ring_window_);
        ring_window_ = nullptr;
    }
    for (const auto& overlay : overlays_)
    {
        if (overlay.cached_hole != nullptr)
        {
            DeleteObject(overlay.cached_hole);
        }
        DestroyWindow(overlay.window);
    }
    overlays_.clear();
    visible_ = false;
}

void OverlayManager::destroy_ring_bitmap() noexcept
{
    ring_surface_.destroy();
    ring_bitmap_capacity_ = {};
    ring_size_ = {};
    ring_base_radius_px_ = 0;
    painted_ripple_radius_px_ = 0;
    ring_stroke_px_ = 0;
    painted_focus_alpha_ = 0;
    painted_ripple_alpha_ = 0;
    painted_crosshair_alpha_ = 0;
    ring_bitmap_dirty_ = false;
}

void OverlayManager::destroy_cursor_bitmap() noexcept
{
    cursor_surface_.destroy();
    cursor_size_ = {};
    cursor_hotspot_ = {};
    rendered_cursor_ = nullptr;
    rendered_cursor_dpi_ = 0;
    rendered_cursor_scale_percent_ = 0;
    cursor_drawable_ = false;
}

void OverlayManager::restore_system_cursor() noexcept
{
    if (system_cursor_hidden_ && MagShowSystemCursor(TRUE) != FALSE)
    {
        system_cursor_hidden_ = false;
    }
}

void OverlayManager::uninitialize_magnification() noexcept
{
    restore_system_cursor();
    if (magnification_initialized_)
    {
        static_cast<void>(MagUninitialize());
        magnification_initialized_ = false;
    }
}
} // namespace zmouse::platform

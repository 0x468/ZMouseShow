#include "overlay_manager.hpp"

#include <windows.h>

#include "zmouse/platform/spotlight_region.hpp"
#include "zmouse/render/ring_rasterizer.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

OverlayManager::~OverlayManager()
{
    destroy_windows();
    destroy_ring_bitmap();
    if (ring_class_ != 0)
    {
        UnregisterClassW(ring_class_name, instance_);
    }
    if (overlay_class_ != 0)
    {
        UnregisterClassW(overlay_class_name, instance_);
    }
}

void OverlayManager::configure(const std::int32_t spotlight_radius_dip, const overlay::SpotlightShape spotlight_shape,
                               const std::uint32_t dim_opacity_percent) noexcept
{
    spotlight_radius_dip_ = (std::clamp)(spotlight_radius_dip, 32, 512);
    spotlight_shape_ = spotlight_shape;
    const auto opacity = (std::clamp)(dim_opacity_percent, 10U, 90U);
    dim_alpha_ = static_cast<BYTE>((opacity * 255U + 50U) / 100U);
    destroy_ring_bitmap();

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
        overlays_.empty() || !create_ring_window())
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
    if (!ensure_ring_bitmap(radius_px, ring_scale_))
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

    if (!update_ring_position(cursor))
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
    if (!ensure_ring_bitmap(radius_px, ring_scale_))
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
    if (!update_ring_position(cursor))
    {
        hide();
    }
}

void OverlayManager::set_animation_frame(const double dim_progress, const double ring_scale, const double ring_opacity)
{
    dim_progress_ = (std::clamp)(dim_progress, 0.0, 1.0);
    ring_scale_ = (std::clamp)(ring_scale, 1.0, 4.0);
    ring_opacity_ = (std::clamp)(ring_opacity, 0.0, 1.0);
    apply_dim_progress();

    if (!visible_)
    {
        return;
    }

    const auto radius_px = overlay::dip_to_pixels(spotlight_radius_dip_, dpi_at(last_cursor_));
    if (!ensure_ring_bitmap(radius_px, ring_scale_) || !update_ring_position(last_cursor_))
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
    for (const auto& overlay : overlays_)
    {
        ShowWindow(overlay.window, SW_HIDE);
    }
    visible_ = false;
}

bool OverlayManager::visible() const noexcept
{
    return visible_;
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

    HRGN full_region = CreateRectRgn(0, 0, monitor_width, monitor_height);
    HRGN hole_region = create_spotlight_region(hole, spotlight_shape_);
    if (full_region == nullptr || hole_region == nullptr)
    {
        if (full_region != nullptr)
        {
            DeleteObject(full_region);
        }
        if (hole_region != nullptr)
        {
            DeleteObject(hole_region);
        }
        return false;
    }

    const int combine_result = CombineRgn(full_region, full_region, hole_region, RGN_DIFF);
    DeleteObject(hole_region);
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
    const auto visual_radius = animated_ring_radius(base_radius_px, scale);
    const auto margin = (std::max)(1, ring_margin_dip * base_radius_px / spotlight_radius_dip_);
    const auto stroke = (std::max)(2, ring_stroke_dip * base_radius_px / spotlight_radius_dip_);
    const auto extent = visual_radius + margin;
    const SIZE requested_size{.cx = extent * 2, .cy = extent * 2};

    const bool settled_at_base_radius = scale <= 1.0;
    const auto maximum_visual_radius =
        settled_at_base_radius ? visual_radius : animated_ring_radius(base_radius_px, 4.0);
    const auto capacity_extent = maximum_visual_radius + margin;
    const SIZE required_capacity{.cx = capacity_extent * 2, .cy = capacity_extent * 2};
    if (ring_bitmap_ == nullptr || ring_base_radius_px_ != base_radius_px ||
        ring_bitmap_capacity_.cx < required_capacity.cx || ring_bitmap_capacity_.cy < required_capacity.cy ||
        (settled_at_base_radius &&
         (ring_bitmap_capacity_.cx != required_capacity.cx || ring_bitmap_capacity_.cy != required_capacity.cy)))
    {
        destroy_ring_bitmap();

        BITMAPINFO bitmap_info{};
        bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap_info.bmiHeader.biWidth = required_capacity.cx;
        bitmap_info.bmiHeader.biHeight = -required_capacity.cy;
        bitmap_info.bmiHeader.biPlanes = 1;
        bitmap_info.bmiHeader.biBitCount = 32;
        bitmap_info.bmiHeader.biCompression = BI_RGB;

        void* pixel_memory = nullptr;
        ring_bitmap_ = CreateDIBSection(nullptr, &bitmap_info, DIB_RGB_COLORS, &pixel_memory, nullptr, 0);
        ring_dc_ = CreateCompatibleDC(nullptr);
        if (ring_bitmap_ == nullptr || ring_dc_ == nullptr || pixel_memory == nullptr)
        {
            destroy_ring_bitmap();
            return false;
        }
        const HGDIOBJ old_bitmap = SelectObject(ring_dc_, ring_bitmap_);
        if (old_bitmap == nullptr || old_bitmap == HGDI_ERROR)
        {
            destroy_ring_bitmap();
            return false;
        }
        ring_old_bitmap_ = old_bitmap;

        ring_pixels_ = static_cast<std::uint32_t*>(pixel_memory);
        ring_bitmap_capacity_ = required_capacity;
        ring_base_radius_px_ = base_radius_px;
        std::fill_n(ring_pixels_,
                    static_cast<std::size_t>(required_capacity.cx) * static_cast<std::size_t>(required_capacity.cy),
                    0U);
    }

    if (ring_visual_radius_px_ == visual_radius && ring_stroke_px_ == stroke && ring_size_.cx == requested_size.cx &&
        ring_size_.cy == requested_size.cy)
    {
        return true;
    }

    const zmouse::render::PixelSurface surface{
        .pixels = std::span(ring_pixels_, static_cast<std::size_t>(ring_bitmap_capacity_.cx) *
                                              static_cast<std::size_t>(ring_bitmap_capacity_.cy)),
        .width = ring_bitmap_capacity_.cx,
        .height = ring_bitmap_capacity_.cy,
        .stride = ring_bitmap_capacity_.cx,
    };
    if (ring_visual_radius_px_ > 0 &&
        !zmouse::render::paint_antialiased_ring(surface, ring_size_.cx, ring_size_.cy, ring_visual_radius_px_,
                                                ring_stroke_px_, 0))
    {
        destroy_ring_bitmap();
        return false;
    }
    if (!zmouse::render::paint_antialiased_ring(surface, requested_size.cx, requested_size.cy, visual_radius, stroke,
                                                235))
    {
        destroy_ring_bitmap();
        return false;
    }

    ring_size_ = requested_size;
    ring_visual_radius_px_ = visual_radius;
    ring_stroke_px_ = stroke;
    return true;
}

bool OverlayManager::update_ring_position(const overlay::Point cursor) const noexcept
{
    if (ring_window_ == nullptr || ring_dc_ == nullptr)
    {
        return false;
    }

    POINT destination{cursor.x - ring_size_.cx / 2, cursor.y - ring_size_.cy / 2};
    POINT source{};
    SIZE size = ring_size_;
    BLENDFUNCTION blend{
        .BlendOp = AC_SRC_OVER,
        .BlendFlags = 0,
        .SourceConstantAlpha = static_cast<BYTE>(std::lround(ring_opacity_ * 255.0)),
        .AlphaFormat = AC_SRC_ALPHA,
    };
    if (UpdateLayeredWindow(ring_window_, nullptr, &destination, &size, ring_dc_, &source, 0, &blend, ULW_ALPHA) ==
        FALSE)
    {
        return false;
    }

    return SetWindowPos(ring_window_, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) != FALSE;
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
    if (ring_window_ != nullptr)
    {
        DestroyWindow(ring_window_);
        ring_window_ = nullptr;
    }
    for (const auto& overlay : overlays_)
    {
        DestroyWindow(overlay.window);
    }
    overlays_.clear();
    visible_ = false;
}

void OverlayManager::destroy_ring_bitmap() noexcept
{
    if (ring_dc_ != nullptr && ring_old_bitmap_ != nullptr)
    {
        SelectObject(ring_dc_, ring_old_bitmap_);
    }
    if (ring_bitmap_ != nullptr)
    {
        DeleteObject(ring_bitmap_);
    }
    if (ring_dc_ != nullptr)
    {
        DeleteDC(ring_dc_);
    }

    ring_dc_ = nullptr;
    ring_bitmap_ = nullptr;
    ring_old_bitmap_ = nullptr;
    ring_pixels_ = nullptr;
    ring_bitmap_capacity_ = {};
    ring_size_ = {};
    ring_base_radius_px_ = 0;
    ring_visual_radius_px_ = 0;
    ring_stroke_px_ = 0;
}
} // namespace zmouse::platform

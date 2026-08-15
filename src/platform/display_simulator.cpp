#include "display_simulator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <windowsx.h>

namespace zmouse::platform
{
namespace
{
constexpr wchar_t simulator_class_name[] = L"ZMouseShow.DisplaySimulator";
constexpr wchar_t simulator_title[] = L"ZMouseShow - 三屏拓扑模拟器";
constexpr COLORREF background_color = RGB(15, 23, 42);
constexpr COLORREF panel_color = RGB(30, 41, 59);
constexpr COLORREF primary_panel_color = RGB(51, 65, 85);
constexpr COLORREF dim_color = RGB(17, 24, 39);
constexpr COLORREF text_color = RGB(248, 250, 252);
constexpr COLORREF muted_text_color = RGB(148, 163, 184);
constexpr COLORREF accent_color = RGB(34, 197, 94);

struct SimulatedMonitor
{
    RECT bounds;
    UINT dpi;
    const wchar_t* name;
    bool primary;
};

constexpr std::array simulated_monitors{
    SimulatedMonitor{{-2'560, 120, 0, 1'560}, 120, L"DISPLAY1 · 2560×1440 · 125%", false},
    SimulatedMonitor{{0, 0, 1'920, 1'080}, 96, L"DISPLAY2 · 1920×1080 · 100% · 主屏", true},
    SimulatedMonitor{{1'920, -180, 4'480, 1'260}, 144, L"DISPLAY3 · 2560×1440 · 150%", false},
};
constexpr RECT virtual_desktop{-2'560, -180, 4'480, 1'560};

struct PreviewLayout
{
    double scale{};
    double origin_x{};
    double origin_y{};
};

[[nodiscard]] PreviewLayout calculate_layout(const RECT& client) noexcept
{
    constexpr double horizontal_margin = 36.0;
    constexpr double header_height = 92.0;
    constexpr double footer_height = 52.0;
    const double available_width = std::max(1.0, static_cast<double>(client.right) - horizontal_margin * 2.0);
    const double available_height = std::max(1.0, static_cast<double>(client.bottom) - header_height - footer_height);
    const double desktop_width = static_cast<double>(virtual_desktop.right - virtual_desktop.left);
    const double desktop_height = static_cast<double>(virtual_desktop.bottom - virtual_desktop.top);
    const double scale = std::min(available_width / desktop_width, available_height / desktop_height);
    const double rendered_width = desktop_width * scale;
    const double rendered_height = desktop_height * scale;
    return {
        .scale = scale,
        .origin_x = (static_cast<double>(client.right) - rendered_width) / 2.0 - virtual_desktop.left * scale,
        .origin_y = header_height + (available_height - rendered_height) / 2.0 - virtual_desktop.top * scale,
    };
}

[[nodiscard]] LONG map_x(const PreviewLayout& layout, const LONG value) noexcept
{
    return static_cast<LONG>(std::lround(layout.origin_x + value * layout.scale));
}

[[nodiscard]] LONG map_y(const PreviewLayout& layout, const LONG value) noexcept
{
    return static_cast<LONG>(std::lround(layout.origin_y + value * layout.scale));
}

[[nodiscard]] RECT map_rect(const PreviewLayout& layout, const RECT& rect) noexcept
{
    return {map_x(layout, rect.left), map_y(layout, rect.top), map_x(layout, rect.right), map_y(layout, rect.bottom)};
}

[[nodiscard]] bool contains(const RECT& rect, const POINT point) noexcept
{
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

[[nodiscard]] UINT dpi_at(const POINT cursor) noexcept
{
    for (const auto& monitor : simulated_monitors)
    {
        if (contains(monitor.bounds, cursor))
        {
            return monitor.dpi;
        }
    }
    return 96;
}

void draw_text_line(HDC dc, HFONT font, const wchar_t* text, RECT bounds, const COLORREF color,
                    const UINT format = DT_LEFT | DT_SINGLELINE | DT_VCENTER) noexcept
{
    const auto old_font = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    static_cast<void>(DrawTextW(dc, text, -1, &bounds, format));
    static_cast<void>(SelectObject(dc, old_font));
}

class SimulatorWindow final
{
  public:
    explicit SimulatorWindow(HINSTANCE instance) noexcept : instance_(instance) {}

    [[nodiscard]] int run()
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        window_class.lpszClassName = simulator_class_name;
        if (RegisterClassExW(&window_class) == 0)
        {
            return 1;
        }

        window_ = CreateWindowExW(0, simulator_class_name, simulator_title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 1'180, 620, nullptr, nullptr, instance_, this);
        if (window_ == nullptr)
        {
            return 1;
        }
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);

        MSG message{};
        while (true)
        {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result == 0)
            {
                return static_cast<int>(message.wParam);
            }
            if (result == -1)
            {
                return 1;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

  private:
    static LRESULT CALLBACK window_proc(HWND window, const UINT message, const WPARAM w_param,
                                        const LPARAM l_param) noexcept
    {
        SimulatorWindow* simulator = nullptr;
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
            simulator = static_cast<SimulatorWindow*>(create->lpCreateParams);
            simulator->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(simulator));
        }
        else
        {
            simulator = reinterpret_cast<SimulatorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        return simulator != nullptr ? simulator->handle_message(message, w_param, l_param)
                                    : DefWindowProcW(window, message, w_param, l_param);
    }

    LRESULT handle_message(const UINT message, const WPARAM w_param, const LPARAM l_param) noexcept
    {
        switch (message)
        {
        case WM_MOUSEMOVE:
            update_cursor({GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
            return 0;
        case WM_KEYDOWN:
            if (w_param == VK_ESCAPE)
            {
                DestroyWindow(window_);
                return 0;
            }
            break;
        case WM_GETMINMAXINFO:
        {
            auto* limits = reinterpret_cast<MINMAXINFO*>(l_param);
            limits->ptMinTrackSize = {760, 440};
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window_, message, w_param, l_param);
    }

    void update_cursor(const POINT client_cursor) noexcept
    {
        RECT client{};
        GetClientRect(window_, &client);
        const auto layout = calculate_layout(client);
        if (layout.scale <= 0.0)
        {
            return;
        }
        simulated_cursor_.x = static_cast<LONG>(std::lround((client_cursor.x - layout.origin_x) / layout.scale));
        simulated_cursor_.y = static_cast<LONG>(std::lround((client_cursor.y - layout.origin_y) / layout.scale));
        simulated_cursor_.x = std::clamp(simulated_cursor_.x, virtual_desktop.left, virtual_desktop.right - 1);
        simulated_cursor_.y = std::clamp(simulated_cursor_.y, virtual_desktop.top, virtual_desktop.bottom - 1);
        static_cast<void>(InvalidateRect(window_, nullptr, FALSE));
    }

    void paint() noexcept
    {
        PAINTSTRUCT paint_state{};
        HDC window_dc = BeginPaint(window_, &paint_state);
        if (window_dc == nullptr)
        {
            return;
        }

        RECT client{};
        GetClientRect(window_, &client);
        HDC buffer_dc = CreateCompatibleDC(window_dc);
        HBITMAP buffer_bitmap = CreateCompatibleBitmap(window_dc, client.right, client.bottom);
        if (buffer_dc == nullptr || buffer_bitmap == nullptr)
        {
            if (buffer_bitmap != nullptr)
            {
                DeleteObject(buffer_bitmap);
            }
            if (buffer_dc != nullptr)
            {
                DeleteDC(buffer_dc);
            }
            EndPaint(window_, &paint_state);
            return;
        }
        const auto old_bitmap = SelectObject(buffer_dc, buffer_bitmap);
        draw(buffer_dc, client);
        static_cast<void>(BitBlt(window_dc, 0, 0, client.right, client.bottom, buffer_dc, 0, 0, SRCCOPY));
        static_cast<void>(SelectObject(buffer_dc, old_bitmap));
        DeleteObject(buffer_bitmap);
        DeleteDC(buffer_dc);
        EndPaint(window_, &paint_state);
    }

    void draw(HDC dc, const RECT& client) const noexcept
    {
        const HBRUSH background = CreateSolidBrush(background_color);
        FillRect(dc, &client, background);
        DeleteObject(background);

        const HFONT title_font =
            CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        const HFONT body_font =
            CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        const HFONT label_font =
            CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        draw_text_line(dc, title_font, L"三屏拓扑模拟器", {36, 18, client.right - 36, 50}, text_color);
        draw_text_line(dc, body_font, L"移动鼠标检查负坐标、混合 DPI 与跨屏圆孔；此模式不替代真实 DWM 多屏验收。",
                       {36, 50, client.right - 36, 78}, muted_text_color);

        const auto layout = calculate_layout(client);
        const LONG cursor_x = map_x(layout, simulated_cursor_.x);
        const LONG cursor_y = map_y(layout, simulated_cursor_.y);
        const double radius_pixels = 120.0 * dpi_at(simulated_cursor_) / 96.0;
        const LONG radius = std::max(8L, static_cast<LONG>(std::lround(radius_pixels * layout.scale)));

        const HBRUSH normal_panel = CreateSolidBrush(panel_color);
        const HBRUSH primary_panel = CreateSolidBrush(primary_panel_color);
        const HBRUSH dim_brush = CreateSolidBrush(dim_color);
        const HPEN border_pen = CreatePen(PS_SOLID, 2, RGB(71, 85, 105));
        const HPEN grid_pen = CreatePen(PS_SOLID, 1, RGB(71, 85, 105));
        const HPEN ring_pen = CreatePen(PS_SOLID, 3, accent_color);

        for (const auto& monitor : simulated_monitors)
        {
            const RECT preview = map_rect(layout, monitor.bounds);
            FillRect(dc, &preview, monitor.primary ? primary_panel : normal_panel);

            const auto old_pen = SelectObject(dc, grid_pen);
            for (int division = 1; division < 4; ++division)
            {
                const LONG x = preview.left + (preview.right - preview.left) * division / 4;
                MoveToEx(dc, x, preview.top, nullptr);
                LineTo(dc, x, preview.bottom);
                const LONG y = preview.top + (preview.bottom - preview.top) * division / 4;
                MoveToEx(dc, preview.left, y, nullptr);
                LineTo(dc, preview.right, y);
            }
            static_cast<void>(SelectObject(dc, old_pen));

            HRGN monitor_region = CreateRectRgnIndirect(&preview);
            HRGN hole_region =
                CreateEllipticRgn(cursor_x - radius, cursor_y - radius, cursor_x + radius + 1, cursor_y + radius + 1);
            if (monitor_region != nullptr && hole_region != nullptr)
            {
                static_cast<void>(CombineRgn(monitor_region, monitor_region, hole_region, RGN_DIFF));
                FillRgn(dc, monitor_region, dim_brush);
            }
            if (hole_region != nullptr)
            {
                DeleteObject(hole_region);
            }
            if (monitor_region != nullptr)
            {
                DeleteObject(monitor_region);
            }

            const auto previous_pen = SelectObject(dc, border_pen);
            const auto previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, preview.left, preview.top, preview.right, preview.bottom);
            static_cast<void>(SelectObject(dc, previous_brush));
            static_cast<void>(SelectObject(dc, previous_pen));

            RECT label_bounds{preview.left + 10, preview.top + 8, preview.right - 8, preview.top + 34};
            draw_text_line(dc, label_font, monitor.name, label_bounds, text_color);
        }

        const auto previous_pen = SelectObject(dc, ring_pen);
        const auto previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cursor_x - radius, cursor_y - radius, cursor_x + radius, cursor_y + radius);
        MoveToEx(dc, cursor_x - 7, cursor_y, nullptr);
        LineTo(dc, cursor_x + 8, cursor_y);
        MoveToEx(dc, cursor_x, cursor_y - 7, nullptr);
        LineTo(dc, cursor_x, cursor_y + 8);
        static_cast<void>(SelectObject(dc, previous_brush));
        static_cast<void>(SelectObject(dc, previous_pen));

        wchar_t status[160]{};
        static_cast<void>(swprintf_s(status, L"模拟坐标  x=%ld  y=%ld  ·  圆孔 120 DIP  ·  Esc 退出",
                                     simulated_cursor_.x, simulated_cursor_.y));
        draw_text_line(dc, body_font, status, {36, client.bottom - 46, client.right - 36, client.bottom - 14},
                       muted_text_color, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        DeleteObject(ring_pen);
        DeleteObject(grid_pen);
        DeleteObject(border_pen);
        DeleteObject(dim_brush);
        DeleteObject(primary_panel);
        DeleteObject(normal_panel);
        DeleteObject(label_font);
        DeleteObject(body_font);
        DeleteObject(title_font);
    }

    HINSTANCE instance_{};
    HWND window_{};
    POINT simulated_cursor_{960, 540};
};
} // namespace

int run_display_simulator(const HINSTANCE instance)
{
    SimulatorWindow simulator(instance);
    return simulator.run();
}
} // namespace zmouse::platform

#include "foreground_context.hpp"

#include <Windows.h>

#include "zmouse/platform/string_util.hpp"
#include <algorithm>
#include <dwmapi.h>
#include <filesystem>
#include <string>
#include <vector>

namespace zmouse::platform
{
namespace
{

std::string foreground_executable_name(const HWND window)
{
    DWORD process_id = 0;
    static_cast<void>(GetWindowThreadProcessId(window, &process_id));
    if (process_id == 0)
    {
        return {};
    }
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr)
    {
        return {};
    }

    std::vector<wchar_t> path(32'768);
    DWORD path_size = static_cast<DWORD>(path.size());
    const bool queried = QueryFullProcessImageNameW(process, 0, path.data(), &path_size) != FALSE;
    static_cast<void>(CloseHandle(process));
    if (!queried)
    {
        return {};
    }
    return wide_to_utf8(std::filesystem::path(std::wstring_view(path.data(), path_size)).filename().wstring());
}

bool is_fullscreen_window(const HWND window, const RECT monitor_rect) noexcept
{
    if (IsWindowVisible(window) == FALSE || IsIconic(window) != FALSE)
    {
        return false;
    }
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0)
    {
        return false;
    }

    RECT window_rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &window_rect, sizeof(window_rect))) &&
        GetWindowRect(window, &window_rect) == FALSE)
    {
        return false;
    }
    constexpr LONG tolerance = 2;
    return window_rect.left <= monitor_rect.left + tolerance && window_rect.top <= monitor_rect.top + tolerance &&
           window_rect.right >= monitor_rect.right - tolerance && window_rect.bottom >= monitor_rect.bottom - tolerance;
}

bool is_pointer_confined(const RECT monitor_rect) noexcept
{
    RECT clip{};
    if (GetClipCursor(&clip) == FALSE)
    {
        return false;
    }
    const RECT virtual_desktop{
        .left = GetSystemMetrics(SM_XVIRTUALSCREEN),
        .top = GetSystemMetrics(SM_YVIRTUALSCREEN),
        .right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        .bottom = GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN),
    };
    if (EqualRect(&clip, &virtual_desktop) != FALSE)
    {
        return false;
    }

    RECT intersection{};
    if (IntersectRect(&intersection, &clip, &monitor_rect) == FALSE)
    {
        return false;
    }
    const auto clip_width = (std::max)(0L, clip.right - clip.left);
    const auto clip_height = (std::max)(0L, clip.bottom - clip.top);
    const auto desktop_width = (std::max)(0L, virtual_desktop.right - virtual_desktop.left);
    const auto desktop_height = (std::max)(0L, virtual_desktop.bottom - virtual_desktop.top);
    return clip_width < desktop_width || clip_height < desktop_height;
}
} // namespace

policy::ForegroundContext query_foreground_context() noexcept
{
    try
    {
        const HWND foreground = GetForegroundWindow();
        if (foreground == nullptr)
        {
            return {};
        }
        const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{.cbSize = sizeof(MONITORINFO)};
        if (monitor == nullptr || GetMonitorInfoW(monitor, &info) == FALSE)
        {
            return {.executable_name = foreground_executable_name(foreground)};
        }
        return {
            .executable_name = foreground_executable_name(foreground),
            .fullscreen = is_fullscreen_window(foreground, info.rcMonitor),
            .pointer_confined = is_pointer_confined(info.rcMonitor),
        };
    }
    catch (...)
    {
        return {};
    }
}
} // namespace zmouse::platform

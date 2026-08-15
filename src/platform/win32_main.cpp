#include <windows.h>

#include <hidusage.h>
#include <shellapi.h>

#include "overlay_manager.hpp"
#include "zmouse/config/settings.hpp"
#include "zmouse/input/double_ctrl_detector.hpp"
#include "zmouse/input/shake_detector.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace
{
constexpr wchar_t window_class_name[] = L"ZMouseShow.MessageWindow";
constexpr wchar_t window_title[] = L"ZMouseShow";
constexpr wchar_t instance_mutex_name[] = L"Local\\ZMouseShow.SingleInstance";

constexpr UINT tray_icon_id = 1;
constexpr UINT command_toggle_pause = 1001;
constexpr UINT command_toggle_shake = 1002;
constexpr UINT command_toggle_auto_timeout = 1003;
constexpr UINT command_reload_configuration = 1004;
constexpr UINT command_export_default_configuration = 1005;
constexpr UINT command_exit = 1006;
constexpr UINT message_tray = WM_APP + 1;
constexpr UINT message_activate = WM_APP + 2;
constexpr UINT_PTR overlay_timer_id = 1;
constexpr UINT overlay_timer_interval_ms = 50;

constexpr std::uint8_t mouse_left = 1U << 0U;
constexpr std::uint8_t mouse_right = 1U << 1U;
constexpr std::uint8_t mouse_middle = 1U << 2U;
constexpr std::uint8_t mouse_x1 = 1U << 3U;
constexpr std::uint8_t mouse_x2 = 1U << 4U;

[[nodiscard]] std::filesystem::path resolve_config_path() noexcept
{
    try
    {
        int argument_count = 0;
        auto* raw_arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
        const auto local_free = [](wchar_t** arguments) noexcept
        {
            if (arguments != nullptr)
            {
                static_cast<void>(LocalFree(arguments));
            }
        };
        const std::unique_ptr<wchar_t*, decltype(local_free)> arguments(raw_arguments, local_free);
        if (arguments != nullptr)
        {
            for (int index = 1; index + 1 < argument_count; ++index)
            {
                if (_wcsicmp(arguments.get()[index], L"--config") == 0)
                {
                    return std::filesystem::path(arguments.get()[index + 1]);
                }
            }
        }

        std::wstring module_path(32'768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
        if (length == 0 || static_cast<std::size_t>(length) >= module_path.size())
        {
            return L"ZMouseShow.toml";
        }
        module_path.resize(length);
        return std::filesystem::path(module_path).parent_path() / L"ZMouseShow.toml";
    }
    catch (...)
    {
        return L"ZMouseShow.toml";
    }
}

class Application final
{
  public:
    Application(HINSTANCE instance, std::filesystem::path config_path) noexcept
        : instance_(instance), config_path_(std::move(config_path))
    {
    }

    int run()
    {
        if (const auto loaded = zmouse::config::load_toml(config_path_))
        {
            apply_settings(*loaded);
        }
        else
        {
            apply_settings({});
        }

        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
        if (!create_window() || !overlay_manager_.initialize(instance_) || !register_raw_input() || !add_tray_icon())
        {
            if (window_ != nullptr)
            {
                DestroyWindow(window_);
            }
            return 1;
        }

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
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) noexcept
    {
        Application* app = nullptr;
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
            app = static_cast<Application*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->window_ = window;
        }
        else
        {
            app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (app != nullptr)
        {
            return app->handle_message(message, w_param, l_param);
        }
        return DefWindowProcW(window, message, w_param, l_param);
    }

    bool create_window()
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = window_class_name;

        if (RegisterClassExW(&window_class) == 0)
        {
            return false;
        }

        return CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, window_class_name, window_title, WS_POPUP, 0, 0, 0,
                               0, nullptr, nullptr, instance_, this) != nullptr;
    }

    bool register_raw_input() const noexcept
    {
        std::array<RAWINPUTDEVICE, 2> devices{};
        devices[0] = {
            .usUsagePage = HID_USAGE_PAGE_GENERIC,
            .usUsage = HID_USAGE_GENERIC_KEYBOARD,
            .dwFlags = RIDEV_INPUTSINK,
            .hwndTarget = window_,
        };
        devices[1] = {
            .usUsagePage = HID_USAGE_PAGE_GENERIC,
            .usUsage = HID_USAGE_GENERIC_MOUSE,
            .dwFlags = RIDEV_INPUTSINK,
            .hwndTarget = window_,
        };

        return RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()), sizeof(RAWINPUTDEVICE)) !=
               FALSE;
    }

    bool add_tray_icon() noexcept
    {
        tray_icon_ = {};
        tray_icon_.cbSize = sizeof(tray_icon_);
        tray_icon_.hWnd = window_;
        tray_icon_.uID = tray_icon_id;
        tray_icon_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        tray_icon_.uCallbackMessage = message_tray;
        tray_icon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(tray_icon_.szTip, window_title);

        if (Shell_NotifyIconW(NIM_ADD, &tray_icon_) == FALSE)
        {
            return false;
        }

        tray_icon_.uVersion = NOTIFYICON_VERSION_4;
        static_cast<void>(Shell_NotifyIconW(NIM_SETVERSION, &tray_icon_));
        tray_icon_added_ = true;
        return true;
    }

    void remove_tray_icon() noexcept
    {
        if (tray_icon_added_)
        {
            static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &tray_icon_));
            tray_icon_added_ = false;
        }
    }

    void show_tray_notification(const wchar_t* title, const wchar_t* message, const DWORD flags) noexcept
    {
        if (!tray_icon_added_)
        {
            return;
        }

        auto notification = tray_icon_;
        notification.uFlags = NIF_INFO;
        notification.dwInfoFlags = flags;
        wcscpy_s(notification.szInfoTitle, title);
        wcscpy_s(notification.szInfo, message);
        static_cast<void>(Shell_NotifyIconW(NIM_MODIFY, &notification));
    }

    void show_tray_menu() noexcept
    {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }

        const UINT pause_flags = MF_STRING | (paused_ ? MF_CHECKED : MF_UNCHECKED);
        static_cast<void>(AppendMenuW(menu, pause_flags, command_toggle_pause, L"暂停(&P)"));
        const UINT shake_flags = MF_STRING | (shake_enabled_ ? MF_CHECKED : MF_UNCHECKED);
        static_cast<void>(AppendMenuW(menu, shake_flags, command_toggle_shake, L"晃动触发（实验）(&S)"));
        const UINT timeout_flags = MF_STRING | (auto_timeout_enabled_ ? MF_CHECKED : MF_UNCHECKED);
        static_cast<void>(AppendMenuW(menu, timeout_flags, command_toggle_auto_timeout, L"自动超时(&T)"));
        static_cast<void>(AppendMenuW(menu, MF_SEPARATOR, 0, nullptr));
        static_cast<void>(AppendMenuW(menu, MF_STRING, command_reload_configuration, L"重新加载配置(&R)"));
        static_cast<void>(AppendMenuW(menu, MF_STRING, command_export_default_configuration, L"导出默认配置(&D)"));
        static_cast<void>(AppendMenuW(menu, MF_SEPARATOR, 0, nullptr));
        static_cast<void>(AppendMenuW(menu, MF_STRING, command_exit, L"退出(&X)"));

        POINT cursor{};
        static_cast<void>(GetCursorPos(&cursor));
        static_cast<void>(SetForegroundWindow(window_));
        static_cast<void>(TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursor.x, cursor.y,
                                           window_, nullptr));
        static_cast<void>(DestroyMenu(menu));
    }

    void toggle_pause() noexcept
    {
        paused_ = !paused_;
        dismiss_overlay();
        double_ctrl_detector_.reset();
        shake_detector_.reset();
    }

    void toggle_shake() noexcept
    {
        shake_enabled_ = !shake_enabled_;
        settings_.shake_enabled = shake_enabled_;
        shake_detector_.reset();
        persist_preferences();
    }

    void toggle_auto_timeout() noexcept
    {
        auto_timeout_enabled_ = !auto_timeout_enabled_;
        settings_.auto_timeout_enabled = auto_timeout_enabled_;
        persist_preferences();
        if (!overlay_manager_.visible())
        {
            return;
        }

        if (!auto_timeout_enabled_)
        {
            static_cast<void>(KillTimer(window_, overlay_timer_id));
            return;
        }

        const ULONGLONG now = GetTickCount64();
        overlay_started_at_ = now;
        last_cursor_move_at_ = now;
        static_cast<void>(SetTimer(window_, overlay_timer_id, overlay_timer_interval_ms, nullptr));
    }

    void apply_settings(const zmouse::config::Settings& settings) noexcept
    {
        if (window_ != nullptr)
        {
            dismiss_overlay();
        }

        settings_ = settings;
        shake_enabled_ = settings.shake_enabled;
        auto_timeout_enabled_ = settings.auto_timeout_enabled;
        overlay_idle_timeout_ms_ = settings.idle_timeout_ms;
        overlay_max_duration_ms_ = settings.maximum_duration_ms;
        double_ctrl_detector_.configure(settings.double_ctrl);
        shake_detector_.configure(settings.shake);
        overlay_manager_.configure(settings.spotlight_radius_dip, settings.dim_opacity_percent);
    }

    void reload_configuration() noexcept
    {
        const auto loaded = zmouse::config::load_toml(config_path_);
        if (!loaded)
        {
            show_tray_notification(L"ZMouseShow", L"配置重新加载失败，已保留当前设置。", NIIF_WARNING);
            return;
        }

        apply_settings(*loaded);
        show_tray_notification(L"ZMouseShow", L"配置已重新加载。", NIIF_INFO);
    }

    void export_default_configuration() noexcept
    {
        if (zmouse::config::write_default_toml(config_path_))
        {
            show_tray_notification(L"ZMouseShow", L"默认 TOML 配置已导出到程序配置路径。", NIIF_INFO);
            return;
        }
        show_tray_notification(L"ZMouseShow", L"未导出：文件可能已存在或路径不可写。", NIIF_WARNING);
    }

    void persist_preferences() noexcept
    {
        if (!zmouse::config::persist_runtime_preferences(config_path_, shake_enabled_, auto_timeout_enabled_))
        {
            show_tray_notification(L"ZMouseShow", L"设置已生效，但无法保存到 TOML 配置。", NIIF_WARNING);
        }
    }

    void activate_overlay(const bool suppress_ctrl_release) noexcept
    {
        POINT cursor{};
        if (GetCursorPos(&cursor) == FALSE)
        {
            return;
        }

        const zmouse::overlay::Point position{cursor.x, cursor.y};
        if (!overlay_manager_.show_at(position))
        {
            return;
        }

        const ULONGLONG now = GetTickCount64();
        overlay_started_at_ = now;
        last_cursor_move_at_ = now;
        last_cursor_position_ = position;
        suppress_trigger_ctrl_release_ = suppress_ctrl_release;
        double_ctrl_detector_.reset();
        shake_detector_.reset();
        if (auto_timeout_enabled_)
        {
            static_cast<void>(SetTimer(window_, overlay_timer_id, overlay_timer_interval_ms, nullptr));
        }
    }

    void dismiss_overlay() noexcept
    {
        if (overlay_manager_.visible())
        {
            overlay_manager_.hide();
        }
        static_cast<void>(KillTimer(window_, overlay_timer_id));
        suppress_trigger_ctrl_release_ = false;
        double_ctrl_detector_.reset();
        shake_detector_.reset();
    }

    void update_overlay_cursor() noexcept
    {
        if (!overlay_manager_.visible())
        {
            return;
        }

        POINT cursor{};
        if (GetCursorPos(&cursor) == FALSE)
        {
            dismiss_overlay();
            return;
        }

        const zmouse::overlay::Point position{cursor.x, cursor.y};
        if (position == last_cursor_position_)
        {
            return;
        }

        last_cursor_position_ = position;
        last_cursor_move_at_ = GetTickCount64();
        overlay_manager_.move_to(position);
        if (!overlay_manager_.visible())
        {
            static_cast<void>(KillTimer(window_, overlay_timer_id));
        }
    }

    void on_overlay_timer() noexcept
    {
        if (!auto_timeout_enabled_ || !overlay_manager_.visible())
        {
            static_cast<void>(KillTimer(window_, overlay_timer_id));
            return;
        }

        update_overlay_cursor();
        const ULONGLONG now = GetTickCount64();
        if (now - last_cursor_move_at_ >= overlay_idle_timeout_ms_ ||
            now - overlay_started_at_ >= overlay_max_duration_ms_)
        {
            dismiss_overlay();
        }
    }

    LRESULT handle_message(const UINT message, const WPARAM w_param, const LPARAM l_param) noexcept
    {
        if (taskbar_created_message_ != 0 && message == taskbar_created_message_)
        {
            tray_icon_added_ = false;
            static_cast<void>(add_tray_icon());
            return 0;
        }

        switch (message)
        {
        case WM_INPUT:
            handle_raw_input(reinterpret_cast<HRAWINPUT>(l_param));
            return 0;

        case WM_COMMAND:
            switch (LOWORD(w_param))
            {
            case command_toggle_pause:
                toggle_pause();
                return 0;
            case command_toggle_shake:
                toggle_shake();
                return 0;
            case command_toggle_auto_timeout:
                toggle_auto_timeout();
                return 0;
            case command_reload_configuration:
                reload_configuration();
                return 0;
            case command_export_default_configuration:
                export_default_configuration();
                return 0;
            case command_exit:
                DestroyWindow(window_);
                return 0;
            default:
                break;
            }
            break;

        case message_tray:
            if (LOWORD(l_param) == WM_CONTEXTMENU || LOWORD(l_param) == WM_RBUTTONUP)
            {
                show_tray_menu();
            }
            return 0;

        case message_activate:
            activate_overlay(w_param != 0);
            return 0;

        case WM_TIMER:
            if (w_param == overlay_timer_id)
            {
                on_overlay_timer();
                return 0;
            }
            break;

        case WM_DISPLAYCHANGE:
            static_cast<void>(overlay_manager_.rebuild());
            return 0;

        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;

        case WM_DESTROY:
            dismiss_overlay();
            remove_tray_icon();
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }

        return DefWindowProcW(window_, message, w_param, l_param);
    }

    void handle_raw_input(const HRAWINPUT handle) noexcept
    {
        RAWINPUT input{};
        UINT size = sizeof(input);
        const UINT read = GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));
        if (read == static_cast<UINT>(-1) || read < sizeof(RAWINPUTHEADER))
        {
            return;
        }

        if (input.header.dwType == RIM_TYPEKEYBOARD)
        {
            handle_keyboard(input.data.keyboard);
        }
        else if (input.header.dwType == RIM_TYPEMOUSE)
        {
            handle_mouse(input.data.mouse);
        }
    }

    void handle_keyboard(const RAWKEYBOARD& keyboard) noexcept
    {
        if (keyboard.VKey == 0xFFU)
        {
            return;
        }

        const bool extended = (keyboard.Flags & RI_KEY_E0) != 0;
        const std::size_t key_id = static_cast<std::size_t>(keyboard.VKey) + (extended ? 256U : 0U);
        if (key_id >= key_down_.size())
        {
            return;
        }

        const bool pressed = (keyboard.Flags & RI_KEY_BREAK) == 0;
        const bool repeated = pressed && key_down_[key_id];
        key_down_[key_id] = pressed;

        const bool left_control = keyboard.VKey == VK_LCONTROL || (keyboard.VKey == VK_CONTROL && !extended);

        const bool overlay_was_visible = overlay_manager_.visible();
        if (overlay_was_visible && !repeated)
        {
            if (left_control && !pressed && suppress_trigger_ctrl_release_)
            {
                suppress_trigger_ctrl_release_ = false;
            }
            else
            {
                dismiss_overlay();
            }
        }

        bool any_other_key_down = false;
        for (std::size_t index = 0; index < key_down_.size(); ++index)
        {
            if (index != key_id && key_down_[index])
            {
                any_other_key_down = true;
                break;
            }
        }

        if (paused_ || overlay_was_visible)
        {
            return;
        }

        const zmouse::input::KeyEvent event{
            .key = left_control ? zmouse::input::KeyKind::left_control : zmouse::input::KeyKind::other,
            .pressed = pressed,
            .repeated = repeated,
            .timestamp = GetTickCount64(),
        };
        if (double_ctrl_detector_.process(event, any_other_key_down, mouse_buttons_ != 0))
        {
            static_cast<void>(PostMessageW(window_, message_activate, 1, 0));
        }
    }

    void handle_mouse(const RAWMOUSE& mouse) noexcept
    {
        const bool overlay_was_visible = overlay_manager_.visible();
        const USHORT flags = mouse.usButtonFlags;
        constexpr USHORT button_down_mask = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN |
                                            RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_BUTTON_4_DOWN |
                                            RI_MOUSE_BUTTON_5_DOWN;
        constexpr USHORT wheel_mask = RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL;
        if (overlay_was_visible && (flags & (button_down_mask | wheel_mask)) != 0)
        {
            dismiss_overlay();
        }

        update_mouse_button(flags, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, mouse_left);
        update_mouse_button(flags, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, mouse_right);
        update_mouse_button(flags, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, mouse_middle);
        update_mouse_button(flags, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, mouse_x1);
        update_mouse_button(flags, RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, mouse_x2);

        constexpr USHORT button_transition_mask =
            RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_DOWN |
            RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
            RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;
        if ((flags & button_transition_mask) != 0)
        {
            double_ctrl_detector_.on_mouse_button_event();
            shake_detector_.on_mouse_buttons_changed(mouse_buttons_ != 0);
        }

        if (overlay_manager_.visible() && (mouse.lLastX != 0 || mouse.lLastY != 0))
        {
            update_overlay_cursor();
        }

        if (overlay_was_visible || paused_ || !shake_enabled_ || (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
        {
            return;
        }

        const zmouse::input::RelativeMovement movement{
            .dx = mouse.lLastX,
            .dy = mouse.lLastY,
            .timestamp = GetTickCount64(),
        };
        if (shake_detector_.process(movement, false))
        {
            static_cast<void>(PostMessageW(window_, message_activate, 0, 0));
        }
    }

    void update_mouse_button(const USHORT flags, const USHORT down_flag, const USHORT up_flag,
                             const std::uint8_t button) noexcept
    {
        if ((flags & down_flag) != 0)
        {
            mouse_buttons_ = static_cast<std::uint8_t>(mouse_buttons_ | button);
        }
        if ((flags & up_flag) != 0)
        {
            mouse_buttons_ = static_cast<std::uint8_t>(mouse_buttons_ & static_cast<std::uint8_t>(~button));
        }
    }

    HINSTANCE instance_{};
    HWND window_{};
    std::filesystem::path config_path_;
    zmouse::config::Settings settings_{};
    UINT taskbar_created_message_{};
    NOTIFYICONDATAW tray_icon_{};
    bool tray_icon_added_{};
    bool paused_{};
    bool shake_enabled_{};
    bool auto_timeout_enabled_{};
    bool suppress_trigger_ctrl_release_{};
    std::array<bool, 512> key_down_{};
    std::uint8_t mouse_buttons_{};
    ULONGLONG overlay_idle_timeout_ms_{1'200};
    ULONGLONG overlay_max_duration_ms_{5'000};
    ULONGLONG overlay_started_at_{};
    ULONGLONG last_cursor_move_at_{};
    zmouse::overlay::Point last_cursor_position_{};
    zmouse::input::DoubleCtrlDetector double_ctrl_detector_{};
    zmouse::input::ShakeDetector shake_detector_{};
    zmouse::platform::OverlayManager overlay_manager_{};
};
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    static_cast<void>(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

    HANDLE instance_mutex = CreateMutexW(nullptr, TRUE, instance_mutex_name);
    if (instance_mutex == nullptr)
    {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(instance_mutex);
        return 0;
    }

    Application application(instance, resolve_config_path());
    const int result = application.run();
    static_cast<void>(ReleaseMutex(instance_mutex));
    static_cast<void>(CloseHandle(instance_mutex));
    return result;
}

#include <windows.h>

#include <hidusage.h>
#include <shellapi.h>

#include "display_simulator.hpp"
#include "foreground_context.hpp"
#include "overlay_manager.hpp"
#include "settings_dialog.hpp"
#include "startup_registration.hpp"
#include "zmouse/platform/string_util.hpp"
#include "zmouse/config/settings.hpp"
#include "zmouse/diagnostics/report.hpp"
#include "zmouse/input/double_ctrl_detector.hpp"
#include "zmouse/input/hotkey_detector.hpp"
#include "zmouse/input/overlay_input_rules.hpp"
#include "zmouse/input/shake_detector.hpp"
#include "zmouse/overlay/locator_animation.hpp"
#include "zmouse/platform/desktop_duplication_capture.hpp"
#include "zmouse/platform/global_hotkey_registration.hpp"
#include "zmouse/platform/magnifier_window.hpp"
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <shellscalingapi.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
constexpr UINT command_export_diagnostics = 1007;
constexpr UINT command_settings = 1008;
constexpr UINT message_tray = WM_APP + 1;
constexpr UINT message_activate = WM_APP + 2;
constexpr UINT_PTR overlay_timer_id = 1;
constexpr int primary_hotkey_id = 0x5A01;
constexpr int secondary_hotkey_id = 0x5A02;
constexpr DWORD high_resolution_waitable_timer_flag = 0x00000002;
constexpr UINT animation_timer_interval_ms = 8;
constexpr UINT timeout_timer_interval_ms = 50;

constexpr std::uint8_t mouse_left = 1U << 0U;
constexpr std::uint8_t mouse_right = 1U << 1U;
constexpr std::uint8_t mouse_middle = 1U << 2U;
constexpr std::uint8_t mouse_x1 = 1U << 3U;
constexpr std::uint8_t mouse_x2 = 1U << 4U;

struct MonitorCollection
{
    std::vector<zmouse::diagnostics::Monitor> monitors;
    bool success{true};
};

using zmouse::platform::wide_to_utf8;

[[nodiscard]] std::string current_utc_time()
{
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char buffer[32]{};
    const int length = sprintf_s(buffer, "%04u-%02u-%02uT%02u:%02u:%02uZ", static_cast<unsigned>(time.wYear),
                                 static_cast<unsigned>(time.wMonth), static_cast<unsigned>(time.wDay),
                                 static_cast<unsigned>(time.wHour), static_cast<unsigned>(time.wMinute),
                                 static_cast<unsigned>(time.wSecond));
    return length > 0 ? std::string(buffer, static_cast<std::size_t>(length)) : std::string{};
}

BOOL CALLBACK collect_monitor_diagnostics(const HMONITOR monitor, HDC, LPRECT, const LPARAM data) noexcept
{
    auto& collection = *reinterpret_cast<MonitorCollection*>(data);
    try
    {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) == FALSE)
        {
            collection.success = false;
            return FALSE;
        }

        UINT dpi_x = 96;
        UINT dpi_y = 96;
        if (GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y) != S_OK)
        {
            dpi_x = 96;
            dpi_y = 96;
        }
        collection.monitors.push_back({
            .device_name = wide_to_utf8(info.szDevice),
            .bounds = {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom},
            .work_area = {info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom},
            .dpi_x = dpi_x,
            .dpi_y = dpi_y,
            .primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
        });
        return TRUE;
    }
    catch (...)
    {
        collection.success = false;
        return FALSE;
    }
}

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

[[nodiscard]] bool has_command_line_switch(const wchar_t* expected) noexcept
{
    int argument_count = 0;
    auto* raw_arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (raw_arguments == nullptr)
    {
        return false;
    }
    bool found = false;
    for (int index = 1; index < argument_count; ++index)
    {
        if (_wcsicmp(raw_arguments[index], expected) == 0)
        {
            found = true;
            break;
        }
    }
    static_cast<void>(LocalFree(raw_arguments));
    return found;
}

class Application final
{
  public:
    Application(HINSTANCE instance, std::filesystem::path config_path) noexcept
        : instance_(instance), config_path_(std::move(config_path))
    {
    }

    ~Application()
    {
        hotkey_registration_.reset();
        stop_magnifier_capture();
        stop_animation_timer();
        if (animation_timer_ != nullptr)
        {
            static_cast<void>(CloseHandle(animation_timer_));
        }
    }

    int run()
    {
        if (auto loaded = zmouse::config::load_toml(config_path_))
        {
            apply_settings(std::move(*loaded));
        }
        else
        {
            apply_settings({});
        }

        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
        if (!create_window() || !overlay_manager_.initialize(instance_) || !magnifier_window_.initialize(instance_) ||
            !register_raw_input())
        {
            if (window_ != nullptr)
            {
                DestroyWindow(window_);
            }
            return 1;
        }

        const bool hotkey_available = replace_hotkey_registration(settings_.hotkey);
        if (!add_tray_icon())
        {
            DestroyWindow(window_);
            return 1;
        }
        if (!zmouse::platform::set_startup_registration_enabled(settings_.startup_enabled))
        {
            show_tray_notification(L"ZMouseShow", L"无法更新当前用户的登录自启动设置，可能受到企业策略限制。",
                                   NIIF_WARNING);
        }
        if (!hotkey_available)
        {
            show_tray_notification(L"ZMouseShow", L"自定义组合键已被其它程序占用，本次运行已禁用该触发方式。",
                                   NIIF_WARNING);
        }

        if (has_command_line_switch(L"--settings"))
        {
            show_settings();
        }

        initialize_animation_timer();
        const DWORD wait_handle_count = animation_timer_ != nullptr ? 1U : 0U;
        const HANDLE* wait_handles = animation_timer_ != nullptr ? &animation_timer_ : nullptr;
        while (true)
        {
            const DWORD wait_result = MsgWaitForMultipleObjectsEx(wait_handle_count, wait_handles, INFINITE,
                                                                  QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wait_result == WAIT_FAILED)
            {
                return 1;
            }
            if (animation_timer_ != nullptr && wait_result == WAIT_OBJECT_0)
            {
                on_overlay_timer();
                continue;
            }

            if (wait_result != WAIT_OBJECT_0 + wait_handle_count)
            {
                return 1;
            }

            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
            {
                if (message.message == WM_QUIT)
                {
                    return static_cast<int>(message.wParam);
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
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

    [[nodiscard]] zmouse::input::OverlayInputState input_state() const noexcept
    {
        return {
            .paused = paused_,
            .activation_pending = activation_pending_,
            .overlay_visible = overlay_manager_.visible(),
            .tray_menu_open = tray_menu_open_,
            .settings_dialog_open = settings_dialog_open_,
        };
    }

    void synchronize_input_state() noexcept
    {
        const auto key_is_down = [](const int virtual_key) noexcept
        { return (GetAsyncKeyState(virtual_key) & static_cast<SHORT>(0x8000)) != 0; };

        key_down_.reset();
        for (std::size_t virtual_key = 1; virtual_key < 0xFFU; ++virtual_key)
        {
            if (virtual_key == VK_LBUTTON || virtual_key == VK_RBUTTON || virtual_key == VK_MBUTTON ||
                virtual_key == VK_XBUTTON1 || virtual_key == VK_XBUTTON2 || virtual_key == VK_SHIFT ||
                virtual_key == VK_LSHIFT || virtual_key == VK_RSHIFT || virtual_key == VK_CONTROL ||
                virtual_key == VK_LCONTROL || virtual_key == VK_RCONTROL || virtual_key == VK_MENU ||
                virtual_key == VK_LMENU || virtual_key == VK_RMENU)
            {
                continue;
            }
            key_down_.set(virtual_key, key_is_down(static_cast<int>(virtual_key)));
        }
        key_down_.set(VK_SHIFT, key_is_down(VK_SHIFT));
        key_down_.set(VK_CONTROL, key_is_down(VK_LCONTROL));
        key_down_.set(VK_CONTROL + 256U, key_is_down(VK_RCONTROL));
        key_down_.set(VK_MENU, key_is_down(VK_LMENU));
        key_down_.set(VK_MENU + 256U, key_is_down(VK_RMENU));

        mouse_buttons_ = 0;
        if (key_is_down(VK_LBUTTON))
        {
            mouse_buttons_ = static_cast<std::uint8_t>(mouse_buttons_ | mouse_left);
        }
        if (key_is_down(VK_RBUTTON))
        {
            mouse_buttons_ = static_cast<std::uint8_t>(mouse_buttons_ | mouse_right);
        }
        if (key_is_down(VK_MBUTTON))
        {
            mouse_buttons_ = static_cast<std::uint8_t>(mouse_buttons_ | mouse_middle);
        }
        if (key_is_down(VK_XBUTTON1))
        {
            mouse_buttons_ = static_cast<std::uint8_t>(mouse_buttons_ | mouse_x1);
        }
        if (key_is_down(VK_XBUTTON2))
        {
            mouse_buttons_ = static_cast<std::uint8_t>(mouse_buttons_ | mouse_x2);
        }

        double_ctrl_detector_.reset();
        shake_detector_.reset();
        shake_detector_.on_mouse_buttons_changed(mouse_buttons_ != 0);
    }

    void show_tray_menu() noexcept
    {
        if (tray_menu_open_)
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }

        tray_menu_open_ = true;
        hide_overlay_immediately();

        static_cast<void>(AppendMenuW(menu, MF_STRING, command_settings, L"设置(&O)..."));
        static_cast<void>(AppendMenuW(menu, MF_SEPARATOR, 0, nullptr));
        const UINT pause_flags = MF_STRING | (paused_ ? MF_CHECKED : MF_UNCHECKED);
        static_cast<void>(AppendMenuW(menu, pause_flags, command_toggle_pause, L"暂停(&P)"));
        const UINT shake_flags = MF_STRING | (shake_enabled_ ? MF_CHECKED : MF_UNCHECKED);
        static_cast<void>(AppendMenuW(menu, shake_flags, command_toggle_shake, L"晃动触发（实验）(&S)"));
        const UINT timeout_flags = MF_STRING | (auto_timeout_enabled_ ? MF_CHECKED : MF_UNCHECKED);
        static_cast<void>(AppendMenuW(menu, timeout_flags, command_toggle_auto_timeout, L"自动超时(&T)"));
        static_cast<void>(AppendMenuW(menu, MF_SEPARATOR, 0, nullptr));
        static_cast<void>(AppendMenuW(menu, MF_STRING, command_reload_configuration, L"重新加载配置(&R)"));
        static_cast<void>(AppendMenuW(menu, MF_STRING, command_export_default_configuration, L"导出默认配置(&D)"));
        static_cast<void>(AppendMenuW(menu, MF_STRING, command_export_diagnostics, L"导出诊断信息(&I)"));
        static_cast<void>(AppendMenuW(menu, MF_SEPARATOR, 0, nullptr));
        static_cast<void>(AppendMenuW(menu, MF_STRING, command_exit, L"退出(&X)"));

        POINT cursor{};
        static_cast<void>(GetCursorPos(&cursor));
        static_cast<void>(SetForegroundWindow(window_));
        static_cast<void>(TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursor.x, cursor.y,
                                           window_, nullptr));
        static_cast<void>(DestroyMenu(menu));
        tray_menu_open_ = false;
        synchronize_input_state();
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
            schedule_overlay_timer();
            return;
        }

        const ULONGLONG now = GetTickCount64();
        overlay_started_at_ = now;
        last_cursor_move_at_ = now;
        schedule_overlay_timer();
    }

    void apply_settings(zmouse::config::Settings settings) noexcept
    {
        if (window_ != nullptr)
        {
            hide_overlay_immediately();
        }

        settings_ = std::move(settings);
        shake_enabled_ = settings_.shake_enabled;
        auto_timeout_enabled_ = settings_.auto_timeout_enabled;
        overlay_idle_timeout_ms_ = settings_.idle_timeout_ms;
        overlay_max_duration_ms_ = settings_.maximum_duration_ms;
        double_ctrl_detector_.configure(settings_.double_ctrl);
        hotkey_detector_.configure(settings_.hotkey);
        shake_detector_.configure(settings_.shake);
        BOOL animations_enabled = TRUE;
        static_cast<void>(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations_enabled, 0));
        auto overlay_effects = settings_.effects;
        auto magnifier_settings = settings_.magnifier;
        if (animations_enabled == FALSE)
        {
            overlay_effects.ripple_enabled = false;
            magnifier_settings.edge_effect = zmouse::magnifier::EdgeEffect::off;
        }
        if (settings_.magnifier.enabled)
        {
            overlay_effects.enlarged_cursor_enabled = false;
        }
        overlay_manager_.configure(settings_.spotlight_radius_dip, settings_.spotlight_shape, overlay_effects,
                                   settings_.dim_opacity_percent, settings_.dim_enabled);
        magnifier_window_.configure(magnifier_settings);
        if (!settings_.magnifier.enabled)
        {
            stop_magnifier_capture();
        }
    }

    [[nodiscard]] bool prepare_hotkey_registration(const zmouse::input::HotkeyConfig& config,
                                                   zmouse::platform::GlobalHotkeyRegistration& replacement,
                                                   bool& changed) const noexcept
    {
        changed = !hotkey_registration_.matches(config);
        if (!changed || !config.enabled)
        {
            return true;
        }

        const int replacement_id =
            hotkey_registration_.identifier() == primary_hotkey_id ? secondary_hotkey_id : primary_hotkey_id;
        return replacement.acquire(window_, replacement_id, config);
    }

    void commit_hotkey_registration(zmouse::platform::GlobalHotkeyRegistration&& replacement, const bool changed,
                                    const zmouse::input::HotkeyConfig& config) noexcept
    {
        if (!changed)
        {
            return;
        }
        hotkey_registration_.reset();
        if (config.enabled)
        {
            hotkey_registration_ = std::move(replacement);
        }
    }

    [[nodiscard]] bool replace_hotkey_registration(const zmouse::input::HotkeyConfig& config) noexcept
    {
        zmouse::platform::GlobalHotkeyRegistration replacement;
        bool changed = false;
        if (!prepare_hotkey_registration(config, replacement, changed))
        {
            return false;
        }
        commit_hotkey_registration(std::move(replacement), changed, config);
        return true;
    }

    void reload_configuration() noexcept
    {
        auto loaded = zmouse::config::load_toml(config_path_);
        if (!loaded)
        {
            show_tray_notification(L"ZMouseShow", L"配置重新加载失败，已保留当前设置。", NIIF_WARNING);
            return;
        }

        if (!replace_hotkey_registration(loaded->hotkey))
        {
            show_tray_notification(L"ZMouseShow", L"新的自定义组合键已被占用，已保留当前设置。", NIIF_WARNING);
            return;
        }
        apply_settings(std::move(*loaded));
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

    void export_diagnostics() noexcept
    {
        try
        {
            MonitorCollection collection;
            if (EnumDisplayMonitors(nullptr, nullptr, collect_monitor_diagnostics,
                                    reinterpret_cast<LPARAM>(&collection)) == FALSE ||
                !collection.success)
            {
                show_tray_notification(L"ZMouseShow", L"无法枚举显示器，诊断信息未导出。", NIIF_WARNING);
                return;
            }

            const int virtual_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
            const int virtual_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
            const auto report_path = config_path_.parent_path() / L"ZMouseShow-diagnostics.txt";
            const zmouse::diagnostics::Snapshot snapshot{
                .version = ZMOUSE_VERSION,
#if defined(NDEBUG)
                .build_type = "Release",
#else
                .build_type = "Debug",
#endif
#if defined(_M_X64)
                .architecture = "x64",
#else
                .architecture = "unknown",
#endif
                .generated_at_utc = current_utc_time(),
                .config_path = wide_to_utf8(config_path_.wstring()),
                .paused = paused_,
                .activation_pending = activation_pending_,
                .overlay_visible = overlay_manager_.visible(),
                .tray_menu_open = tray_menu_open_,
                .settings_dialog_open = settings_dialog_open_,
                .remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0,
                .custom_hotkey_registered = hotkey_registration_.active(),
                .startup_registered = zmouse::platform::startup_registration_enabled(),
                .virtual_desktop = {virtual_left, virtual_top, virtual_left + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                                    virtual_top + GetSystemMetrics(SM_CYVIRTUALSCREEN)},
                .monitors = std::move(collection.monitors),
                .settings = settings_,
                .capture =
                    magnifier_capture_ != nullptr ? magnifier_capture_->diagnostics() : zmouse::capture::Diagnostics{},
            };
            if (!zmouse::diagnostics::write_report(report_path, snapshot))
            {
                show_tray_notification(L"ZMouseShow", L"诊断信息无法写入配置目录。", NIIF_WARNING);
                return;
            }
            show_tray_notification(L"ZMouseShow", L"诊断信息已导出到配置目录。", NIIF_INFO);
        }
        catch (...)
        {
            show_tray_notification(L"ZMouseShow", L"导出诊断信息时发生错误。", NIIF_WARNING);
        }
    }

    void persist_preferences() noexcept
    {
        if (!zmouse::config::persist_runtime_preferences(config_path_, shake_enabled_, auto_timeout_enabled_))
        {
            show_tray_notification(L"ZMouseShow", L"设置已生效，但无法保存到 TOML 配置。", NIIF_WARNING);
        }
    }

    static bool apply_dialog_settings(void* context, const zmouse::config::Settings& settings) noexcept
    {
        auto& application = *static_cast<Application*>(context);
        zmouse::config::Settings replacement_settings;
        try
        {
            replacement_settings = settings;
        }
        catch (...)
        {
            return false;
        }
        zmouse::platform::GlobalHotkeyRegistration replacement;
        bool hotkey_changed = false;
        if (!application.prepare_hotkey_registration(settings.hotkey, replacement, hotkey_changed))
        {
            return false;
        }
        const bool startup_was_enabled = zmouse::platform::startup_registration_enabled();
        if (!zmouse::platform::set_startup_registration_enabled(settings.startup_enabled))
        {
            return false;
        }
        if (!zmouse::config::persist_basic_settings(application.config_path_, settings))
        {
            static_cast<void>(zmouse::platform::set_startup_registration_enabled(startup_was_enabled));
            return false;
        }
        application.commit_hotkey_registration(std::move(replacement), hotkey_changed, settings.hotkey);
        application.apply_settings(std::move(replacement_settings));
        return true;
    }

    void show_settings() noexcept
    {
        if (settings_dialog_open_)
        {
            return;
        }
        settings_dialog_open_ = true;
        hide_overlay_immediately();
        static_cast<void>(
            zmouse::platform::show_settings_dialog(instance_, nullptr, settings_, apply_dialog_settings, this));
        settings_dialog_open_ = false;
        synchronize_input_state();
    }

    void activate_overlay() noexcept
    {
        if (!zmouse::input::triggers_armed(input_state()))
        {
            return;
        }

        POINT cursor{};
        if (GetCursorPos(&cursor) == FALSE)
        {
            return;
        }

        const zmouse::overlay::Point position{cursor.x, cursor.y};
        const ULONGLONG now = GetTickCount64();
        locator_animation_.show(now);
        const auto initial_frame = locator_animation_.frame(now);
        overlay_manager_.set_animation_frame(initial_frame.dim_progress, initial_frame.focus_opacity,
                                             initial_frame.ripple_scale, initial_frame.ripple_opacity);
        if (!overlay_manager_.show_at(position))
        {
            locator_animation_.reset();
            return;
        }

        update_magnifier_capture({cursor.x, cursor.y});

        overlay_started_at_ = now;
        last_cursor_move_at_ = now;
        last_cursor_position_ = position;
        cursor_update_pending_ = false;
        double_ctrl_detector_.reset();
        shake_detector_.reset();
        schedule_overlay_timer();
    }

    void request_overlay_activation(const zmouse::policy::TriggerSource source) noexcept
    {
        if (!zmouse::input::triggers_armed(input_state()))
        {
            return;
        }
        if (!zmouse::policy::should_allow_activation(settings_.activation_policy, source,
                                                     zmouse::platform::query_foreground_context()))
        {
            return;
        }

        activation_pending_ = true;
        if (PostMessageW(window_, message_activate, 0, 0) == FALSE)
        {
            activation_pending_ = false;
        }
    }

    void dismiss_overlay() noexcept
    {
        if (!overlay_manager_.visible())
        {
            static_cast<void>(KillTimer(window_, overlay_timer_id));
            stop_magnifier_capture();
            return;
        }

        const ULONGLONG now = GetTickCount64();
        locator_animation_.hide(now);
        const auto frame = locator_animation_.frame(now);
        overlay_manager_.set_animation_frame(frame.dim_progress, frame.focus_opacity, frame.ripple_scale,
                                             frame.ripple_opacity);
        double_ctrl_detector_.reset();
        shake_detector_.reset();
        schedule_overlay_timer();
    }

    void hide_overlay_immediately() noexcept
    {
        activation_pending_ = false;
        overlay_manager_.hide();
        stop_magnifier_capture();
        locator_animation_.reset();
        cursor_update_pending_ = false;
        stop_animation_timer();
        static_cast<void>(KillTimer(window_, overlay_timer_id));
        double_ctrl_detector_.reset();
        shake_detector_.reset();
    }

    void initialize_animation_timer() noexcept
    {
        animation_timer_ = CreateWaitableTimerExW(nullptr, nullptr, high_resolution_waitable_timer_flag,
                                                  TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (animation_timer_ == nullptr)
        {
            animation_timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        }
    }

    [[nodiscard]] bool start_animation_timer() noexcept
    {
        if (animation_timer_ == nullptr)
        {
            return false;
        }
        if (animation_timer_running_)
        {
            return true;
        }

        LARGE_INTEGER due_time{};
        due_time.QuadPart = -static_cast<LONGLONG>(animation_timer_interval_ms) * 10'000LL;
        if (SetWaitableTimer(animation_timer_, &due_time, static_cast<LONG>(animation_timer_interval_ms), nullptr,
                             nullptr, FALSE) == FALSE)
        {
            return false;
        }
        animation_timer_running_ = true;
        return true;
    }

    void stop_animation_timer() noexcept
    {
        if (animation_timer_ != nullptr && animation_timer_running_)
        {
            static_cast<void>(CancelWaitableTimer(animation_timer_));
        }
        animation_timer_running_ = false;
    }

    void schedule_overlay_timer() noexcept
    {
        if (!overlay_manager_.visible())
        {
            stop_animation_timer();
            static_cast<void>(KillTimer(window_, overlay_timer_id));
            return;
        }

        const auto phase = locator_animation_.phase();
        const bool animating = phase == zmouse::overlay::AnimationPhase::appearing ||
                               phase == zmouse::overlay::AnimationPhase::disappearing;
        if (animating || cursor_update_pending_)
        {
            static_cast<void>(KillTimer(window_, overlay_timer_id));
            if (!start_animation_timer())
            {
                static_cast<void>(SetTimer(window_, overlay_timer_id, animation_timer_interval_ms, nullptr));
            }
            return;
        }

        stop_animation_timer();
        if (!auto_timeout_enabled_)
        {
            static_cast<void>(KillTimer(window_, overlay_timer_id));
            return;
        }

        static_cast<void>(SetTimer(window_, overlay_timer_id, timeout_timer_interval_ms, nullptr));
    }

    void update_overlay_cursor() noexcept
    {
        cursor_update_pending_ = false;
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
            update_magnifier_capture(position);
            return;
        }

        last_cursor_position_ = position;
        last_cursor_move_at_ = GetTickCount64();
        overlay_manager_.move_to(position);
        update_magnifier_capture(position);
        if (!overlay_manager_.visible())
        {
            hide_overlay_immediately();
        }
    }

    void on_overlay_timer() noexcept
    {
        if (!overlay_manager_.visible())
        {
            hide_overlay_immediately();
            return;
        }

        update_overlay_cursor();
        if (!overlay_manager_.visible())
        {
            return;
        }

        const ULONGLONG now = GetTickCount64();
        const auto frame = locator_animation_.frame(now);
        if (!frame.surface_visible)
        {
            hide_overlay_immediately();
            return;
        }
        overlay_manager_.set_animation_frame(frame.dim_progress, frame.focus_opacity, frame.ripple_scale,
                                             frame.ripple_opacity);
        if (!overlay_manager_.visible())
        {
            hide_overlay_immediately();
            return;
        }

        if (locator_animation_.phase() == zmouse::overlay::AnimationPhase::visible && auto_timeout_enabled_ &&
            (now - last_cursor_move_at_ >= overlay_idle_timeout_ms_ ||
             now - overlay_started_at_ >= overlay_max_duration_ms_))
        {
            dismiss_overlay();
            return;
        }
        schedule_overlay_timer();
    }

    void stop_magnifier_capture() noexcept
    {
        if (magnifier_capture_ != nullptr)
        {
            magnifier_capture_->stop();
            magnifier_capture_.reset();
        }
        magnifier_monitor_ = nullptr;
        magnifier_retry_after_ = 0;
        capture_exclusion_applied_ = false;
        magnifier_window_.hide();
    }

    void update_magnifier_capture(const zmouse::overlay::Point position) noexcept
    {
        if (!settings_.magnifier.enabled)
        {
            return;
        }
        const ULONGLONG now = GetTickCount64();
        if (now < magnifier_retry_after_)
        {
            return;
        }
        const HMONITOR monitor = MonitorFromPoint({position.x, position.y}, MONITOR_DEFAULTTONEAREST);
        if (monitor == nullptr)
        {
            return;
        }
        if (magnifier_capture_ == nullptr)
        {
            magnifier_capture_ = std::make_unique<zmouse::platform::DesktopDuplicationCapture>();
        }
        if (magnifier_monitor_ != nullptr && magnifier_monitor_ != monitor)
        {
            magnifier_capture_->stop();
            magnifier_monitor_ = nullptr;
        }
        // Exclude every app-owned visual before acquiring a frame. Continuing
        // after a failed exclusion would allow recursive capture of our lens.
        if (!capture_exclusion_applied_)
        {
            capture_exclusion_applied_ = zmouse::platform::exclude_window_from_capture(magnifier_window_.window()) &&
                                         overlay_manager_.exclude_from_capture();
        }
        const bool exclusion_applied = capture_exclusion_applied_;
        magnifier_capture_->mark_exclusion(exclusion_applied);
        if (!exclusion_applied)
        {
            magnifier_capture_->stop();
            magnifier_window_.hide();
            magnifier_retry_after_ = now + 1'000;
            return;
        }
        if (!magnifier_capture_->running() && !magnifier_capture_->start(monitor))
        {
            // Capture failure is a safe degradation: P3 remains visible and the
            // next movement/timer tick may retry after a topology change.
            magnifier_retry_after_ = now + 1'000;
            return;
        }
        magnifier_monitor_ = monitor;
        UINT dpi_x = 96;
        UINT dpi_y = 96;
        static_cast<void>(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y));
        if (!magnifier_window_.render(*magnifier_capture_, {position.x, position.y}, dpi_x))
        {
            const auto failure = magnifier_capture_->diagnostics().last_failure;
            if (failure != zmouse::capture::FailureCategory::timeout &&
                failure != zmouse::capture::FailureCategory::none)
            {
                magnifier_capture_->stop();
                magnifier_monitor_ = nullptr;
                magnifier_retry_after_ = now + 1'000;
            }
        }
        else
        {
            magnifier_retry_after_ = 0;
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

        case WM_HOTKEY:
            if (hotkey_registration_.active() && static_cast<int>(w_param) == hotkey_registration_.identifier())
            {
                double_ctrl_detector_.reset();
                if (zmouse::input::triggers_armed(input_state()) && registered_hotkey_chord_is_clean())
                {
                    request_overlay_activation(zmouse::policy::TriggerSource::custom_hotkey);
                }
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(w_param))
            {
            case command_settings:
                show_settings();
                return 0;
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
            case command_export_diagnostics:
                export_diagnostics();
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
            if (!activation_pending_)
            {
                return 0;
            }
            activation_pending_ = false;
            activate_overlay();
            return 0;

        case WM_TIMER:
            if (w_param == overlay_timer_id)
            {
                on_overlay_timer();
                return 0;
            }
            break;

        case WM_DISPLAYCHANGE:
            stop_magnifier_capture();
            if (!overlay_manager_.rebuild())
            {
                hide_overlay_immediately();
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;

        case WM_DESTROY:
            hide_overlay_immediately();
            hotkey_registration_.reset();
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
        const bool right_control = keyboard.VKey == VK_RCONTROL || (keyboard.VKey == VK_CONTROL && extended);

        const bool overlay_was_visible = overlay_manager_.visible();
        if (zmouse::input::should_dismiss_overlay_for_key_event(overlay_was_visible, pressed, repeated))
        {
            dismiss_overlay();
        }

        bool any_other_key_down = false;
        bool any_other_non_modifier_key_down = false;
        for (std::size_t index = 0; index < key_down_.size(); ++index)
        {
            if (index != key_id && key_down_[index])
            {
                any_other_key_down = true;
                const auto virtual_key = static_cast<UINT>(index % 256U);
                any_other_non_modifier_key_down =
                    any_other_non_modifier_key_down || !is_modifier_virtual_key(virtual_key);
            }
        }

        if (!zmouse::input::triggers_armed(input_state()))
        {
            return;
        }

        if (hotkey_registration_.active())
        {
            const zmouse::input::HotkeyEvent hotkey_event{
                .key = static_cast<std::uint16_t>(keyboard.VKey),
                .pressed = pressed,
                .repeated = repeated,
                .control_down = is_virtual_key_down(VK_CONTROL) || is_virtual_key_down(VK_LCONTROL) ||
                                is_virtual_key_down(VK_RCONTROL),
                .alt_down =
                    is_virtual_key_down(VK_MENU) || is_virtual_key_down(VK_LMENU) || is_virtual_key_down(VK_RMENU),
                .shift_down =
                    is_virtual_key_down(VK_SHIFT) || is_virtual_key_down(VK_LSHIFT) || is_virtual_key_down(VK_RSHIFT),
                .windows_down = is_virtual_key_down(VK_LWIN) || is_virtual_key_down(VK_RWIN),
                .other_key_down = any_other_non_modifier_key_down,
            };
            if (hotkey_detector_.process(hotkey_event))
            {
                double_ctrl_detector_.reset();
                request_overlay_activation(zmouse::policy::TriggerSource::custom_hotkey);
                return;
            }
        }

        const zmouse::input::KeyEvent control_event{
            .key = left_control    ? zmouse::input::KeyKind::left_control
                   : right_control ? zmouse::input::KeyKind::right_control
                                   : zmouse::input::KeyKind::other,
            .pressed = pressed,
            .repeated = repeated,
            .timestamp = GetTickCount64(),
        };
        if (double_ctrl_detector_.process(control_event, any_other_key_down, mouse_buttons_ != 0))
        {
            request_overlay_activation(zmouse::policy::TriggerSource::double_ctrl);
        }
    }

    [[nodiscard]] bool is_virtual_key_down(const UINT virtual_key) const noexcept
    {
        return key_down_[virtual_key] || key_down_[virtual_key + 256U];
    }

    [[nodiscard]] bool registered_hotkey_chord_is_clean() noexcept
    {
        const auto key_is_down = [](const int virtual_key) noexcept
        { return (GetAsyncKeyState(virtual_key) & static_cast<SHORT>(0x8000)) != 0; };

        bool other_key_down = mouse_buttons_ != 0;
        for (int virtual_key = 1; virtual_key < 0xFF && !other_key_down; ++virtual_key)
        {
            if (virtual_key != settings_.hotkey.key && !is_modifier_virtual_key(static_cast<UINT>(virtual_key)) &&
                key_is_down(virtual_key))
            {
                other_key_down = true;
            }
        }

        const zmouse::input::HotkeyEvent event{
            .key = settings_.hotkey.key,
            .pressed = true,
            .repeated = false,
            .control_down = key_is_down(VK_CONTROL) || key_is_down(VK_LCONTROL) || key_is_down(VK_RCONTROL),
            .alt_down = key_is_down(VK_MENU) || key_is_down(VK_LMENU) || key_is_down(VK_RMENU),
            .shift_down = key_is_down(VK_SHIFT) || key_is_down(VK_LSHIFT) || key_is_down(VK_RSHIFT),
            .windows_down = key_is_down(VK_LWIN) || key_is_down(VK_RWIN),
            .other_key_down = other_key_down,
        };
        return hotkey_detector_.process(event);
    }

    [[nodiscard]] static bool is_modifier_virtual_key(const UINT virtual_key) noexcept
    {
        switch (virtual_key)
        {
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_LWIN:
        case VK_RWIN:
            return true;
        default:
            return false;
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
            cursor_update_pending_ = true;
            schedule_overlay_timer();
        }

        if (!zmouse::input::triggers_armed(input_state()) || !shake_enabled_ ||
            (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
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
            request_overlay_activation(zmouse::policy::TriggerSource::mouse_shake);
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
    bool tray_menu_open_{};
    bool settings_dialog_open_{};
    std::bitset<512> key_down_{};
    bool activation_pending_{};
    HANDLE animation_timer_{};
    bool animation_timer_running_{};
    std::uint8_t mouse_buttons_{};
    ULONGLONG overlay_idle_timeout_ms_{1'200};
    ULONGLONG overlay_max_duration_ms_{5'000};
    ULONGLONG overlay_started_at_{};
    ULONGLONG last_cursor_move_at_{};
    zmouse::overlay::Point last_cursor_position_{};
    bool cursor_update_pending_{};
    zmouse::input::DoubleCtrlDetector double_ctrl_detector_{};
    zmouse::input::HotkeyDetector hotkey_detector_{};
    zmouse::platform::GlobalHotkeyRegistration hotkey_registration_{};
    zmouse::input::ShakeDetector shake_detector_{};
    zmouse::overlay::LocatorAnimation locator_animation_{};
    zmouse::platform::OverlayManager overlay_manager_{};
    zmouse::platform::MagnifierWindow magnifier_window_{};
    std::unique_ptr<zmouse::platform::DesktopDuplicationCapture> magnifier_capture_{};
    HMONITOR magnifier_monitor_{};
    ULONGLONG magnifier_retry_after_{};
    bool capture_exclusion_applied_{};
};
} // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    static_cast<void>(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

    if (has_command_line_switch(L"--simulate-displays"))
    {
        return zmouse::platform::run_display_simulator(instance);
    }

    HANDLE instance_mutex = CreateMutexW(nullptr, TRUE, instance_mutex_name);
    if (instance_mutex == nullptr)
    {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (const HWND existing_window = FindWindowW(window_class_name, window_title); existing_window != nullptr)
        {
            if (has_command_line_switch(L"--settings"))
            {
                static_cast<void>(SetForegroundWindow(existing_window));
                static_cast<void>(PostMessageW(existing_window, WM_COMMAND, command_settings, 0));
            }
            if (has_command_line_switch(L"--diagnostics"))
            {
                static_cast<void>(PostMessageW(existing_window, WM_COMMAND, command_export_diagnostics, 0));
            }
        }
        CloseHandle(instance_mutex);
        return 0;
    }

    Application application(instance, resolve_config_path());
    const int result = application.run();
    static_cast<void>(ReleaseMutex(instance_mutex));
    static_cast<void>(CloseHandle(instance_mutex));
    return result;
}

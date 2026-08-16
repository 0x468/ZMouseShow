#include "settings_dialog.hpp"

#include "../../resources/resource.h"
#include <algorithm>
#include <commctrl.h>
#include <cstdint>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zmouse::platform
{
namespace
{
[[nodiscard]] std::wstring wide_from_utf8(const std::string_view value)
{
    if (value.empty())
    {
        return {};
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
    {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                            size) != size)
    {
        return {};
    }
    return result;
}

[[nodiscard]] std::optional<std::string> utf8_from_wide(const std::wstring_view value) noexcept
{
    try
    {
        if (value.empty())
        {
            return std::string{};
        }
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return std::nullopt;
        }
        std::string result(static_cast<std::size_t>(size), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                result.data(), size, nullptr, nullptr) != size)
        {
            return std::nullopt;
        }
        return result;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

[[nodiscard]] std::wstring format_excluded_processes(const std::vector<std::string>& processes)
{
    std::wstring result;
    for (std::size_t index = 0; index < processes.size(); ++index)
    {
        if (index != 0)
        {
            result += L"; ";
        }
        result += wide_from_utf8(processes[index]);
    }
    return result;
}

[[nodiscard]] std::optional<std::vector<std::string>> read_excluded_processes(const HWND dialog) noexcept
{
    try
    {
        const HWND edit = GetDlgItem(dialog, IDC_EXCLUDED_PROCESSES);
        const int length = GetWindowTextLengthW(edit);
        if (length < 0 || length > 16'384)
        {
            return std::nullopt;
        }
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        const int copied = GetWindowTextW(edit, text.data(), static_cast<int>(text.size()));
        if (copied < 0)
        {
            return std::nullopt;
        }
        text.resize(static_cast<std::size_t>(copied));

        std::vector<std::string> result;
        std::size_t start = 0;
        while (start <= text.size())
        {
            const auto end = text.find_first_of(L";\r\n", start);
            const auto token =
                std::wstring_view(text).substr(start, end == std::wstring::npos ? text.size() - start : end - start);
            const auto utf8 = utf8_from_wide(token);
            if (!utf8)
            {
                return std::nullopt;
            }
            if (const auto normalized = policy::normalize_executable_name(*utf8))
            {
                if (std::ranges::find(result, *normalized) == result.end())
                {
                    result.push_back(*normalized);
                }
                if (result.size() > 64)
                {
                    return std::nullopt;
                }
            }
            else if (token.find_first_not_of(L" \t") != std::wstring_view::npos)
            {
                return std::nullopt;
            }
            if (end == std::wstring::npos)
            {
                break;
            }
            start = end + 1;
        }
        return result;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

[[nodiscard]] std::wstring hotkey_name(const input::HotkeyConfig& hotkey)
{
    std::wstring name;
    if (hotkey.control)
    {
        name += L"Ctrl+";
    }
    if (hotkey.alt)
    {
        name += L"Alt+";
    }
    if (hotkey.shift)
    {
        name += L"Shift+";
    }
    if (hotkey.windows)
    {
        name += L"Win+";
    }
    if ((hotkey.key >= 'A' && hotkey.key <= 'Z') || (hotkey.key >= '0' && hotkey.key <= '9'))
    {
        name.push_back(static_cast<wchar_t>(hotkey.key));
    }
    else if (hotkey.key >= VK_F1 && hotkey.key <= VK_F24)
    {
        name += L'F' + std::to_wstring(hotkey.key - VK_F1 + 1U);
    }
    else
    {
        name += L"未支持按键";
    }
    return name;
}

[[nodiscard]] bool same_hotkey(const input::HotkeyConfig& left, const input::HotkeyConfig& right) noexcept
{
    return left.enabled == right.enabled && left.key == right.key && left.control == right.control &&
           left.alt == right.alt && left.shift == right.shift && left.windows == right.windows;
}

[[nodiscard]] const wchar_t* hotkey_issue_message(const input::HotkeyIssue issue) noexcept
{
    switch (issue)
    {
    case input::HotkeyIssue::unsupported_key:
        return L"组合键主键必须是 A–Z、0–9 或 F1–F24。";
    case input::HotkeyIssue::insufficient_modifiers:
        return L"字母或数字至少需要两个修饰键；功能键至少需要一个修饰键。";
    case input::HotkeyIssue::reserved_system_shortcut:
        return L"该组合键属于 Windows 保留快捷键，不能使用。";
    case input::HotkeyIssue::common_application_shortcut:
        return L"该组合键常用于其它应用，可能会同时触发。";
    case input::HotkeyIssue::none:
    case input::HotkeyIssue::disabled:
        return L"";
    }
    return L"组合键不可用。";
}

[[nodiscard]] bool probe_hotkey_registration(const HWND window, const input::HotkeyConfig& hotkey) noexcept
{
    constexpr int probe_id = 0x5A10;
    UINT modifiers = MOD_NOREPEAT;
    modifiers |= hotkey.control ? MOD_CONTROL : 0U;
    modifiers |= hotkey.alt ? MOD_ALT : 0U;
    modifiers |= hotkey.shift ? MOD_SHIFT : 0U;
    modifiers |= hotkey.windows ? MOD_WIN : 0U;
    if (RegisterHotKey(window, probe_id, modifiers, hotkey.key) == FALSE)
    {
        return false;
    }
    static_cast<void>(UnregisterHotKey(window, probe_id));
    return true;
}

class SettingsDialog final
{
  public:
    SettingsDialog(config::Settings settings, const ApplySettingsCallback apply, void* context)
        : applied_(settings), draft_(settings), apply_(apply), context_(context)
    {
    }

    [[nodiscard]] INT_PTR show(const HINSTANCE instance, const HWND owner) noexcept
    {
        return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, dialog_proc,
                               reinterpret_cast<LPARAM>(this));
    }

  private:
    static INT_PTR CALLBACK dialog_proc(const HWND dialog, const UINT message, const WPARAM w_param,
                                        const LPARAM l_param) noexcept
    {
        SettingsDialog* self = nullptr;
        if (message == WM_INITDIALOG)
        {
            self = reinterpret_cast<SettingsDialog*>(l_param);
            self->dialog_ = dialog;
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
        }
        if (self == nullptr)
        {
            return FALSE;
        }

        try
        {
            return self->handle_message(message, w_param);
        }
        catch (...)
        {
            MessageBoxW(dialog, L"设置窗口发生错误，未保存更改。", L"ZMouseShow", MB_OK | MB_ICONERROR);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }

    INT_PTR handle_message(const UINT message, const WPARAM w_param)
    {
        switch (message)
        {
        case WM_INITDIALOG:
            initialize_controls();
            return TRUE;
        case WM_COMMAND:
            return handle_command(LOWORD(w_param), HIWORD(w_param));
        default:
            return FALSE;
        }
    }

    void initialize_controls()
    {
        initializing_ = true;
        static_cast<void>(
            SendDlgItemMessageW(dialog_, IDC_CTRL_SIDE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"左 Ctrl（默认）")));
        static_cast<void>(
            SendDlgItemMessageW(dialog_, IDC_CTRL_SIDE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"右 Ctrl")));
        static_cast<void>(
            SendDlgItemMessageW(dialog_, IDC_CTRL_SIDE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"任一侧 Ctrl")));
        static_cast<void>(
            SendDlgItemMessageW(dialog_, IDC_SPOTLIGHT_SHAPE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"圆形")));
        static_cast<void>(
            SendDlgItemMessageW(dialog_, IDC_SPOTLIGHT_SHAPE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"圆角方形")));
        static_cast<void>(
            SendDlgItemMessageW(dialog_, IDC_SPOTLIGHT_SHAPE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"菱形")));
        static_cast<void>(SendDlgItemMessageW(dialog_, IDC_FULLSCREEN_SUPPRESSION, CB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(L"自动（仅抑制晃动）")));
        static_cast<void>(SendDlgItemMessageW(dialog_, IDC_FULLSCREEN_SUPPRESSION, CB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(L"关闭")));
        static_cast<void>(SendDlgItemMessageW(dialog_, IDC_FULLSCREEN_SUPPRESSION, CB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(L"严格（抑制全部）")));

        int side = 0;
        if (draft_.double_ctrl.side == input::ControlSide::right)
        {
            side = 1;
        }
        else if (draft_.double_ctrl.side == input::ControlSide::either)
        {
            side = 2;
        }
        static_cast<void>(SendDlgItemMessageW(dialog_, IDC_CTRL_SIDE, CB_SETCURSEL, side, 0));

        int shape = 0;
        if (draft_.spotlight_shape == overlay::SpotlightShape::rounded_square)
        {
            shape = 1;
        }
        else if (draft_.spotlight_shape == overlay::SpotlightShape::diamond)
        {
            shape = 2;
        }
        static_cast<void>(SendDlgItemMessageW(dialog_, IDC_SPOTLIGHT_SHAPE, CB_SETCURSEL, shape, 0));
        int fullscreen_suppression = 0;
        if (draft_.activation_policy.fullscreen_suppression == policy::FullscreenSuppression::off)
        {
            fullscreen_suppression = 1;
        }
        else if (draft_.activation_policy.fullscreen_suppression == policy::FullscreenSuppression::strict)
        {
            fullscreen_suppression = 2;
        }
        static_cast<void>(
            SendDlgItemMessageW(dialog_, IDC_FULLSCREEN_SUPPRESSION, CB_SETCURSEL, fullscreen_suppression, 0));
        CheckDlgButton(dialog_, IDC_SHAKE_ENABLED, draft_.shake_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog_, IDC_DOUBLE_CTRL_ENABLED, draft_.double_ctrl.enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog_, IDC_HOTKEY_ENABLED, draft_.hotkey.enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog_, IDC_AUTO_TIMEOUT_ENABLED, draft_.auto_timeout_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog_, IDC_FOCUS_RING_ENABLED,
                       draft_.effects.focus_ring_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog_, IDC_RIPPLE_ENABLED, draft_.effects.ripple_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog_, IDC_CROSSHAIR_ENABLED, draft_.effects.crosshair_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog_, IDC_ENLARGED_CURSOR_ENABLED,
                       draft_.effects.enlarged_cursor_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog_, IDC_STARTUP_ENABLED, draft_.startup_enabled ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(dialog_, IDC_CTRL_SIDE), draft_.double_ctrl.enabled ? TRUE : FALSE);
        EnableWindow(GetDlgItem(dialog_, IDC_CURSOR_SCALE_PERCENT),
                     draft_.effects.enlarged_cursor_enabled ? TRUE : FALSE);
        SetDlgItemInt(dialog_, IDC_RADIUS_DIP, static_cast<UINT>(draft_.spotlight_radius_dip), FALSE);
        SetDlgItemInt(dialog_, IDC_DIM_OPACITY, draft_.dim_opacity_percent, FALSE);
        SetDlgItemInt(dialog_, IDC_CURSOR_SCALE_PERCENT, draft_.effects.cursor_scale_percent, FALSE);
        const auto excluded_processes = format_excluded_processes(draft_.activation_policy.excluded_processes);
        SetDlgItemTextW(dialog_, IDC_EXCLUDED_PROCESSES, excluded_processes.c_str());

        set_hotkey_controls(draft_.hotkey);
        set_hotkey_controls_enabled(draft_.hotkey.enabled);
        update_hotkey_summary();
        EnableWindow(GetDlgItem(dialog_, IDC_APPLY_SETTINGS), FALSE);
        initializing_ = false;
        return;
    }

    INT_PTR handle_command(const WORD control, const WORD notification)
    {
        if (control == IDOK)
        {
            if (apply_changes())
            {
                EndDialog(dialog_, IDOK);
            }
            return TRUE;
        }
        if (control == IDCANCEL)
        {
            EndDialog(dialog_, IDCANCEL);
            return TRUE;
        }
        if (control == IDC_APPLY_SETTINGS && notification == BN_CLICKED)
        {
            static_cast<void>(apply_changes());
            return TRUE;
        }
        if (control == IDC_HOTKEY_RESET && notification == BN_CLICKED)
        {
            auto defaults = input::HotkeyConfig{};
            defaults.enabled = IsDlgButtonChecked(dialog_, IDC_HOTKEY_ENABLED) == BST_CHECKED;
            set_hotkey_controls(defaults);
            update_hotkey_summary();
            dirty_ = true;
            EnableWindow(GetDlgItem(dialog_, IDC_APPLY_SETTINGS), TRUE);
            return TRUE;
        }

        if (control == IDC_DOUBLE_CTRL_ENABLED && notification == BN_CLICKED)
        {
            const bool enabled = IsDlgButtonChecked(dialog_, IDC_DOUBLE_CTRL_ENABLED) == BST_CHECKED;
            EnableWindow(GetDlgItem(dialog_, IDC_CTRL_SIDE), enabled ? TRUE : FALSE);
        }
        if (control == IDC_HOTKEY_ENABLED && notification == BN_CLICKED)
        {
            const bool enabled = IsDlgButtonChecked(dialog_, IDC_HOTKEY_ENABLED) == BST_CHECKED;
            set_hotkey_controls_enabled(enabled);
            update_hotkey_summary();
        }
        if (control == IDC_ENLARGED_CURSOR_ENABLED && notification == BN_CLICKED)
        {
            const bool enabled = IsDlgButtonChecked(dialog_, IDC_ENLARGED_CURSOR_ENABLED) == BST_CHECKED;
            EnableWindow(GetDlgItem(dialog_, IDC_CURSOR_SCALE_PERCENT), enabled ? TRUE : FALSE);
        }
        if ((control == IDC_HOTKEY_CAPTURE && notification == EN_CHANGE) ||
            (control == IDC_HOTKEY_WINDOWS && notification == BN_CLICKED))
        {
            update_hotkey_summary();
        }

        const bool checkbox_changed =
            notification == BN_CLICKED &&
            (control == IDC_SHAKE_ENABLED || control == IDC_DOUBLE_CTRL_ENABLED || control == IDC_HOTKEY_ENABLED ||
             control == IDC_HOTKEY_WINDOWS || control == IDC_AUTO_TIMEOUT_ENABLED ||
             control == IDC_FOCUS_RING_ENABLED || control == IDC_RIPPLE_ENABLED || control == IDC_CROSSHAIR_ENABLED ||
             control == IDC_ENLARGED_CURSOR_ENABLED || control == IDC_STARTUP_ENABLED);
        const bool combo_changed =
            (control == IDC_CTRL_SIDE || control == IDC_SPOTLIGHT_SHAPE || control == IDC_FULLSCREEN_SUPPRESSION) &&
            notification == CBN_SELCHANGE;
        const bool edit_changed =
            notification == EN_CHANGE &&
            (control == IDC_RADIUS_DIP || control == IDC_DIM_OPACITY || control == IDC_HOTKEY_CAPTURE ||
             control == IDC_CURSOR_SCALE_PERCENT || control == IDC_EXCLUDED_PROCESSES);
        if (!initializing_ && (checkbox_changed || combo_changed || edit_changed))
        {
            dirty_ = true;
            EnableWindow(GetDlgItem(dialog_, IDC_APPLY_SETTINGS), TRUE);
        }
        return FALSE;
    }

    [[nodiscard]] bool read_controls(config::Settings& settings) const noexcept
    {
        BOOL radius_valid = FALSE;
        const UINT radius = GetDlgItemInt(dialog_, IDC_RADIUS_DIP, &radius_valid, FALSE);
        if (radius_valid == FALSE || radius < 32 || radius > 512)
        {
            MessageBoxW(dialog_, L"高亮半径必须是 32–512 DIP。", L"ZMouseShow 设置", MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(dialog_, IDC_RADIUS_DIP));
            return false;
        }

        BOOL opacity_valid = FALSE;
        const UINT opacity = GetDlgItemInt(dialog_, IDC_DIM_OPACITY, &opacity_valid, FALSE);
        if (opacity_valid == FALSE || opacity < 10 || opacity > 90)
        {
            MessageBoxW(dialog_, L"暗化强度必须是 10–90%。", L"ZMouseShow 设置", MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(dialog_, IDC_DIM_OPACITY));
            return false;
        }

        BOOL cursor_scale_valid = FALSE;
        const UINT cursor_scale = GetDlgItemInt(dialog_, IDC_CURSOR_SCALE_PERCENT, &cursor_scale_valid, FALSE);
        if (cursor_scale_valid == FALSE || cursor_scale < 125 || cursor_scale > 400)
        {
            MessageBoxW(dialog_, L"放大光标比例必须是 125–400%。", L"ZMouseShow 设置", MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(dialog_, IDC_CURSOR_SCALE_PERCENT));
            return false;
        }

        const LRESULT side = SendDlgItemMessageW(dialog_, IDC_CTRL_SIDE, CB_GETCURSEL, 0, 0);
        const LRESULT shape = SendDlgItemMessageW(dialog_, IDC_SPOTLIGHT_SHAPE, CB_GETCURSEL, 0, 0);
        const LRESULT fullscreen_suppression =
            SendDlgItemMessageW(dialog_, IDC_FULLSCREEN_SUPPRESSION, CB_GETCURSEL, 0, 0);
        if (side < 0 || side > 2 || shape < 0 || shape > 2 || fullscreen_suppression < 0 || fullscreen_suppression > 2)
        {
            return false;
        }

        const auto excluded_processes = read_excluded_processes(dialog_);
        if (!excluded_processes)
        {
            MessageBoxW(dialog_, L"排除程序只能填写 EXE 文件名，最多 64 个，并用分号分隔。", L"ZMouseShow 设置",
                        MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(dialog_, IDC_EXCLUDED_PROCESSES));
            return false;
        }

        settings = applied_;
        settings.shake_enabled = IsDlgButtonChecked(dialog_, IDC_SHAKE_ENABLED) == BST_CHECKED;
        settings.double_ctrl.enabled = IsDlgButtonChecked(dialog_, IDC_DOUBLE_CTRL_ENABLED) == BST_CHECKED;
        settings.hotkey = hotkey_from_controls();
        const auto hotkey_validation = input::validate_hotkey_config(settings.hotkey);
        if (!hotkey_validation.accepted)
        {
            MessageBoxW(dialog_, hotkey_issue_message(hotkey_validation.issue), L"ZMouseShow 设置",
                        MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(dialog_, IDC_HOTKEY_CAPTURE));
            return false;
        }
        settings.auto_timeout_enabled = IsDlgButtonChecked(dialog_, IDC_AUTO_TIMEOUT_ENABLED) == BST_CHECKED;
        settings.spotlight_radius_dip = static_cast<std::int32_t>(radius);
        settings.spotlight_shape = shape == 0   ? overlay::SpotlightShape::circle
                                   : shape == 1 ? overlay::SpotlightShape::rounded_square
                                                : overlay::SpotlightShape::diamond;
        settings.dim_opacity_percent = opacity;
        settings.effects.focus_ring_enabled = IsDlgButtonChecked(dialog_, IDC_FOCUS_RING_ENABLED) == BST_CHECKED;
        settings.effects.ripple_enabled = IsDlgButtonChecked(dialog_, IDC_RIPPLE_ENABLED) == BST_CHECKED;
        settings.effects.crosshair_enabled = IsDlgButtonChecked(dialog_, IDC_CROSSHAIR_ENABLED) == BST_CHECKED;
        settings.effects.enlarged_cursor_enabled =
            IsDlgButtonChecked(dialog_, IDC_ENLARGED_CURSOR_ENABLED) == BST_CHECKED;
        settings.effects.cursor_scale_percent = cursor_scale;
        settings.activation_policy.fullscreen_suppression =
            fullscreen_suppression == 0   ? policy::FullscreenSuppression::automatic
            : fullscreen_suppression == 1 ? policy::FullscreenSuppression::off
                                          : policy::FullscreenSuppression::strict;
        settings.activation_policy.excluded_processes = *excluded_processes;
        settings.startup_enabled = IsDlgButtonChecked(dialog_, IDC_STARTUP_ENABLED) == BST_CHECKED;
        settings.double_ctrl.side = side == 0   ? input::ControlSide::left
                                    : side == 1 ? input::ControlSide::right
                                                : input::ControlSide::either;
        return true;
    }

    [[nodiscard]] bool apply_changes()
    {
        if (!dirty_)
        {
            return true;
        }
        if (!read_controls(draft_))
        {
            return false;
        }
        if (!validate_hotkey_change(draft_.hotkey))
        {
            return false;
        }
        if (apply_ == nullptr || !apply_(context_, draft_))
        {
            MessageBoxW(dialog_, L"无法保存设置。当前配置保持不变。", L"ZMouseShow 设置", MB_OK | MB_ICONERROR);
            return false;
        }
        applied_ = draft_;
        dirty_ = false;
        EnableWindow(GetDlgItem(dialog_, IDC_APPLY_SETTINGS), FALSE);
        return true;
    }

    [[nodiscard]] input::HotkeyConfig hotkey_from_controls() const noexcept
    {
        const auto value = static_cast<WORD>(SendDlgItemMessageW(dialog_, IDC_HOTKEY_CAPTURE, HKM_GETHOTKEY, 0, 0));
        const auto modifiers = HIBYTE(value);
        return {
            .enabled = IsDlgButtonChecked(dialog_, IDC_HOTKEY_ENABLED) == BST_CHECKED,
            .key = LOBYTE(value),
            .control = (modifiers & HOTKEYF_CONTROL) != 0,
            .alt = (modifiers & HOTKEYF_ALT) != 0,
            .shift = (modifiers & HOTKEYF_SHIFT) != 0,
            .windows = IsDlgButtonChecked(dialog_, IDC_HOTKEY_WINDOWS) == BST_CHECKED,
        };
    }

    void set_hotkey_controls(const input::HotkeyConfig& hotkey) const noexcept
    {
        BYTE modifiers = 0;
        modifiers |= hotkey.control ? HOTKEYF_CONTROL : 0;
        modifiers |= hotkey.alt ? HOTKEYF_ALT : 0;
        modifiers |= hotkey.shift ? HOTKEYF_SHIFT : 0;
        const WORD value = MAKEWORD(static_cast<BYTE>(hotkey.key), modifiers);
        static_cast<void>(SendDlgItemMessageW(dialog_, IDC_HOTKEY_CAPTURE, HKM_SETHOTKEY, value, 0));
        CheckDlgButton(dialog_, IDC_HOTKEY_WINDOWS, hotkey.windows ? BST_CHECKED : BST_UNCHECKED);
    }

    void set_hotkey_controls_enabled(const bool enabled) const noexcept
    {
        EnableWindow(GetDlgItem(dialog_, IDC_HOTKEY_CAPTURE), enabled ? TRUE : FALSE);
        EnableWindow(GetDlgItem(dialog_, IDC_HOTKEY_WINDOWS), enabled ? TRUE : FALSE);
    }

    void update_hotkey_summary() const
    {
        const auto hotkey = hotkey_from_controls();
        const auto validation = input::validate_hotkey_config(hotkey);
        std::wstring summary = L"当前：" + hotkey_name(hotkey);
        if (!validation.accepted || validation.requires_confirmation)
        {
            summary += L"；";
            summary += hotkey_issue_message(validation.issue);
        }
        SetDlgItemTextW(dialog_, IDC_HOTKEY_SUMMARY, summary.c_str());
    }

    [[nodiscard]] bool validate_hotkey_change(const input::HotkeyConfig& hotkey) const noexcept
    {
        if (!hotkey.enabled || same_hotkey(hotkey, applied_.hotkey))
        {
            return true;
        }

        const auto validation = input::validate_hotkey_config(hotkey);
        if (validation.requires_confirmation &&
            MessageBoxW(dialog_, L"该组合键常用于其它应用，前台程序可能会同时响应。仍要使用吗？", L"ZMouseShow 设置",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        {
            return false;
        }
        if (!probe_hotkey_registration(dialog_, hotkey))
        {
            MessageBoxW(dialog_, L"该组合键可能已被其它程序注册。已保留原来的快捷键。", L"ZMouseShow 设置",
                        MB_OK | MB_ICONWARNING);
            return false;
        }
        return true;
    }

    HWND dialog_{};
    config::Settings applied_{};
    config::Settings draft_{};
    ApplySettingsCallback apply_{};
    void* context_{};
    bool initializing_{};
    bool dirty_{};
};
} // namespace

bool show_settings_dialog(const HINSTANCE instance, const HWND owner, const config::Settings& settings,
                          const ApplySettingsCallback apply, void* context) noexcept
{
    INITCOMMONCONTROLSEX common_controls{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_WIN95_CLASSES,
    };
    if (InitCommonControlsEx(&common_controls) == FALSE)
    {
        MessageBoxW(owner, L"无法初始化原生设置控件。", L"ZMouseShow", MB_OK | MB_ICONERROR);
        return false;
    }
    try
    {
        SettingsDialog dialog(settings, apply, context);
        SetLastError(ERROR_SUCCESS);
        const INT_PTR result = dialog.show(instance, owner);
        if (result == -1)
        {
            wchar_t message[128]{};
            static_cast<void>(swprintf_s(message, L"无法创建设置窗口（Win32 错误 %lu）。", GetLastError()));
            MessageBoxW(owner, message, L"ZMouseShow", MB_OK | MB_ICONERROR);
            return false;
        }
        return true;
    }
    catch (...)
    {
        MessageBoxW(owner, L"无法分配设置窗口所需的内存。", L"ZMouseShow", MB_OK | MB_ICONERROR);
        return false;
    }
}
} // namespace zmouse::platform

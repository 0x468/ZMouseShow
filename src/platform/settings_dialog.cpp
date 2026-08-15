#include "settings_dialog.hpp"

#include "../../resources/resource.h"
#include <cstdint>
#include <cwchar>
#include <string>

namespace zmouse::platform
{
namespace
{
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

class SettingsDialog final
{
  public:
    SettingsDialog(config::Settings settings, const ApplySettingsCallback apply, void* context) noexcept
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
        CheckDlgButton(dialog_, IDC_SHAKE_ENABLED, draft_.shake_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog_, IDC_HOTKEY_ENABLED, draft_.hotkey.enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog_, IDC_AUTO_TIMEOUT_ENABLED, draft_.auto_timeout_enabled ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(dialog_, IDC_RADIUS_DIP, static_cast<UINT>(draft_.spotlight_radius_dip), FALSE);
        SetDlgItemInt(dialog_, IDC_DIM_OPACITY, draft_.dim_opacity_percent, FALSE);

        const auto summary = L"当前组合键：" + hotkey_name(draft_.hotkey) + L"（内容在 TOML 中配置）";
        SetDlgItemTextW(dialog_, IDC_HOTKEY_SUMMARY, summary.c_str());
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

        const bool checkbox_changed =
            notification == BN_CLICKED &&
            (control == IDC_SHAKE_ENABLED || control == IDC_HOTKEY_ENABLED || control == IDC_AUTO_TIMEOUT_ENABLED);
        const bool combo_changed = control == IDC_CTRL_SIDE && notification == CBN_SELCHANGE;
        const bool edit_changed =
            notification == EN_CHANGE && (control == IDC_RADIUS_DIP || control == IDC_DIM_OPACITY);
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
            MessageBoxW(dialog_, L"圆孔半径必须是 32–512 DIP。", L"ZMouseShow 设置", MB_OK | MB_ICONWARNING);
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

        const LRESULT side = SendDlgItemMessageW(dialog_, IDC_CTRL_SIDE, CB_GETCURSEL, 0, 0);
        if (side < 0 || side > 2)
        {
            return false;
        }

        settings = applied_;
        settings.shake_enabled = IsDlgButtonChecked(dialog_, IDC_SHAKE_ENABLED) == BST_CHECKED;
        settings.hotkey.enabled = IsDlgButtonChecked(dialog_, IDC_HOTKEY_ENABLED) == BST_CHECKED;
        settings.auto_timeout_enabled = IsDlgButtonChecked(dialog_, IDC_AUTO_TIMEOUT_ENABLED) == BST_CHECKED;
        settings.spotlight_radius_dip = static_cast<std::int32_t>(radius);
        settings.dim_opacity_percent = opacity;
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
} // namespace zmouse::platform

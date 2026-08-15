#pragma once

#include <windows.h>

#include "zmouse/config/settings.hpp"

namespace zmouse::platform
{
using ApplySettingsCallback = bool (*)(void* context, const config::Settings& settings) noexcept;

[[nodiscard]] bool show_settings_dialog(HINSTANCE instance, HWND owner, const config::Settings& settings,
                                        ApplySettingsCallback apply, void* context) noexcept;
} // namespace zmouse::platform

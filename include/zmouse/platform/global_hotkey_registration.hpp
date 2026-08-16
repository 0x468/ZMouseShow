#pragma once

#include <windows.h>

#include "zmouse/input/hotkey_detector.hpp"

namespace zmouse::platform
{
class GlobalHotkeyRegistration final
{
  public:
    GlobalHotkeyRegistration() noexcept = default;
    ~GlobalHotkeyRegistration();

    GlobalHotkeyRegistration(const GlobalHotkeyRegistration&) = delete;
    GlobalHotkeyRegistration& operator=(const GlobalHotkeyRegistration&) = delete;

    GlobalHotkeyRegistration(GlobalHotkeyRegistration&& other) noexcept;
    GlobalHotkeyRegistration& operator=(GlobalHotkeyRegistration&& other) noexcept;

    [[nodiscard]] bool acquire(HWND window, int identifier, const input::HotkeyConfig& config) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] int identifier() const noexcept;
    [[nodiscard]] bool matches(const input::HotkeyConfig& config) const noexcept;

  private:
    HWND window_{};
    int identifier_{};
    input::HotkeyConfig config_{.enabled = false};
};
} // namespace zmouse::platform

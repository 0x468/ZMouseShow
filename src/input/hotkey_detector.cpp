#include "zmouse/input/hotkey_detector.hpp"

namespace zmouse::input
{
HotkeyDetector::HotkeyDetector(const HotkeyConfig config) noexcept : config_(config) {}

void HotkeyDetector::configure(const HotkeyConfig config) noexcept
{
    config_ = config;
}

bool HotkeyDetector::process(const HotkeyEvent& event) const noexcept
{
    return config_.enabled && event.pressed && !event.repeated && event.key == config_.key &&
           event.control_down == config_.control && event.alt_down == config_.alt &&
           event.shift_down == config_.shift && event.windows_down == config_.windows && !event.other_key_down;
}
} // namespace zmouse::input

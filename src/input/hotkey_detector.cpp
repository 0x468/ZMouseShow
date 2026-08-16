#include "zmouse/input/hotkey_detector.hpp"

namespace zmouse::input
{
namespace
{
[[nodiscard]] bool supported_key(const std::uint16_t key) noexcept
{
    return (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || (key >= 0x70U && key <= 0x87U);
}

[[nodiscard]] bool reserved_windows_key(const std::uint16_t key) noexcept
{
    switch (key)
    {
    case 'D':
    case 'E':
    case 'I':
    case 'L':
    case 'R':
    case 'X':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool common_application_key(const std::uint16_t key) noexcept
{
    switch (key)
    {
    case 'A':
    case 'C':
    case 'F':
    case 'N':
    case 'O':
    case 'P':
    case 'R':
    case 'S':
    case 'T':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
        return true;
    default:
        return false;
    }
}
} // namespace

HotkeyValidation validate_hotkey_config(const HotkeyConfig& config) noexcept
{
    if (!config.enabled)
    {
        return {.accepted = true, .requires_confirmation = false, .issue = HotkeyIssue::disabled};
    }
    if (!supported_key(config.key))
    {
        return {.accepted = false, .requires_confirmation = false, .issue = HotkeyIssue::unsupported_key};
    }
    if (config.key == 0x7BU)
    {
        return {.accepted = false, .requires_confirmation = false, .issue = HotkeyIssue::reserved_system_shortcut};
    }

    const auto modifier_count = static_cast<unsigned>(config.control) + static_cast<unsigned>(config.alt) +
                                static_cast<unsigned>(config.shift) + static_cast<unsigned>(config.windows);
    const bool function_key = config.key >= 0x70U && config.key <= 0x87U;
    if ((function_key && modifier_count < 1U) || (!function_key && modifier_count < 2U))
    {
        return {.accepted = false, .requires_confirmation = false, .issue = HotkeyIssue::insufficient_modifiers};
    }
    if ((config.alt && !config.control && !config.shift && !config.windows && config.key == 0x73U) ||
        (config.windows && reserved_windows_key(config.key)))
    {
        return {.accepted = false, .requires_confirmation = false, .issue = HotkeyIssue::reserved_system_shortcut};
    }
    if (config.control && !config.alt && !config.windows && common_application_key(config.key))
    {
        return {.accepted = true, .requires_confirmation = true, .issue = HotkeyIssue::common_application_shortcut};
    }
    return {.accepted = true, .requires_confirmation = false, .issue = HotkeyIssue::none};
}

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

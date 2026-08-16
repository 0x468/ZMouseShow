#pragma once

#include <cstdint>

namespace zmouse::input
{
struct HotkeyConfig
{
    bool enabled{};
    std::uint16_t key{0x7AU};
    bool control{true};
    bool alt{true};
    bool shift{};
    bool windows{};
};

enum class HotkeyIssue : std::uint8_t
{
    none,
    disabled,
    unsupported_key,
    insufficient_modifiers,
    reserved_system_shortcut,
    common_application_shortcut,
};

struct HotkeyValidation
{
    bool accepted{};
    bool requires_confirmation{};
    HotkeyIssue issue{HotkeyIssue::none};
};

[[nodiscard]] HotkeyValidation validate_hotkey_config(const HotkeyConfig& config) noexcept;

struct HotkeyEvent
{
    std::uint16_t key{};
    bool pressed{};
    bool repeated{};
    bool control_down{};
    bool alt_down{};
    bool shift_down{};
    bool windows_down{};
    bool other_key_down{};
};

class HotkeyDetector final
{
  public:
    HotkeyDetector() = default;
    explicit HotkeyDetector(HotkeyConfig config) noexcept;

    void configure(HotkeyConfig config) noexcept;
    [[nodiscard]] bool process(const HotkeyEvent& event) const noexcept;

  private:
    HotkeyConfig config_{};
};
} // namespace zmouse::input

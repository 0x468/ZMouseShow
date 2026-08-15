#pragma once

#include <cstdint>

namespace zmouse::input
{
struct HotkeyConfig
{
    bool enabled{};
    std::uint16_t key{0x7BU};
    bool control{true};
    bool alt{true};
    bool shift{};
    bool windows{};
};

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

#pragma once

#include <cstdint>

namespace zmouse::input
{
using TimestampMs = std::uint64_t;

enum class KeyKind
{
    left_control,
    other,
};

struct KeyEvent
{
    KeyKind key{KeyKind::other};
    bool pressed{};
    bool repeated{};
    TimestampMs timestamp{};
};

struct DoubleCtrlConfig
{
    TimestampMs minimum_interval_ms{100};
    TimestampMs maximum_interval_ms{500};
    TimestampMs cooldown_ms{500};
};

class DoubleCtrlDetector final
{
  public:
    explicit DoubleCtrlDetector(DoubleCtrlConfig config = {});

    [[nodiscard]] bool process(const KeyEvent& event, bool any_other_key_down, bool any_mouse_button_down);
    void on_mouse_button_event() noexcept;
    void reset() noexcept;

  private:
    enum class State
    {
        idle,
        first_down,
        first_up,
        triggered_awaiting_release,
    };

    void begin_candidate(TimestampMs timestamp) noexcept;
    void cancel_candidate() noexcept;

    DoubleCtrlConfig config_;
    State state_{State::idle};
    TimestampMs first_down_at_{};
    TimestampMs cooldown_until_{};
};
} // namespace zmouse::input

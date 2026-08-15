#pragma once

#include "zmouse/input/double_ctrl_detector.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>

namespace zmouse::input
{
struct RelativeMovement
{
    std::int32_t dx{};
    std::int32_t dy{};
    TimestampMs timestamp{};
};

struct ShakeConfig
{
    TimestampMs interval_ms{1000};
    double minimum_distance{1000.0};
    double minimum_path_to_diagonal_ratio{4.0};
    std::size_t minimum_reversals{3};
    TimestampMs cooldown_ms{800};
};

class ShakeDetector final
{
  public:
    explicit ShakeDetector(ShakeConfig config = {});

    void configure(ShakeConfig config) noexcept;
    [[nodiscard]] bool process(const RelativeMovement& movement, bool overlay_active);
    void on_mouse_buttons_changed(bool any_button_down) noexcept;
    void reset() noexcept;

  private:
    void clear_history() noexcept;
    void prune(TimestampMs now);
    [[nodiscard]] std::size_t count_reversals() const noexcept;

    ShakeConfig config_;
    std::deque<RelativeMovement> history_;
    bool any_mouse_button_down_{};
    TimestampMs cooldown_until_{};
};
} // namespace zmouse::input

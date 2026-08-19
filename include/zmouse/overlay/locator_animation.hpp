#pragma once

#include <cstdint>

namespace zmouse::overlay
{
using AnimationTimestampMs = std::uint64_t;

enum class AnimationPhase : std::uint8_t
{
    hidden,
    appearing,
    visible,
    disappearing,
};

struct AnimationFrame
{
    double dim_progress{};
    double focus_opacity{};
    double ripple_scale{1.0};
    double ripple_opacity{};
    bool surface_visible{};
    bool needs_more_frames{};
};

class LocatorAnimation final
{
  public:
    void show(AnimationTimestampMs now) noexcept;
    void hide(AnimationTimestampMs now) noexcept;
    void reset() noexcept;

    [[nodiscard]] AnimationFrame frame(AnimationTimestampMs now) noexcept;
    [[nodiscard]] AnimationPhase phase() const noexcept;

  private:
    static constexpr AnimationTimestampMs transition_duration_ms = 220;
    static constexpr double final_ripple_scale = 1.75;

    [[nodiscard]] static double progress(AnimationTimestampMs now, AnimationTimestampMs started_at) noexcept;
    [[nodiscard]] static double ease_out(double value) noexcept;

    AnimationPhase phase_{AnimationPhase::hidden};
    AnimationTimestampMs phase_started_at_{};
    double disappearing_start_dim_{1.0};
    double disappearing_start_focus_opacity_{1.0};
};
} // namespace zmouse::overlay

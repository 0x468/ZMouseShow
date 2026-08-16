#include "zmouse/overlay/locator_animation.hpp"

#include <algorithm>

namespace zmouse::overlay
{
void LocatorAnimation::show(const AnimationTimestampMs now) noexcept
{
    phase_ = AnimationPhase::appearing;
    phase_started_at_ = now;
    disappearing_start_dim_ = 1.0;
    disappearing_start_focus_opacity_ = 1.0;
}

void LocatorAnimation::hide(const AnimationTimestampMs now) noexcept
{
    if (phase_ == AnimationPhase::hidden || phase_ == AnimationPhase::disappearing)
    {
        return;
    }

    const auto current = frame(now);
    disappearing_start_dim_ = current.dim_progress;
    disappearing_start_focus_opacity_ = current.focus_opacity;
    phase_ = AnimationPhase::disappearing;
    phase_started_at_ = now;
}

void LocatorAnimation::reset() noexcept
{
    phase_ = AnimationPhase::hidden;
    phase_started_at_ = 0;
    disappearing_start_dim_ = 1.0;
    disappearing_start_focus_opacity_ = 1.0;
}

AnimationFrame LocatorAnimation::frame(const AnimationTimestampMs now) noexcept
{
    switch (phase_)
    {
    case AnimationPhase::hidden:
        return {};

    case AnimationPhase::appearing:
    {
        const double linear = progress(now, phase_started_at_);
        const double eased = ease_out(linear);
        if (linear >= 1.0)
        {
            phase_ = AnimationPhase::visible;
            return {.dim_progress = 1.0,
                    .focus_opacity = 1.0,
                    .ripple_scale = final_ripple_scale,
                    .ripple_opacity = 0.0,
                    .surface_visible = true,
                    .needs_more_frames = false};
        }
        return {.dim_progress = eased,
                .focus_opacity = eased,
                .ripple_scale = 1.0 + (final_ripple_scale - 1.0) * eased,
                .ripple_opacity = 1.0 - eased,
                .surface_visible = true,
                .needs_more_frames = true};
    }

    case AnimationPhase::visible:
        return {.dim_progress = 1.0,
                .focus_opacity = 1.0,
                .ripple_scale = final_ripple_scale,
                .ripple_opacity = 0.0,
                .surface_visible = true,
                .needs_more_frames = false};

    case AnimationPhase::disappearing:
    {
        const double linear = progress(now, phase_started_at_);
        if (linear >= 1.0)
        {
            reset();
            return {};
        }
        const double remaining = 1.0 - ease_out(linear);
        return {.dim_progress = disappearing_start_dim_ * remaining,
                .focus_opacity = disappearing_start_focus_opacity_ * remaining,
                .ripple_scale = final_ripple_scale,
                .ripple_opacity = 0.0,
                .surface_visible = true,
                .needs_more_frames = true};
    }
    }
    return {};
}

AnimationPhase LocatorAnimation::phase() const noexcept
{
    return phase_;
}

double LocatorAnimation::progress(const AnimationTimestampMs now, const AnimationTimestampMs started_at) noexcept
{
    if (now <= started_at)
    {
        return 0.0;
    }
    return (std::min)(1.0, static_cast<double>(now - started_at) / static_cast<double>(transition_duration_ms));
}

double LocatorAnimation::ease_out(const double value) noexcept
{
    const double remaining = 1.0 - (std::clamp)(value, 0.0, 1.0);
    return 1.0 - remaining * remaining * remaining;
}
} // namespace zmouse::overlay

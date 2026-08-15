#include "zmouse/input/double_ctrl_detector.hpp"

#include <utility>

namespace zmouse::input
{
DoubleCtrlDetector::DoubleCtrlDetector(DoubleCtrlConfig config) : config_(std::move(config)) {}

void DoubleCtrlDetector::configure(const DoubleCtrlConfig config) noexcept
{
    config_ = config;
    reset();
}

bool DoubleCtrlDetector::process(const KeyEvent& event, const bool any_other_key_down, const bool any_mouse_button_down)
{
    if (event.key != KeyKind::left_control)
    {
        cancel_candidate();
        return false;
    }

    if (event.repeated)
    {
        return false;
    }

    if (any_other_key_down || any_mouse_button_down)
    {
        cancel_candidate();
        return false;
    }

    if (event.timestamp < cooldown_until_)
    {
        cancel_candidate();
        return false;
    }

    switch (state_)
    {
    case State::idle:
        if (event.pressed)
        {
            begin_candidate(event.timestamp);
        }
        break;

    case State::first_down:
        if (!event.pressed)
        {
            state_ = State::first_up;
        }
        break;

    case State::first_up:
        if (event.pressed)
        {
            const auto interval = event.timestamp - first_down_at_;
            if (interval >= config_.minimum_interval_ms && interval <= config_.maximum_interval_ms)
            {
                state_ = State::triggered_awaiting_release;
                cooldown_until_ = event.timestamp + config_.cooldown_ms;
                return true;
            }

            begin_candidate(event.timestamp);
        }
        break;

    case State::triggered_awaiting_release:
        if (!event.pressed)
        {
            state_ = State::idle;
        }
        break;
    }

    return false;
}

void DoubleCtrlDetector::on_mouse_button_event() noexcept
{
    cancel_candidate();
}

void DoubleCtrlDetector::reset() noexcept
{
    state_ = State::idle;
    first_down_at_ = 0;
    cooldown_until_ = 0;
}

void DoubleCtrlDetector::begin_candidate(const TimestampMs timestamp) noexcept
{
    state_ = State::first_down;
    first_down_at_ = timestamp;
}

void DoubleCtrlDetector::cancel_candidate() noexcept
{
    state_ = State::idle;
    first_down_at_ = 0;
}
} // namespace zmouse::input

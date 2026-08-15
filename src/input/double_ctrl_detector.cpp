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
    if (!accepts(event.key))
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
            begin_candidate(event.timestamp, event.key);
        }
        break;

    case State::first_down:
        if (event.key != candidate_key_)
        {
            cancel_candidate();
        }
        else if (!event.pressed)
        {
            state_ = State::first_up;
        }
        break;

    case State::first_up:
        if (event.pressed)
        {
            if (event.key != candidate_key_)
            {
                begin_candidate(event.timestamp, event.key);
                break;
            }
            const auto interval = event.timestamp - first_down_at_;
            if (interval >= config_.minimum_interval_ms && interval <= config_.maximum_interval_ms)
            {
                state_ = State::triggered_awaiting_release;
                cooldown_until_ = event.timestamp + config_.cooldown_ms;
                return true;
            }

            begin_candidate(event.timestamp, event.key);
        }
        break;

    case State::triggered_awaiting_release:
        if (event.key == candidate_key_ && !event.pressed)
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
    candidate_key_ = KeyKind::other;
}

bool DoubleCtrlDetector::accepts(const KeyKind key) const noexcept
{
    return (key == KeyKind::left_control &&
            (config_.side == ControlSide::left || config_.side == ControlSide::either)) ||
           (key == KeyKind::right_control &&
            (config_.side == ControlSide::right || config_.side == ControlSide::either));
}

void DoubleCtrlDetector::begin_candidate(const TimestampMs timestamp, const KeyKind key) noexcept
{
    state_ = State::first_down;
    first_down_at_ = timestamp;
    candidate_key_ = key;
}

void DoubleCtrlDetector::cancel_candidate() noexcept
{
    state_ = State::idle;
    first_down_at_ = 0;
    candidate_key_ = KeyKind::other;
}
} // namespace zmouse::input

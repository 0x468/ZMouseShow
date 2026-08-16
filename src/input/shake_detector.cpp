#include "zmouse/input/shake_detector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace zmouse::input
{
namespace
{
constexpr std::array<double, maximum_shake_sensitivity> shake_distances{
    1800.0, 1550.0, 1350.0, 1150.0, 1000.0, 875.0, 775.0, 675.0, 600.0, 525.0,
};

[[nodiscard]] constexpr int sign(const std::int32_t value) noexcept
{
    return (value > 0) - (value < 0);
}
} // namespace

double shake_distance_for_sensitivity(const std::uint32_t sensitivity) noexcept
{
    const auto clamped = (std::clamp)(sensitivity, minimum_shake_sensitivity, maximum_shake_sensitivity);
    return shake_distances[clamped - minimum_shake_sensitivity];
}

std::uint32_t shake_sensitivity_for_distance(const double minimum_distance) noexcept
{
    std::uint32_t closest = default_shake_sensitivity;
    double closest_difference = std::numeric_limits<double>::infinity();
    for (std::uint32_t sensitivity = minimum_shake_sensitivity; sensitivity <= maximum_shake_sensitivity; ++sensitivity)
    {
        const double difference = std::abs(minimum_distance - shake_distance_for_sensitivity(sensitivity));
        if (difference < closest_difference)
        {
            closest = sensitivity;
            closest_difference = difference;
        }
    }
    return closest;
}

ShakeDetector::ShakeDetector(ShakeConfig config) : config_(std::move(config)) {}

void ShakeDetector::configure(const ShakeConfig config) noexcept
{
    config_ = config;
    reset();
}

bool ShakeDetector::process(const RelativeMovement& movement, const bool overlay_active)
{
    if (any_mouse_button_down_ || overlay_active)
    {
        clear_history();
        return false;
    }

    if (movement.timestamp < cooldown_until_)
    {
        clear_history();
        return false;
    }

    if (movement.dx == 0 && movement.dy == 0)
    {
        return false;
    }

    history_.push_back(movement);
    prune(movement.timestamp);

    double path_length = 0.0;
    std::int64_t current_x = 0;
    std::int64_t current_y = 0;
    std::int64_t minimum_x = 0;
    std::int64_t maximum_x = 0;
    std::int64_t minimum_y = 0;
    std::int64_t maximum_y = 0;

    for (const auto& sample : history_)
    {
        current_x += sample.dx;
        current_y += sample.dy;
        minimum_x = (std::min)(minimum_x, current_x);
        maximum_x = (std::max)(maximum_x, current_x);
        minimum_y = (std::min)(minimum_y, current_y);
        maximum_y = (std::max)(maximum_y, current_y);
        path_length += std::hypot(static_cast<double>(sample.dx), static_cast<double>(sample.dy));
    }

    if (path_length < config_.minimum_distance || count_reversals() < config_.minimum_reversals)
    {
        return false;
    }

    const auto width = static_cast<double>(maximum_x - minimum_x);
    const auto height = static_cast<double>(maximum_y - minimum_y);
    const auto diagonal = std::hypot(width, height);
    if (diagonal <= 0.0 || path_length / diagonal < config_.minimum_path_to_diagonal_ratio)
    {
        return false;
    }

    clear_history();
    cooldown_until_ = movement.timestamp + config_.cooldown_ms;
    return true;
}

void ShakeDetector::on_mouse_buttons_changed(const bool any_button_down) noexcept
{
    any_mouse_button_down_ = any_button_down;
    clear_history();
}

void ShakeDetector::reset() noexcept
{
    any_mouse_button_down_ = false;
    cooldown_until_ = 0;
    clear_history();
}

void ShakeDetector::clear_history() noexcept
{
    history_.clear();
}

void ShakeDetector::prune(const TimestampMs now)
{
    const auto earliest = now > config_.interval_ms ? now - config_.interval_ms : 0;
    while (!history_.empty() && history_.front().timestamp < earliest)
    {
        history_.pop_front();
    }
}

std::size_t ShakeDetector::count_reversals() const noexcept
{
    std::size_t reversals = 0;
    int previous_x = 0;
    int previous_y = 0;

    for (const auto& sample : history_)
    {
        const int current_x = sign(sample.dx);
        const int current_y = sign(sample.dy);
        const bool reversed_x = current_x != 0 && previous_x != 0 && current_x != previous_x;
        const bool reversed_y = current_y != 0 && previous_y != 0 && current_y != previous_y;
        if (reversed_x || reversed_y)
        {
            ++reversals;
        }
        if (current_x != 0)
        {
            previous_x = current_x;
        }
        if (current_y != 0)
        {
            previous_y = current_y;
        }
    }

    return reversals;
}
} // namespace zmouse::input

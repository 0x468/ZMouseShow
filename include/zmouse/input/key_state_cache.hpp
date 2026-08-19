#pragma once

#include <bitset>
#include <cstddef>

namespace zmouse::input
{
class KeyStateCache final
{
  public:
    static constexpr std::size_t capacity = 512;

    struct Update final
    {
        bool repeated{};
        bool changed{};
    };

    [[nodiscard]] Update update(const std::size_t key_id, const bool pressed, const bool modifier) noexcept
    {
        if (key_id >= capacity)
        {
            return {};
        }

        const bool was_down = keys_[key_id];
        const bool repeated = pressed && was_down;
        if (was_down == pressed)
        {
            return {.repeated = repeated, .changed = false};
        }

        keys_.set(key_id, pressed);
        auto& count = modifier ? modifier_count_ : non_modifier_count_;
        count += pressed ? 1 : -1;
        if (count < 0)
        {
            count = 0;
        }
        return {.repeated = repeated, .changed = true};
    }

    void reset() noexcept
    {
        keys_.reset();
        modifier_count_ = 0;
        non_modifier_count_ = 0;
    }

    [[nodiscard]] bool is_down(const std::size_t key_id) const noexcept
    {
        return key_id < capacity && keys_[key_id];
    }

    [[nodiscard]] bool any_other_non_modifier(const std::size_t current_key_id,
                                              const bool current_is_modifier) const noexcept
    {
        const int current_contribution = !current_is_modifier && is_down(current_key_id) ? 1 : 0;
        return non_modifier_count_ > current_contribution;
    }

    [[nodiscard]] bool any_other_key(const std::size_t current_key_id, const bool current_is_modifier) const noexcept
    {
        const int current_modifier_contribution = current_is_modifier && is_down(current_key_id) ? 1 : 0;
        return any_other_non_modifier(current_key_id, current_is_modifier) ||
               modifier_count_ > current_modifier_contribution;
    }

    [[nodiscard]] int non_modifier_count() const noexcept
    {
        return non_modifier_count_;
    }

    [[nodiscard]] int modifier_count() const noexcept
    {
        return modifier_count_;
    }

  private:
    std::bitset<capacity> keys_;
    int non_modifier_count_{};
    int modifier_count_{};
};
} // namespace zmouse::input

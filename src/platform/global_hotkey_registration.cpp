#include "zmouse/platform/global_hotkey_registration.hpp"

#include <utility>

namespace zmouse::platform
{
namespace
{
[[nodiscard]] bool same_hotkey(const input::HotkeyConfig& left, const input::HotkeyConfig& right) noexcept
{
    return left.enabled == right.enabled && left.key == right.key && left.control == right.control &&
           left.alt == right.alt && left.shift == right.shift && left.windows == right.windows;
}

[[nodiscard]] UINT modifier_flags(const input::HotkeyConfig& config) noexcept
{
    UINT modifiers = MOD_NOREPEAT;
    modifiers |= config.control ? MOD_CONTROL : 0U;
    modifiers |= config.alt ? MOD_ALT : 0U;
    modifiers |= config.shift ? MOD_SHIFT : 0U;
    modifiers |= config.windows ? MOD_WIN : 0U;
    return modifiers;
}
} // namespace

GlobalHotkeyRegistration::~GlobalHotkeyRegistration()
{
    reset();
}

GlobalHotkeyRegistration::GlobalHotkeyRegistration(GlobalHotkeyRegistration&& other) noexcept
    : window_(std::exchange(other.window_, nullptr)), identifier_(std::exchange(other.identifier_, 0)),
      config_(std::exchange(other.config_, input::HotkeyConfig{.enabled = false}))
{
}

GlobalHotkeyRegistration& GlobalHotkeyRegistration::operator=(GlobalHotkeyRegistration&& other) noexcept
{
    if (this != &other)
    {
        reset();
        window_ = std::exchange(other.window_, nullptr);
        identifier_ = std::exchange(other.identifier_, 0);
        config_ = std::exchange(other.config_, input::HotkeyConfig{.enabled = false});
    }
    return *this;
}

bool GlobalHotkeyRegistration::acquire(HWND window, const int identifier, const input::HotkeyConfig& config) noexcept
{
    reset();
    const auto validation = input::validate_hotkey_config(config);
    if (window == nullptr || identifier <= 0 || !config.enabled || !validation.accepted)
    {
        return false;
    }
    if (RegisterHotKey(window, identifier, modifier_flags(config), config.key) == FALSE)
    {
        return false;
    }

    window_ = window;
    identifier_ = identifier;
    config_ = config;
    return true;
}

void GlobalHotkeyRegistration::reset() noexcept
{
    if (window_ != nullptr && identifier_ > 0)
    {
        static_cast<void>(UnregisterHotKey(window_, identifier_));
    }
    window_ = nullptr;
    identifier_ = 0;
    config_ = {.enabled = false};
}

bool GlobalHotkeyRegistration::active() const noexcept
{
    return window_ != nullptr && identifier_ > 0;
}

int GlobalHotkeyRegistration::identifier() const noexcept
{
    return identifier_;
}

bool GlobalHotkeyRegistration::matches(const input::HotkeyConfig& config) const noexcept
{
    if (!config.enabled)
    {
        return !active();
    }
    return active() && same_hotkey(config_, config);
}
} // namespace zmouse::platform

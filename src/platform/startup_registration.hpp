#pragma once

namespace zmouse::platform
{
[[nodiscard]] bool startup_registration_enabled() noexcept;
[[nodiscard]] bool set_startup_registration_enabled(bool enabled) noexcept;
} // namespace zmouse::platform

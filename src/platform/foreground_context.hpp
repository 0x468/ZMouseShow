#pragma once

#include "zmouse/policy/activation_policy.hpp"

namespace zmouse::platform
{
[[nodiscard]] policy::ForegroundContext query_foreground_context() noexcept;
} // namespace zmouse::platform

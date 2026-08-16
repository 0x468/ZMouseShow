#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zmouse::policy
{
enum class TriggerSource : std::uint8_t
{
    double_ctrl,
    custom_hotkey,
    mouse_shake,
};

enum class FullscreenSuppression : std::uint8_t
{
    off,
    automatic,
    strict,
};

struct ActivationPolicyConfig
{
    FullscreenSuppression fullscreen_suppression{FullscreenSuppression::automatic};
    std::vector<std::string> excluded_processes{};
};

struct ForegroundContext
{
    std::string executable_name{};
    bool fullscreen{};
    bool pointer_confined{};
};

[[nodiscard]] std::optional<std::string> normalize_executable_name(std::string_view value) noexcept;
[[nodiscard]] bool should_allow_activation(const ActivationPolicyConfig& config, TriggerSource source,
                                           const ForegroundContext& foreground) noexcept;
} // namespace zmouse::policy

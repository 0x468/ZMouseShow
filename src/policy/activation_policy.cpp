#include "zmouse/policy/activation_policy.hpp"

#include <algorithm>
#include <cctype>

namespace zmouse::policy
{
std::optional<std::string> normalize_executable_name(const std::string_view value) noexcept
{
    try
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
        {
            return std::nullopt;
        }
        const auto last = value.find_last_not_of(" \t\r\n");
        const auto trimmed = value.substr(first, last - first + 1);
        if (trimmed.size() > 260 || trimmed.find_first_of("\\/:<>\"|?*") != std::string_view::npos ||
            std::ranges::any_of(trimmed, [](const unsigned char character) { return character < 0x20U; }))
        {
            return std::nullopt;
        }

        std::string normalized(trimmed);
        std::ranges::transform(normalized, normalized.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        if (!normalized.ends_with(".exe"))
        {
            return std::nullopt;
        }
        return normalized;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool should_allow_activation(const ActivationPolicyConfig& config, const TriggerSource source,
                             const ForegroundContext& foreground) noexcept
{
    const auto executable = normalize_executable_name(foreground.executable_name);
    if (executable && std::ranges::any_of(config.excluded_processes,
                                          [&](const std::string_view excluded)
                                          {
                                              const auto normalized = normalize_executable_name(excluded);
                                              return normalized && *normalized == *executable;
                                          }))
    {
        return false;
    }

    const bool immersive = foreground.fullscreen || foreground.pointer_confined;
    if (!immersive || config.fullscreen_suppression == FullscreenSuppression::off)
    {
        return true;
    }
    if (config.fullscreen_suppression == FullscreenSuppression::strict)
    {
        return false;
    }
    return source != TriggerSource::mouse_shake;
}
} // namespace zmouse::policy

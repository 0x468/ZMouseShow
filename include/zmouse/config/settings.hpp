#pragma once

#include "zmouse/input/double_ctrl_detector.hpp"
#include "zmouse/input/hotkey_detector.hpp"
#include "zmouse/input/shake_detector.hpp"
#include "zmouse/magnifier/settings.hpp"
#include "zmouse/overlay/spotlight_shape.hpp"
#include "zmouse/overlay/visual_effects.hpp"
#include "zmouse/policy/activation_policy.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace zmouse::config
{
struct Settings
{
    bool shake_enabled{};
    bool auto_timeout_enabled{};

    std::int32_t spotlight_radius_dip{120};
    overlay::SpotlightShape spotlight_shape{overlay::SpotlightShape::circle};
    std::uint32_t dim_opacity_percent{60};
    bool dim_enabled{true};
    std::uint64_t idle_timeout_ms{1'200};
    std::uint64_t maximum_duration_ms{5'000};

    input::DoubleCtrlConfig double_ctrl{};
    input::HotkeyConfig hotkey{};
    input::ShakeConfig shake{};
    overlay::VisualEffects effects{};
    policy::ActivationPolicyConfig activation_policy{};
    bool startup_enabled{};
    magnifier::Settings magnifier{};
};

[[nodiscard]] std::optional<Settings> parse_toml(std::string_view text) noexcept;
[[nodiscard]] std::optional<Settings> load_toml(const std::filesystem::path& path) noexcept;
[[nodiscard]] bool write_default_toml(const std::filesystem::path& path) noexcept;
[[nodiscard]] bool persist_runtime_preferences(const std::filesystem::path& path, bool shake_enabled,
                                               bool auto_timeout_enabled) noexcept;
[[nodiscard]] bool persist_basic_settings(const std::filesystem::path& path, const Settings& settings) noexcept;
[[nodiscard]] std::string_view default_toml_text() noexcept;
} // namespace zmouse::config

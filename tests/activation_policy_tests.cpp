#include "zmouse/policy/activation_policy.hpp"
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void test_executable_normalization()
{
    check(zmouse::policy::normalize_executable_name("  GAME.ExE  ") == "game.exe", "normalizes executable name");
    check(!zmouse::policy::normalize_executable_name("C:\\game.exe"), "rejects paths");
    check(!zmouse::policy::normalize_executable_name("game"), "requires exe suffix");
}

void test_fullscreen_modes()
{
    using zmouse::policy::FullscreenSuppression;
    using zmouse::policy::TriggerSource;
    zmouse::policy::ActivationPolicyConfig config{};
    const zmouse::policy::ForegroundContext fullscreen{.executable_name = "game.exe", .fullscreen = true};

    config.fullscreen_suppression = FullscreenSuppression::off;
    check(zmouse::policy::should_allow_activation(config, TriggerSource::mouse_shake, fullscreen),
          "off mode permits shake");

    config.fullscreen_suppression = FullscreenSuppression::automatic;
    check(!zmouse::policy::should_allow_activation(config, TriggerSource::mouse_shake, fullscreen),
          "automatic mode suppresses shake");
    check(zmouse::policy::should_allow_activation(config, TriggerSource::double_ctrl, fullscreen),
          "automatic mode permits explicit keyboard trigger");
    check(zmouse::policy::should_allow_activation(config, TriggerSource::custom_hotkey, fullscreen),
          "automatic mode permits custom hotkey");

    config.fullscreen_suppression = FullscreenSuppression::strict;
    check(!zmouse::policy::should_allow_activation(config, TriggerSource::double_ctrl, fullscreen),
          "strict mode suppresses keyboard trigger");
}

void test_pointer_confinement_and_exclusions()
{
    using zmouse::policy::TriggerSource;
    zmouse::policy::ActivationPolicyConfig config{};
    const zmouse::policy::ForegroundContext confined{.executable_name = "Editor.exe", .pointer_confined = true};
    check(!zmouse::policy::should_allow_activation(config, TriggerSource::mouse_shake, confined),
          "pointer confinement is immersive for automatic mode");

    config.excluded_processes = {"editor.exe"};
    config.normalized_excluded = {"editor.exe"};
    check(
        !zmouse::policy::should_allow_activation(config, TriggerSource::double_ctrl, {.executable_name = "EDITOR.EXE"}),
        "exclusion is case insensitive");
    check(zmouse::policy::should_allow_activation(config, TriggerSource::double_ctrl, {.executable_name = "other.exe"}),
          "non-excluded window remains allowed");
}
} // namespace

int main()
{
    test_executable_normalization();
    test_fullscreen_modes();
    test_pointer_confinement_and_exclusions();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

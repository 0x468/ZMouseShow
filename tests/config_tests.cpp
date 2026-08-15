#include "zmouse/config/settings.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
int failures = 0;

void check(const bool condition, const std::string_view description)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}

void test_empty_configuration_uses_defaults()
{
    const auto settings = zmouse::config::parse_ini({});
    check(!settings.shake_enabled, "shake is disabled by default");
    check(!settings.auto_timeout_enabled, "automatic timeout is disabled by default");
    check(settings.spotlight_radius_dip == 120, "default spotlight radius is retained");
    check(settings.dim_opacity_percent == 60, "default dim opacity is retained");
}

void test_valid_values_override_defaults()
{
    constexpr std::string_view ini = R"ini(
[GENERAL]
shake_enabled=yes
auto_timeout_enabled=on

[overlay]
radius_dip=180
dim_opacity_percent=72

[timeout]
idle_ms=2500
maximum_duration_ms=12000

[double_ctrl]
minimum_interval_ms=80
maximum_interval_ms=650
cooldown_ms=900

[shake]
interval_ms=1400
minimum_distance=1500.5
minimum_path_to_diagonal_ratio=5.5
minimum_reversals=5
cooldown_ms=1100
)ini";

    const auto settings = zmouse::config::parse_ini(ini);
    check(settings.shake_enabled, "boolean yes is accepted");
    check(settings.auto_timeout_enabled, "boolean on is accepted");
    check(settings.spotlight_radius_dip == 180, "spotlight radius is parsed");
    check(settings.dim_opacity_percent == 72, "dim opacity is parsed");
    check(settings.idle_timeout_ms == 2500, "idle timeout is parsed");
    check(settings.maximum_duration_ms == 12000, "maximum duration is parsed");
    check(settings.double_ctrl.minimum_interval_ms == 80, "minimum Ctrl interval is parsed");
    check(settings.double_ctrl.maximum_interval_ms == 650, "maximum Ctrl interval is parsed");
    check(settings.double_ctrl.cooldown_ms == 900, "Ctrl cooldown is parsed");
    check(settings.shake.interval_ms == 1400, "shake interval is parsed");
    check(std::abs(settings.shake.minimum_distance - 1500.5) < 0.001, "shake distance is parsed");
    check(std::abs(settings.shake.minimum_path_to_diagonal_ratio - 5.5) < 0.001, "shake path ratio is parsed");
    check(settings.shake.minimum_reversals == 5, "shake reversal count is parsed");
    check(settings.shake.cooldown_ms == 1100, "shake cooldown is parsed");
}

void test_invalid_values_are_ignored()
{
    constexpr std::string_view ini = "\xEF\xBB\xBF"
                                     R"ini([general]
shake_enabled=maybe
unknown_key=42

[overlay]
radius_dip=4096
dim_opacity_percent=-1

[timeout]
idle_ms=not-a-number
maximum_duration_ms=9999999

[double_ctrl]
minimum_interval_ms=900
maximum_interval_ms=200

[shake]
minimum_distance=nan
)ini";

    const auto settings = zmouse::config::parse_ini(ini);
    check(!settings.shake_enabled, "invalid boolean uses its default");
    check(settings.spotlight_radius_dip == 120, "out-of-range radius uses its default");
    check(settings.dim_opacity_percent == 60, "out-of-range opacity uses its default");
    check(settings.idle_timeout_ms == 1200, "invalid timeout uses its default");
    check(settings.maximum_duration_ms == 5000, "out-of-range duration uses its default");
    check(settings.double_ctrl.minimum_interval_ms == 100 && settings.double_ctrl.maximum_interval_ms == 500,
          "an inconsistent Ctrl interval pair resets to defaults");
    check(settings.shake.minimum_distance == 1000.0, "non-finite floating-point values use their defaults");
}

void test_exported_defaults_round_trip()
{
    const auto settings = zmouse::config::parse_ini(zmouse::config::default_ini_text());
    check(!settings.shake_enabled && !settings.auto_timeout_enabled, "exported defaults keep optional triggers off");
    check(settings.spotlight_radius_dip == 120 && settings.dim_opacity_percent == 60,
          "exported overlay defaults round-trip");
    check(settings.double_ctrl.maximum_interval_ms == 500, "exported Ctrl defaults round-trip");
    check(settings.shake.minimum_reversals == 3, "exported shake defaults round-trip");
}

void test_file_export_load_and_no_overwrite()
{
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path =
        std::filesystem::temp_directory_path() / ("ZMouseShow-config-test-" + std::to_string(unique_suffix) + ".ini");

    const bool exported = zmouse::config::write_default_ini(path);
    check(exported, "default configuration can be exported to a new file");
    if (!exported)
    {
        return;
    }

    const auto loaded = zmouse::config::load_ini(path);
    check(loaded.has_value(), "an exported configuration can be loaded");
    check(!zmouse::config::write_default_ini(path), "export never overwrites an existing configuration");

    std::error_code error;
    static_cast<void>(std::filesystem::remove(path, error));
    check(!error, "the configuration test file is cleaned up");
}
} // namespace

int main()
{
    test_empty_configuration_uses_defaults();
    test_valid_values_override_defaults();
    test_invalid_values_are_ignored();
    test_exported_defaults_round_trip();
    test_file_export_load_and_no_overwrite();

    if (failures == 0)
    {
        std::cout << "All configuration tests passed.\n";
        return 0;
    }

    std::cerr << failures << " configuration assertion(s) failed.\n";
    return 1;
}

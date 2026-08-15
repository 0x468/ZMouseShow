#include "zmouse/config/settings.hpp"
#include "zmouse/diagnostics/report.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

[[nodiscard]] std::filesystem::path temporary_path(const std::string_view label)
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("ZMouseShow-" + std::string(label) + '-' + std::to_string(suffix) + ".toml");
}

[[nodiscard]] std::string read_all(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void remove_test_file(const std::filesystem::path& path)
{
    std::error_code error;
    static_cast<void>(std::filesystem::remove(path, error));
    check(!error, "the configuration test file is cleaned up");
}

void test_empty_configuration_uses_defaults()
{
    const auto settings = zmouse::config::parse_toml({});
    check(settings.has_value(), "an empty TOML document is valid");
    if (!settings)
    {
        return;
    }

    check(!settings->shake_enabled, "shake is disabled by default");
    check(!settings->auto_timeout_enabled, "automatic timeout is disabled by default");
    check(settings->spotlight_radius_dip == 120, "default spotlight radius is retained");
    check(settings->dim_opacity_percent == 60, "default dim opacity is retained");
}

void test_valid_values_override_defaults()
{
    constexpr std::string_view toml = R"toml(
[general]
shake_enabled = true
auto_timeout_enabled = true

[overlay]
radius_dip = 180
dim_opacity_percent = 72

[timeout]
idle_ms = 2500
maximum_duration_ms = 12000

[double_ctrl]
side = "right"
minimum_interval_ms = 80
maximum_interval_ms = 650
cooldown_ms = 900

[hotkey]
enabled = true
key = "F12"
control = true
alt = true
shift = false
windows = false

[shake]
interval_ms = 1400
minimum_distance = 1500.5
minimum_path_to_diagonal_ratio = 5.5
minimum_reversals = 5
cooldown_ms = 1100
)toml";

    const auto settings = zmouse::config::parse_toml(toml);
    check(settings.has_value(), "valid TOML is accepted");
    if (!settings)
    {
        return;
    }

    check(settings->shake_enabled, "shake setting is parsed");
    check(settings->auto_timeout_enabled, "timeout setting is parsed");
    check(settings->spotlight_radius_dip == 180, "spotlight radius is parsed");
    check(settings->dim_opacity_percent == 72, "dim opacity is parsed");
    check(settings->idle_timeout_ms == 2500, "idle timeout is parsed");
    check(settings->maximum_duration_ms == 12000, "maximum duration is parsed");
    check(settings->double_ctrl.side == zmouse::input::ControlSide::right, "Ctrl side is parsed");
    check(settings->double_ctrl.minimum_interval_ms == 80, "minimum Ctrl interval is parsed");
    check(settings->double_ctrl.maximum_interval_ms == 650, "maximum Ctrl interval is parsed");
    check(settings->double_ctrl.cooldown_ms == 900, "Ctrl cooldown is parsed");
    check(settings->hotkey.enabled && settings->hotkey.key == 0x7B, "custom hotkey is parsed");
    check(settings->hotkey.control && settings->hotkey.alt && !settings->hotkey.shift && !settings->hotkey.windows,
          "custom hotkey modifiers are parsed");
    check(settings->shake.interval_ms == 1400, "shake interval is parsed");
    check(std::abs(settings->shake.minimum_distance - 1500.5) < 0.001, "shake distance is parsed");
    check(std::abs(settings->shake.minimum_path_to_diagonal_ratio - 5.5) < 0.001, "shake path ratio is parsed");
    check(settings->shake.minimum_reversals == 5, "shake reversal count is parsed");
    check(settings->shake.cooldown_ms == 1100, "shake cooldown is parsed");
}

void test_invalid_values_are_ignored()
{
    constexpr std::string_view toml = R"toml(
[general]
shake_enabled = "maybe"
unknown_key = 42

[overlay]
radius_dip = 4096
dim_opacity_percent = -1

[timeout]
idle_ms = "not-a-number"
maximum_duration_ms = 9999999

[double_ctrl]
side = "unsupported"
minimum_interval_ms = 900
maximum_interval_ms = 200

[hotkey]
enabled = true
key = "Escape"

[shake]
minimum_distance = nan
)toml";

    const auto settings = zmouse::config::parse_toml(toml);
    check(settings.has_value(), "valid TOML with unusable fields is accepted");
    if (!settings)
    {
        return;
    }

    check(!settings->shake_enabled, "a wrong-type boolean uses its default");
    check(settings->spotlight_radius_dip == 120, "out-of-range radius uses its default");
    check(settings->dim_opacity_percent == 60, "out-of-range opacity uses its default");
    check(settings->idle_timeout_ms == 1200, "a wrong-type timeout uses its default");
    check(settings->maximum_duration_ms == 5000, "out-of-range duration uses its default");
    check(settings->double_ctrl.minimum_interval_ms == 100 && settings->double_ctrl.maximum_interval_ms == 500,
          "an inconsistent Ctrl interval pair resets to defaults");
    check(settings->double_ctrl.side == zmouse::input::ControlSide::left, "an unsupported Ctrl side uses its default");
    check(settings->hotkey.key == 0x7B, "an unsupported custom hotkey key uses its default");
    check(settings->shake.minimum_distance == 1000.0, "non-finite values use their defaults");
}

void test_invalid_syntax_is_rejected()
{
    check(!zmouse::config::parse_toml("[general\nshake_enabled = true"), "syntactically invalid TOML is rejected");
}

void test_exported_defaults_round_trip()
{
    const auto settings = zmouse::config::parse_toml(zmouse::config::default_toml_text());
    check(settings.has_value(), "the exported defaults are valid TOML");
    if (!settings)
    {
        return;
    }

    check(!settings->shake_enabled && !settings->auto_timeout_enabled, "exported defaults keep optional triggers off");
    check(settings->spotlight_radius_dip == 120 && settings->dim_opacity_percent == 60,
          "exported overlay defaults round-trip");
    check(settings->double_ctrl.maximum_interval_ms == 500, "exported Ctrl defaults round-trip");
    check(settings->double_ctrl.side == zmouse::input::ControlSide::left, "exported Ctrl side defaults to left");
    check(!settings->hotkey.enabled && settings->hotkey.key == 0x7B, "exported custom hotkey defaults round-trip");
    check(settings->shake.minimum_reversals == 3, "exported shake defaults round-trip");
}

void test_file_export_load_and_no_overwrite()
{
    const auto path = temporary_path("export-test");
    const bool exported = zmouse::config::write_default_toml(path);
    check(exported, "default configuration can be exported to a new file");
    if (!exported)
    {
        return;
    }

    check(zmouse::config::load_toml(path).has_value(), "an exported configuration can be loaded");
    check(!zmouse::config::write_default_toml(path), "export never overwrites an existing configuration");
    remove_test_file(path);
}

void test_preferences_are_persisted_without_losing_comments()
{
    const auto path = temporary_path("persistence-test");
    {
        std::ofstream stream(path, std::ios::binary);
        stream << R"toml(# keep this comment
[general]
shake_enabled = false # keep this inline comment

[custom]
answer = 42
)toml";
    }

    check(zmouse::config::persist_runtime_preferences(path, true, true), "runtime preferences can be persisted");
    const auto contents = read_all(path);
    check(contents.find("# keep this comment") != std::string::npos, "leading comments are preserved");
    check(contents.find("# keep this inline comment") != std::string::npos, "inline comments are preserved");
    check(contents.find("[custom]\nanswer = 42") != std::string::npos, "unknown tables are preserved");

    const auto loaded = zmouse::config::load_toml(path);
    check(loaded && loaded->shake_enabled && loaded->auto_timeout_enabled, "persisted preferences can be loaded");
    remove_test_file(path);
}

void test_preferences_create_a_missing_configuration()
{
    const auto path = temporary_path("create-test");
    check(zmouse::config::persist_runtime_preferences(path, true, false),
          "persisting preferences creates a missing configuration");
    const auto loaded = zmouse::config::load_toml(path);
    check(loaded && loaded->shake_enabled && !loaded->auto_timeout_enabled,
          "newly created configuration contains the requested preferences");
    remove_test_file(path);
}

void test_diagnostics_report_contains_effective_state_without_input_history()
{
    zmouse::diagnostics::Snapshot snapshot{
        .version = "0.1.0-test",
        .build_type = "Debug",
        .architecture = "x64",
        .generated_at_utc = "2026-08-15T00:00:00Z",
        .config_path = "C:/portable/ZMouseShow.toml",
        .paused = true,
        .remote_session = false,
        .virtual_desktop = {-2560, 0, 2560, 1440},
        .monitors = {{.device_name = "DISPLAY1",
                      .bounds = {-2560, 0, 0, 1440},
                      .work_area = {-2560, 0, 0, 1400},
                      .dpi_x = 120,
                      .dpi_y = 120,
                      .primary = false}},
    };
    snapshot.settings.double_ctrl.side = zmouse::input::ControlSide::right;
    snapshot.settings.hotkey.enabled = true;

    const auto report = zmouse::diagnostics::build_report(snapshot);
    check(report.find("version: 0.1.0-test") != std::string::npos, "diagnostics include the application version");
    check(report.find("virtual_desktop: -2560,0 - 2560,1440 (5120x1440)") != std::string::npos,
          "diagnostics include negative-coordinate desktop geometry");
    check(report.find("monitor[0].dpi: 120x120") != std::string::npos, "diagnostics include per-monitor DPI");
    check(report.find("double_ctrl.side: right") != std::string::npos,
          "diagnostics include effective keyboard settings");
    check(report.find("does not contain key history") != std::string::npos,
          "diagnostics explicitly document their privacy boundary");

    const auto path = temporary_path("diagnostics-test");
    check(zmouse::diagnostics::write_report(path, snapshot), "diagnostics can be written to disk");
    check(read_all(path) == report, "the written diagnostics match the generated report");
    remove_test_file(path);
}
} // namespace

int main()
{
    test_empty_configuration_uses_defaults();
    test_valid_values_override_defaults();
    test_invalid_values_are_ignored();
    test_invalid_syntax_is_rejected();
    test_exported_defaults_round_trip();
    test_file_export_load_and_no_overwrite();
    test_preferences_are_persisted_without_losing_comments();
    test_preferences_create_a_missing_configuration();
    test_diagnostics_report_contains_effective_state_without_input_history();

    if (failures == 0)
    {
        std::cout << "All configuration tests passed.\n";
        return 0;
    }

    std::cerr << failures << " configuration assertion(s) failed.\n";
    return 1;
}

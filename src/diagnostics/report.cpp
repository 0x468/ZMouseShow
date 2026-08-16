#include "zmouse/diagnostics/report.hpp"

#include <fstream>
#include <sstream>
#include <string_view>

namespace zmouse::diagnostics
{
namespace
{
[[nodiscard]] constexpr std::string_view boolean(const bool value) noexcept
{
    return value ? "true" : "false";
}

[[nodiscard]] constexpr std::string_view control_side(const input::ControlSide side) noexcept
{
    switch (side)
    {
    case input::ControlSide::left:
        return "left";
    case input::ControlSide::right:
        return "right";
    case input::ControlSide::either:
        return "either";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view spotlight_shape(const overlay::SpotlightShape shape) noexcept
{
    switch (shape)
    {
    case overlay::SpotlightShape::circle:
        return "circle";
    case overlay::SpotlightShape::rounded_square:
        return "rounded_square";
    case overlay::SpotlightShape::diamond:
        return "diamond";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view fullscreen_suppression(const policy::FullscreenSuppression mode) noexcept
{
    switch (mode)
    {
    case policy::FullscreenSuppression::off:
        return "off";
    case policy::FullscreenSuppression::automatic:
        return "automatic";
    case policy::FullscreenSuppression::strict:
        return "strict";
    }
    return "unknown";
}

[[nodiscard]] std::string hotkey_key(const std::uint16_t key)
{
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
    {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= 0x70U && key <= 0x87U)
    {
        return "F" + std::to_string(key - 0x70U + 1U);
    }
    return "unsupported(" + std::to_string(key) + ')';
}

void append_rect(std::ostringstream& report, const Rect& rect)
{
    report << rect.left << ',' << rect.top << " - " << rect.right << ',' << rect.bottom << " ("
           << rect.right - rect.left << 'x' << rect.bottom - rect.top << ')';
}
} // namespace

std::string build_report(const Snapshot& snapshot)
{
    std::ostringstream report;
    report << "ZMouseShow diagnostics\n"
           << "======================\n"
           << "version: " << snapshot.version << '\n'
           << "build_type: " << snapshot.build_type << '\n'
           << "architecture: " << snapshot.architecture << '\n'
           << "generated_at_utc: " << snapshot.generated_at_utc << '\n'
           << "config_path: " << snapshot.config_path << '\n'
           << "paused: " << boolean(snapshot.paused) << '\n'
           << "activation_pending: " << boolean(snapshot.activation_pending) << '\n'
           << "overlay_visible: " << boolean(snapshot.overlay_visible) << '\n'
           << "tray_menu_open: " << boolean(snapshot.tray_menu_open) << '\n'
           << "settings_dialog_open: " << boolean(snapshot.settings_dialog_open) << '\n'
           << "remote_session: " << boolean(snapshot.remote_session) << '\n'
           << "custom_hotkey_registered: " << boolean(snapshot.custom_hotkey_registered) << '\n'
           << "startup_registered: " << boolean(snapshot.startup_registered) << "\n\n"
           << "Display topology\n"
           << "----------------\n"
           << "virtual_desktop: ";
    append_rect(report, snapshot.virtual_desktop);
    report << "\nmonitor_count: " << snapshot.monitors.size() << '\n';
    for (std::size_t index = 0; index < snapshot.monitors.size(); ++index)
    {
        const auto& monitor = snapshot.monitors[index];
        report << "monitor[" << index << "].device: " << monitor.device_name << '\n'
               << "monitor[" << index << "].primary: " << boolean(monitor.primary) << '\n'
               << "monitor[" << index << "].bounds: ";
        append_rect(report, monitor.bounds);
        report << "\nmonitor[" << index << "].work_area: ";
        append_rect(report, monitor.work_area);
        report << "\nmonitor[" << index << "].dpi: " << monitor.dpi_x << 'x' << monitor.dpi_y << '\n';
    }

    const auto& settings = snapshot.settings;
    report << "\nEffective settings\n"
           << "------------------\n"
           << "general.shake_enabled: " << boolean(settings.shake_enabled) << '\n'
           << "general.auto_timeout_enabled: " << boolean(settings.auto_timeout_enabled) << '\n'
           << "overlay.radius_dip: " << settings.spotlight_radius_dip << '\n'
           << "overlay.shape: " << spotlight_shape(settings.spotlight_shape) << '\n'
           << "overlay.dim_opacity_percent: " << settings.dim_opacity_percent << '\n'
           << "effects.focus_ring_enabled: " << boolean(settings.effects.focus_ring_enabled) << '\n'
           << "effects.ripple_enabled: " << boolean(settings.effects.ripple_enabled) << '\n'
           << "effects.crosshair_enabled: " << boolean(settings.effects.crosshair_enabled) << '\n'
           << "effects.enlarged_cursor_enabled: " << boolean(settings.effects.enlarged_cursor_enabled) << '\n'
           << "effects.cursor_scale_percent: " << settings.effects.cursor_scale_percent << '\n'
           << "behavior.fullscreen_suppression: "
           << fullscreen_suppression(settings.activation_policy.fullscreen_suppression) << '\n'
           << "behavior.excluded_process_count: " << settings.activation_policy.excluded_processes.size() << '\n';
    for (std::size_t index = 0; index < settings.activation_policy.excluded_processes.size(); ++index)
    {
        report << "behavior.excluded_processes[" << index
               << "]: " << settings.activation_policy.excluded_processes[index] << '\n';
    }
    report << "startup.enabled: " << boolean(settings.startup_enabled) << '\n'
           << "timeout.idle_ms: " << settings.idle_timeout_ms << '\n'
           << "timeout.maximum_duration_ms: " << settings.maximum_duration_ms << '\n'
           << "double_ctrl.enabled: " << boolean(settings.double_ctrl.enabled) << '\n'
           << "double_ctrl.side: " << control_side(settings.double_ctrl.side) << '\n'
           << "double_ctrl.minimum_interval_ms: " << settings.double_ctrl.minimum_interval_ms << '\n'
           << "double_ctrl.maximum_interval_ms: " << settings.double_ctrl.maximum_interval_ms << '\n'
           << "double_ctrl.cooldown_ms: " << settings.double_ctrl.cooldown_ms << '\n'
           << "hotkey.enabled: " << boolean(settings.hotkey.enabled) << '\n'
           << "hotkey.key: " << hotkey_key(settings.hotkey.key) << '\n'
           << "hotkey.control: " << boolean(settings.hotkey.control) << '\n'
           << "hotkey.alt: " << boolean(settings.hotkey.alt) << '\n'
           << "hotkey.shift: " << boolean(settings.hotkey.shift) << '\n'
           << "hotkey.windows: " << boolean(settings.hotkey.windows) << '\n'
           << "shake.interval_ms: " << settings.shake.interval_ms << '\n'
           << "shake.minimum_distance: " << settings.shake.minimum_distance << '\n'
           << "shake.minimum_path_to_diagonal_ratio: " << settings.shake.minimum_path_to_diagonal_ratio << '\n'
           << "shake.minimum_reversals: " << settings.shake.minimum_reversals << '\n'
           << "shake.cooldown_ms: " << settings.shake.cooldown_ms << "\n\n"
           << "Privacy\n"
           << "-------\n"
           << "This report does not contain key history, pointer coordinates, or mouse movement history.\n";
    return report.str();
}

bool write_report(const std::filesystem::path& path, const Snapshot& snapshot) noexcept
{
    try
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }
        const auto report = build_report(snapshot);
        stream.write(report.data(), static_cast<std::streamsize>(report.size()));
        stream.flush();
        return stream.good();
    }
    catch (...)
    {
        return false;
    }
}
} // namespace zmouse::diagnostics

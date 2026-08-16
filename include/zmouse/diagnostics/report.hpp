#pragma once

#include "zmouse/config/settings.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zmouse::diagnostics
{
struct Rect
{
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
};

struct Monitor
{
    std::string device_name;
    Rect bounds{};
    Rect work_area{};
    std::uint32_t dpi_x{96};
    std::uint32_t dpi_y{96};
    bool primary{};
};

struct Snapshot
{
    std::string version;
    std::string build_type;
    std::string architecture;
    std::string generated_at_utc;
    std::string config_path;
    bool paused{};
    bool activation_pending{};
    bool overlay_visible{};
    bool tray_menu_open{};
    bool settings_dialog_open{};
    bool remote_session{};
    bool custom_hotkey_registered{};
    bool startup_registered{};
    Rect virtual_desktop{};
    std::vector<Monitor> monitors;
    config::Settings settings{};
};

[[nodiscard]] std::string build_report(const Snapshot& snapshot);
[[nodiscard]] bool write_report(const std::filesystem::path& path, const Snapshot& snapshot) noexcept;
} // namespace zmouse::diagnostics

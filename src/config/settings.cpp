#include "zmouse/config/settings.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <toml.hpp>
#include <type_traits>

namespace zmouse::config
{
namespace
{
constexpr std::uintmax_t maximum_config_size = 64U * 1024U;

constexpr std::string_view default_configuration = R"toml(# ZMouseShow portable configuration
# Unknown, mistyped, and out-of-range fields are ignored.

[general]
shake_enabled = false
auto_timeout_enabled = false

[overlay]
dim_enabled = true
radius_dip = 120
shape = "circle"
dim_opacity_percent = 60

[effects]
focus_ring_enabled = true
ripple_enabled = true
crosshair_enabled = false
enlarged_cursor_enabled = false
cursor_scale_percent = 200

[magnifier]
enabled = false
zoom_percent = 200
diameter_dip = 280
shape = "circle"
follow_mode = "centered"
edge_effect = "subtle"

[behavior]
fullscreen_suppression = "automatic"
excluded_processes = []

[startup]
enabled = false

[timeout]
idle_ms = 1200
maximum_duration_ms = 5000

[double_ctrl]
enabled = true
side = "left"
minimum_interval_ms = 100
maximum_interval_ms = 500
cooldown_ms = 500

[hotkey]
enabled = false
key = "F11"
control = true
alt = true
shift = false
windows = false

[shake]
interval_ms = 1000
minimum_distance = 1000.0
minimum_path_to_diagonal_ratio = 4.0
minimum_reversals = 3
cooldown_ms = 800
)toml";

[[nodiscard]] std::string_view trim(std::string_view value) noexcept
{
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
    {
        return {};
    }
    return value.substr(first, value.find_last_not_of(whitespace) - first + 1);
}

[[nodiscard]] std::string toml_double(const double value)
{
    std::array<char, 64> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                            std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (error != std::errc{})
    {
        return {};
    }
    return {buffer.data(), end};
}

template <typename Value>
void assign_integer(const toml::table& table, const std::string_view key, Value& destination,
                    const std::int64_t minimum, const std::int64_t maximum) noexcept
{
    static_assert(std::is_integral_v<Value> && !std::is_same_v<Value, bool>);
    const auto value = table[key].value<std::int64_t>();
    if (value && *value >= minimum && *value <= maximum)
    {
        destination = static_cast<Value>(*value);
    }
}

void assign_double(const toml::table& table, const std::string_view key, double& destination, const double minimum,
                   const double maximum) noexcept
{
    const auto value = table[key].value<double>();
    if (value && std::isfinite(*value) && *value >= minimum && *value <= maximum)
    {
        destination = *value;
    }
}

[[nodiscard]] bool assign_optional_boolean(const toml::table& table, const std::string_view key,
                                           bool& destination) noexcept
{
    const auto* node = table.get(key);
    if (node == nullptr)
    {
        return true;
    }
    const auto value = node->value<bool>();
    if (!value)
    {
        return false;
    }
    destination = *value;
    return true;
}

[[nodiscard]] char ascii_upper(const char value) noexcept
{
    return value >= 'a' && value <= 'z' ? static_cast<char>(value - ('a' - 'A')) : value;
}

[[nodiscard]] std::optional<std::uint16_t> parse_hotkey_key(const std::string_view value) noexcept
{
    if (value.size() == 1)
    {
        const char key = ascii_upper(value.front());
        if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
        {
            return static_cast<std::uint16_t>(key);
        }
        return std::nullopt;
    }

    if (value.size() < 2 || ascii_upper(value.front()) != 'F')
    {
        return std::nullopt;
    }

    std::uint16_t function_number = 0;
    for (const char character : value.substr(1))
    {
        if (character < '0' || character > '9')
        {
            return std::nullopt;
        }
        function_number =
            static_cast<std::uint16_t>(function_number * 10U + static_cast<std::uint16_t>(character - '0'));
    }
    if (function_number < 1 || function_number > 24)
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(0x70U + function_number - 1U);
}

[[nodiscard]] std::optional<overlay::SpotlightShape> parse_spotlight_shape(const std::string_view value) noexcept
{
    if (value == "circle")
    {
        return overlay::SpotlightShape::circle;
    }
    if (value == "rounded_square")
    {
        return overlay::SpotlightShape::rounded_square;
    }
    if (value == "diamond")
    {
        return overlay::SpotlightShape::diamond;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<magnifier::Shape> parse_magnifier_shape(const std::string_view value) noexcept
{
    if (value == "circle")
    {
        return magnifier::Shape::circle;
    }
    if (value == "rounded_rectangle")
    {
        return magnifier::Shape::rounded_rectangle;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view magnifier_shape_name(const magnifier::Shape shape) noexcept
{
    return shape == magnifier::Shape::rounded_rectangle ? "rounded_rectangle" : "circle";
}

[[nodiscard]] std::optional<magnifier::FollowMode> parse_follow_mode(const std::string_view value) noexcept
{
    return value == "centered" ? std::optional{magnifier::FollowMode::centered} : std::nullopt;
}

[[nodiscard]] std::optional<magnifier::EdgeEffect> parse_edge_effect(const std::string_view value) noexcept
{
    if (value == "off")
    {
        return magnifier::EdgeEffect::off;
    }
    if (value == "subtle")
    {
        return magnifier::EdgeEffect::subtle;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view edge_effect_name(const magnifier::EdgeEffect effect) noexcept
{
    return effect == magnifier::EdgeEffect::off ? "off" : "subtle";
}

[[nodiscard]] std::string_view spotlight_shape_name(const overlay::SpotlightShape shape) noexcept
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
    return "circle";
}

[[nodiscard]] std::optional<policy::FullscreenSuppression> parse_fullscreen_suppression(
    const std::string_view value) noexcept
{
    if (value == "off")
    {
        return policy::FullscreenSuppression::off;
    }
    if (value == "automatic")
    {
        return policy::FullscreenSuppression::automatic;
    }
    if (value == "strict")
    {
        return policy::FullscreenSuppression::strict;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view fullscreen_suppression_name(const policy::FullscreenSuppression mode) noexcept
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
    return "automatic";
}

[[nodiscard]] std::optional<std::vector<std::string>> parse_excluded_processes(const toml::node* node)
{
    if (node == nullptr)
    {
        return std::vector<std::string>{};
    }
    const auto* array = node->as_array();
    if (array == nullptr || array->size() > 64)
    {
        return std::nullopt;
    }

    std::vector<std::string> result;
    result.reserve(array->size());
    for (const auto& item : *array)
    {
        const auto value = item.value<std::string>();
        const auto normalized = value ? policy::normalize_executable_name(*value) : std::nullopt;
        if (!normalized)
        {
            return std::nullopt;
        }
        if (std::ranges::find(result, *normalized) == result.end())
        {
            result.push_back(*normalized);
        }
    }
    return result;
}

[[nodiscard]] std::string excluded_processes_toml(const std::vector<std::string>& processes)
{
    std::string result{"["};
    for (std::size_t index = 0; index < processes.size(); ++index)
    {
        if (index != 0)
        {
            result += ", ";
        }
        // NOTE: normalize_executable_name rejects ", \, and control characters,
        // so no TOML escaping is needed here. If that validation ever changes,
        // switch to toml++ string serialization to avoid injection.
        result += '"' + processes[index] + '"';
    }
    result += ']';
    return result;
}

void apply_table(const toml::table& document, Settings& settings) noexcept
{
    if (const auto* general = document["general"].as_table())
    {
        if (const auto value = (*general)["shake_enabled"].value<bool>())
        {
            settings.shake_enabled = *value;
        }
        if (const auto value = (*general)["auto_timeout_enabled"].value<bool>())
        {
            settings.auto_timeout_enabled = *value;
        }
    }

    if (const auto* overlay = document["overlay"].as_table())
    {
        if (const auto value = (*overlay)["dim_enabled"].value<bool>())
        {
            settings.dim_enabled = *value;
        }
        assign_integer(*overlay, "radius_dip", settings.spotlight_radius_dip, 32, 512);
        if (const auto value = (*overlay)["shape"].value<std::string>())
        {
            if (const auto shape = parse_spotlight_shape(*value))
            {
                settings.spotlight_shape = *shape;
            }
        }
        assign_integer(*overlay, "dim_opacity_percent", settings.dim_opacity_percent, 10, 90);
    }

    if (const auto* timeout = document["timeout"].as_table())
    {
        assign_integer(*timeout, "idle_ms", settings.idle_timeout_ms, 100, 60'000);
        assign_integer(*timeout, "maximum_duration_ms", settings.maximum_duration_ms, 500, 600'000);
    }

    if (const auto* effects = document["effects"].as_table())
    {
        if (const auto value = (*effects)["focus_ring_enabled"].value<bool>())
        {
            settings.effects.focus_ring_enabled = *value;
        }
        if (const auto value = (*effects)["ripple_enabled"].value<bool>())
        {
            settings.effects.ripple_enabled = *value;
        }
        if (const auto value = (*effects)["crosshair_enabled"].value<bool>())
        {
            settings.effects.crosshair_enabled = *value;
        }
        if (const auto value = (*effects)["enlarged_cursor_enabled"].value<bool>())
        {
            settings.effects.enlarged_cursor_enabled = *value;
        }
        assign_integer(*effects, "cursor_scale_percent", settings.effects.cursor_scale_percent, 125, 400);
    }

    if (const auto* magnifier = document["magnifier"].as_table())
    {
        if (const auto value = (*magnifier)["enabled"].value<bool>())
        {
            settings.magnifier.enabled = *value;
        }
        assign_integer(*magnifier, "zoom_percent", settings.magnifier.zoom_percent, 125, 400);
        assign_integer(*magnifier, "diameter_dip", settings.magnifier.diameter_dip, 160, 640);
        if (const auto value = (*magnifier)["shape"].value<std::string>())
        {
            if (const auto shape = parse_magnifier_shape(*value))
            {
                settings.magnifier.shape = *shape;
            }
        }
        if (const auto value = (*magnifier)["follow_mode"].value<std::string>())
        {
            if (const auto mode = parse_follow_mode(*value))
            {
                settings.magnifier.follow_mode = *mode;
            }
        }
        if (const auto value = (*magnifier)["edge_effect"].value<std::string>())
        {
            if (const auto effect = parse_edge_effect(*value))
            {
                settings.magnifier.edge_effect = *effect;
            }
        }
    }

    if (const auto* behavior = document["behavior"].as_table())
    {
        if (const auto value = (*behavior)["fullscreen_suppression"].value<std::string>())
        {
            if (const auto mode = parse_fullscreen_suppression(*value))
            {
                settings.activation_policy.fullscreen_suppression = *mode;
            }
        }
        if (const auto processes = parse_excluded_processes(behavior->get("excluded_processes")))
        {
            settings.activation_policy.excluded_processes = *processes;
            // Pre-normalize for O(1) lookup in should_allow_activation
            for (const auto& proc : *processes)
            {
                if (auto normalized = policy::normalize_executable_name(proc))
                {
                    settings.activation_policy.normalized_excluded.push_back(std::move(*normalized));
                }
            }
        }
    }

    if (const auto* startup = document["startup"].as_table())
    {
        if (const auto value = (*startup)["enabled"].value<bool>())
        {
            settings.startup_enabled = *value;
        }
    }

    const auto default_double_ctrl = input::DoubleCtrlConfig{};
    if (const auto* double_ctrl = document["double_ctrl"].as_table())
    {
        if (const auto enabled = (*double_ctrl)["enabled"].value<bool>())
        {
            settings.double_ctrl.enabled = *enabled;
        }
        if (const auto side = (*double_ctrl)["side"].value<std::string>())
        {
            if (*side == "left")
            {
                settings.double_ctrl.side = input::ControlSide::left;
            }
            else if (*side == "right")
            {
                settings.double_ctrl.side = input::ControlSide::right;
            }
            else if (*side == "either")
            {
                settings.double_ctrl.side = input::ControlSide::either;
            }
        }
        assign_integer(*double_ctrl, "minimum_interval_ms", settings.double_ctrl.minimum_interval_ms, 50, 1'000);
        assign_integer(*double_ctrl, "maximum_interval_ms", settings.double_ctrl.maximum_interval_ms, 100, 2'000);
        assign_integer(*double_ctrl, "cooldown_ms", settings.double_ctrl.cooldown_ms, 0, 5'000);
        if (settings.double_ctrl.minimum_interval_ms > settings.double_ctrl.maximum_interval_ms)
        {
            settings.double_ctrl.minimum_interval_ms = default_double_ctrl.minimum_interval_ms;
            settings.double_ctrl.maximum_interval_ms = default_double_ctrl.maximum_interval_ms;
        }
    }

    if (const auto* hotkey = document["hotkey"].as_table())
    {
        input::HotkeyConfig candidate;
        bool valid = assign_optional_boolean(*hotkey, "enabled", candidate.enabled) &&
                     assign_optional_boolean(*hotkey, "control", candidate.control) &&
                     assign_optional_boolean(*hotkey, "alt", candidate.alt) &&
                     assign_optional_boolean(*hotkey, "shift", candidate.shift) &&
                     assign_optional_boolean(*hotkey, "windows", candidate.windows);
        if (const auto* key_node = hotkey->get("key"))
        {
            const auto key = key_node->value<std::string>();
            const auto parsed_key = key ? parse_hotkey_key(*key) : std::nullopt;
            if (parsed_key)
            {
                candidate.key = *parsed_key;
            }
            else
            {
                valid = false;
            }
        }
        if (!valid || !input::validate_hotkey_config(candidate).accepted)
        {
            candidate.enabled = false;
        }
        settings.hotkey = candidate;
    }

    if (const auto* shake = document["shake"].as_table())
    {
        assign_integer(*shake, "interval_ms", settings.shake.interval_ms, 100, 5'000);
        assign_double(*shake, "minimum_distance", settings.shake.minimum_distance, 100.0, 10'000.0);
        assign_double(*shake, "minimum_path_to_diagonal_ratio", settings.shake.minimum_path_to_diagonal_ratio, 1.1,
                      20.0);
        assign_integer(*shake, "minimum_reversals", settings.shake.minimum_reversals, 2, 20);
        assign_integer(*shake, "cooldown_ms", settings.shake.cooldown_ms, 0, 10'000);
    }
}

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) noexcept
{
    try
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > maximum_config_size || size > (std::numeric_limits<std::size_t>::max)())
        {
            return std::nullopt;
        }

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            return std::nullopt;
        }

        std::string contents(static_cast<std::size_t>(size), '\0');
        if (!contents.empty())
        {
            stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (!stream)
            {
                return std::nullopt;
            }
        }
        return contents;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

[[nodiscard]] bool table_header_matches(std::string_view line, const std::string_view expected) noexcept
{
    line = trim(line);
    if (line.starts_with("\xEF\xBB\xBF"))
    {
        line.remove_prefix(3);
        line = trim(line);
    }

    const auto comment = line.find('#');
    if (comment != std::string_view::npos)
    {
        line = trim(line.substr(0, comment));
    }
    if (line.size() < 2 || line.front() != '[' || line.back() != ']' || line.starts_with("[["))
    {
        return false;
    }

    auto name = trim(line.substr(1, line.size() - 2));
    if (name.size() >= 2 &&
        ((name.front() == '"' && name.back() == '"') || (name.front() == '\'' && name.back() == '\'')))
    {
        name = name.substr(1, name.size() - 2);
    }
    return name == expected;
}

[[nodiscard]] bool is_table_header(std::string_view line) noexcept
{
    line = trim(line);
    return !line.empty() && line.front() == '[';
}

[[nodiscard]] bool key_matches(std::string_view key, const std::string_view expected) noexcept
{
    key = trim(key);
    if (key == expected)
    {
        return true;
    }
    return key.size() == expected.size() + 2 &&
           ((key.front() == '"' && key.back() == '"') || (key.front() == '\'' && key.back() == '\'')) &&
           key.substr(1, key.size() - 2) == expected;
}

[[nodiscard]] std::optional<std::size_t> assignment_value_offset(const std::string_view line,
                                                                 const std::string_view key) noexcept
{
    bool in_basic_string = false;
    bool in_literal_string = false;
    bool escaped = false;
    for (std::size_t index = 0; index < line.size(); ++index)
    {
        const char character = line[index];
        if (in_basic_string)
        {
            if (character == '"' && !escaped)
            {
                in_basic_string = false;
            }
            escaped = character == '\\' && !escaped;
            if (character != '\\')
            {
                escaped = false;
            }
            continue;
        }
        if (in_literal_string)
        {
            if (character == '\'')
            {
                in_literal_string = false;
            }
            continue;
        }
        if (character == '"')
        {
            in_basic_string = true;
            continue;
        }
        if (character == '\'')
        {
            in_literal_string = true;
            continue;
        }
        if (character == '#')
        {
            return std::nullopt;
        }
        if (character != '=')
        {
            continue;
        }
        if (!key_matches(line.substr(0, index), key))
        {
            return std::nullopt;
        }
        auto value_offset = index + 1;
        while (value_offset < line.size() && (line[value_offset] == ' ' || line[value_offset] == '\t'))
        {
            ++value_offset;
        }
        return value_offset;
    }
    return std::nullopt;
}

[[nodiscard]] std::size_t assignment_value_end(const std::string_view line, const std::size_t value_offset) noexcept
{
    bool in_basic_string = false;
    bool in_literal_string = false;
    bool escaped = false;
    std::size_t end = line.size();
    for (std::size_t index = value_offset; index < line.size(); ++index)
    {
        const char character = line[index];
        if (in_basic_string)
        {
            if (character == '"' && !escaped)
            {
                in_basic_string = false;
            }
            escaped = character == '\\' && !escaped;
            if (character != '\\')
            {
                escaped = false;
            }
            continue;
        }
        if (in_literal_string)
        {
            if (character == '\'')
            {
                in_literal_string = false;
            }
            continue;
        }
        if (character == '"')
        {
            in_basic_string = true;
        }
        else if (character == '\'')
        {
            in_literal_string = true;
        }
        else if (character == '#')
        {
            end = index;
            break;
        }
    }
    while (end > value_offset && (line[end - 1] == ' ' || line[end - 1] == '\t'))
    {
        --end;
    }
    return end;
}

void append_assignment(std::string& output, const std::string_view key, const std::string_view value)
{
    if (!output.empty() && output.back() != '\n')
    {
        output.push_back('\n');
    }
    output.append(key);
    output.append(" = ");
    output.append(value);
    output.push_back('\n');
}

[[nodiscard]] std::string patch_value(std::string_view contents, const std::string_view table,
                                      const std::string_view key, const std::string_view value)
{
    std::string output;
    output.reserve(contents.size() + table.size() + key.size() + value.size() + 16);

    bool in_target_table = false;
    bool found_target_table = false;
    bool replaced = false;
    std::size_t offset = 0;
    while (offset < contents.size())
    {
        const auto newline = contents.find('\n', offset);
        const auto end = newline == std::string_view::npos ? contents.size() : newline + 1;
        auto line = contents.substr(offset, end - offset);
        auto content = line;
        if (!content.empty() && content.back() == '\n')
        {
            content.remove_suffix(1);
        }
        if (!content.empty() && content.back() == '\r')
        {
            content.remove_suffix(1);
        }

        if (is_table_header(content))
        {
            if (in_target_table && !replaced)
            {
                append_assignment(output, key, value);
                replaced = true;
            }
            in_target_table = table_header_matches(content, table);
            found_target_table = found_target_table || in_target_table;
        }

        if (in_target_table)
        {
            if (const auto value_offset = assignment_value_offset(content, key))
            {
                const auto value_end = assignment_value_end(content, *value_offset);
                output.append(content.substr(0, *value_offset));
                output.append(value);
                output.append(content.substr(value_end));
                output.append(line.substr(content.size()));
                replaced = true;
                offset = end;
                continue;
            }
        }

        output.append(line);
        offset = end;
    }

    if (found_target_table)
    {
        if (!replaced)
        {
            append_assignment(output, key, value);
        }
        return output;
    }

    if (!output.empty() && output.back() != '\n')
    {
        output.push_back('\n');
    }
    if (!output.empty())
    {
        output.push_back('\n');
    }
    output.push_back('[');
    output.append(table);
    output.append("]\n");
    append_assignment(output, key, value);
    return output;
}

[[nodiscard]] std::string patch_boolean(const std::string_view contents, const std::string_view key, const bool value)
{
    return patch_value(contents, "general", key, value ? "true" : "false");
}

[[nodiscard]] bool write_atomically(const std::filesystem::path& path, const std::string_view contents) noexcept
{
    try
    {
        auto temporary = path;
        temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());

        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                return false;
            }
            stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            stream.flush();
            if (!stream)
            {
                std::error_code ignored;
                static_cast<void>(std::filesystem::remove(temporary, ignored));
                return false;
            }
        }

        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
        {
            std::error_code ignored;
            static_cast<void>(std::filesystem::remove(temporary, ignored));
            return false;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace

std::optional<Settings> parse_toml(const std::string_view text) noexcept
{
    try
    {
        const auto document = toml::parse(text);
        Settings settings;
        apply_table(document, settings);
        return settings;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<Settings> load_toml(const std::filesystem::path& path) noexcept
{
    const auto contents = read_file(path);
    if (!contents)
    {
        return std::nullopt;
    }
    return parse_toml(*contents);
}

bool write_default_toml(const std::filesystem::path& path) noexcept
{
    try
    {
        std::ofstream stream(path, std::ios::binary | std::ios::noreplace);
        if (!stream)
        {
            return false;
        }
        stream.write(default_configuration.data(), static_cast<std::streamsize>(default_configuration.size()));
        return static_cast<bool>(stream);
    }
    catch (...)
    {
        return false;
    }
}

bool persist_runtime_preferences(const std::filesystem::path& path, const bool shake_enabled,
                                 const bool auto_timeout_enabled) noexcept
{
    try
    {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error)
        {
            return false;
        }

        const auto existing = exists ? read_file(path) : std::optional<std::string>{std::string(default_configuration)};
        if (!existing)
        {
            return false;
        }

        const auto parsed = toml::parse(*existing);
        if (const auto* general_node = parsed.get("general"))
        {
            const auto* general = general_node->as_table();
            if (general == nullptr)
            {
                return false;
            }
            for (const auto key : {"shake_enabled", "auto_timeout_enabled"})
            {
                if (const auto* node = general->get(key); node != nullptr && !node->is_boolean())
                {
                    return false;
                }
            }
        }

        auto updated = patch_boolean(*existing, "shake_enabled", shake_enabled);
        updated = patch_boolean(updated, "auto_timeout_enabled", auto_timeout_enabled);
        if (updated.size() > maximum_config_size)
        {
            return false;
        }

        const auto verified = parse_toml(updated);
        if (!verified || verified->shake_enabled != shake_enabled ||
            verified->auto_timeout_enabled != auto_timeout_enabled)
        {
            return false;
        }
        return write_atomically(path, updated);
    }
    catch (...)
    {
        return false;
    }
}

bool persist_basic_settings(const std::filesystem::path& path, const Settings& settings) noexcept
{
    try
    {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error)
        {
            return false;
        }

        const auto existing = exists ? read_file(path) : std::optional<std::string>{std::string(default_configuration)};
        if (!existing)
        {
            return false;
        }
        static_cast<void>(toml::parse(*existing));

        std::string_view side = "left";
        if (settings.double_ctrl.side == input::ControlSide::right)
        {
            side = "right";
        }
        else if (settings.double_ctrl.side == input::ControlSide::either)
        {
            side = "either";
        }

        std::string hotkey_key;
        if ((settings.hotkey.key >= 'A' && settings.hotkey.key <= 'Z') ||
            (settings.hotkey.key >= '0' && settings.hotkey.key <= '9'))
        {
            hotkey_key.push_back(static_cast<char>(settings.hotkey.key));
        }
        else if (settings.hotkey.key >= 0x70U && settings.hotkey.key <= 0x87U)
        {
            hotkey_key = "F" + std::to_string(settings.hotkey.key - 0x70U + 1U);
        }
        else
        {
            return false;
        }

        auto updated = patch_value(*existing, "general", "shake_enabled", settings.shake_enabled ? "true" : "false");
        updated =
            patch_value(updated, "general", "auto_timeout_enabled", settings.auto_timeout_enabled ? "true" : "false");
        updated = patch_value(updated, "timeout", "idle_ms", std::to_string(settings.idle_timeout_ms));
        updated = patch_value(updated, "timeout", "maximum_duration_ms", std::to_string(settings.maximum_duration_ms));
        updated = patch_value(updated, "double_ctrl", "enabled", settings.double_ctrl.enabled ? "true" : "false");
        updated = patch_value(updated, "double_ctrl", "side", '"' + std::string(side) + '"');
        updated = patch_value(updated, "double_ctrl", "minimum_interval_ms",
                              std::to_string(settings.double_ctrl.minimum_interval_ms));
        updated = patch_value(updated, "double_ctrl", "maximum_interval_ms",
                              std::to_string(settings.double_ctrl.maximum_interval_ms));
        updated = patch_value(updated, "double_ctrl", "cooldown_ms", std::to_string(settings.double_ctrl.cooldown_ms));
        updated = patch_value(updated, "hotkey", "enabled", settings.hotkey.enabled ? "true" : "false");
        updated = patch_value(updated, "hotkey", "key", '"' + hotkey_key + '"');
        updated = patch_value(updated, "hotkey", "control", settings.hotkey.control ? "true" : "false");
        updated = patch_value(updated, "hotkey", "alt", settings.hotkey.alt ? "true" : "false");
        updated = patch_value(updated, "hotkey", "shift", settings.hotkey.shift ? "true" : "false");
        updated = patch_value(updated, "hotkey", "windows", settings.hotkey.windows ? "true" : "false");
        updated = patch_value(updated, "overlay", "radius_dip", std::to_string(settings.spotlight_radius_dip));
        updated = patch_value(updated, "overlay", "dim_enabled", settings.dim_enabled ? "true" : "false");
        updated = patch_value(updated, "overlay", "shape",
                              '"' + std::string(spotlight_shape_name(settings.spotlight_shape)) + '"');
        updated = patch_value(updated, "overlay", "dim_opacity_percent", std::to_string(settings.dim_opacity_percent));
        updated = patch_value(updated, "effects", "focus_ring_enabled",
                              settings.effects.focus_ring_enabled ? "true" : "false");
        updated = patch_value(updated, "effects", "ripple_enabled", settings.effects.ripple_enabled ? "true" : "false");
        updated =
            patch_value(updated, "effects", "crosshair_enabled", settings.effects.crosshair_enabled ? "true" : "false");
        updated = patch_value(updated, "effects", "enlarged_cursor_enabled",
                              settings.effects.enlarged_cursor_enabled ? "true" : "false");
        updated = patch_value(updated, "effects", "cursor_scale_percent",
                              std::to_string(settings.effects.cursor_scale_percent));
        updated = patch_value(updated, "magnifier", "enabled", settings.magnifier.enabled ? "true" : "false");
        updated = patch_value(updated, "magnifier", "zoom_percent", std::to_string(settings.magnifier.zoom_percent));
        updated = patch_value(updated, "magnifier", "diameter_dip", std::to_string(settings.magnifier.diameter_dip));
        updated = patch_value(updated, "magnifier", "shape",
                              '"' + std::string(magnifier_shape_name(settings.magnifier.shape)) + '"');
        updated = patch_value(updated, "magnifier", "follow_mode", "\"centered\"");
        updated = patch_value(updated, "magnifier", "edge_effect",
                              '"' + std::string(edge_effect_name(settings.magnifier.edge_effect)) + '"');
        updated = patch_value(
            updated, "behavior", "fullscreen_suppression",
            '"' + std::string(fullscreen_suppression_name(settings.activation_policy.fullscreen_suppression)) + '"');
        updated = patch_value(updated, "behavior", "excluded_processes",
                              excluded_processes_toml(settings.activation_policy.excluded_processes));
        updated = patch_value(updated, "startup", "enabled", settings.startup_enabled ? "true" : "false");
        updated = patch_value(updated, "shake", "interval_ms", std::to_string(settings.shake.interval_ms));
        updated = patch_value(updated, "shake", "minimum_distance", toml_double(settings.shake.minimum_distance));
        updated = patch_value(updated, "shake", "minimum_path_to_diagonal_ratio",
                              toml_double(settings.shake.minimum_path_to_diagonal_ratio));
        updated = patch_value(updated, "shake", "minimum_reversals", std::to_string(settings.shake.minimum_reversals));
        updated = patch_value(updated, "shake", "cooldown_ms", std::to_string(settings.shake.cooldown_ms));
        if (updated.size() > maximum_config_size)
        {
            return false;
        }

        const auto verified = parse_toml(updated);
        if (!verified || verified->shake_enabled != settings.shake_enabled ||
            verified->auto_timeout_enabled != settings.auto_timeout_enabled ||
            verified->idle_timeout_ms != settings.idle_timeout_ms ||
            verified->maximum_duration_ms != settings.maximum_duration_ms ||
            verified->double_ctrl.enabled != settings.double_ctrl.enabled ||
            verified->double_ctrl.side != settings.double_ctrl.side ||
            verified->double_ctrl.minimum_interval_ms != settings.double_ctrl.minimum_interval_ms ||
            verified->double_ctrl.maximum_interval_ms != settings.double_ctrl.maximum_interval_ms ||
            verified->double_ctrl.cooldown_ms != settings.double_ctrl.cooldown_ms ||
            verified->hotkey.enabled != settings.hotkey.enabled || verified->hotkey.key != settings.hotkey.key ||
            verified->hotkey.control != settings.hotkey.control || verified->hotkey.alt != settings.hotkey.alt ||
            verified->hotkey.shift != settings.hotkey.shift || verified->hotkey.windows != settings.hotkey.windows ||
            verified->spotlight_radius_dip != settings.spotlight_radius_dip ||
            verified->dim_enabled != settings.dim_enabled || verified->spotlight_shape != settings.spotlight_shape ||
            verified->dim_opacity_percent != settings.dim_opacity_percent ||
            verified->effects.focus_ring_enabled != settings.effects.focus_ring_enabled ||
            verified->effects.ripple_enabled != settings.effects.ripple_enabled ||
            verified->effects.crosshair_enabled != settings.effects.crosshair_enabled ||
            verified->effects.enlarged_cursor_enabled != settings.effects.enlarged_cursor_enabled ||
            verified->effects.cursor_scale_percent != settings.effects.cursor_scale_percent ||
            verified->magnifier.enabled != settings.magnifier.enabled ||
            verified->magnifier.zoom_percent != settings.magnifier.zoom_percent ||
            verified->magnifier.diameter_dip != settings.magnifier.diameter_dip ||
            verified->magnifier.shape != settings.magnifier.shape ||
            verified->magnifier.follow_mode != settings.magnifier.follow_mode ||
            verified->magnifier.edge_effect != settings.magnifier.edge_effect ||
            verified->activation_policy.fullscreen_suppression != settings.activation_policy.fullscreen_suppression ||
            verified->activation_policy.excluded_processes != settings.activation_policy.excluded_processes ||
            verified->startup_enabled != settings.startup_enabled ||
            verified->shake.interval_ms != settings.shake.interval_ms ||
            verified->shake.minimum_distance != settings.shake.minimum_distance ||
            verified->shake.minimum_path_to_diagonal_ratio != settings.shake.minimum_path_to_diagonal_ratio ||
            verified->shake.minimum_reversals != settings.shake.minimum_reversals ||
            verified->shake.cooldown_ms != settings.shake.cooldown_ms)
        {
            return false;
        }
        return write_atomically(path, updated);
    }
    catch (...)
    {
        return false;
    }
}

std::string_view default_toml_text() noexcept
{
    return default_configuration;
}
} // namespace zmouse::config

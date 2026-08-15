#include "zmouse/config/settings.hpp"

#include <windows.h>

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
radius_dip = 120
dim_opacity_percent = 60

[timeout]
idle_ms = 1200
maximum_duration_ms = 5000

[double_ctrl]
minimum_interval_ms = 100
maximum_interval_ms = 500
cooldown_ms = 500

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
        assign_integer(*overlay, "radius_dip", settings.spotlight_radius_dip, 32, 512);
        assign_integer(*overlay, "dim_opacity_percent", settings.dim_opacity_percent, 10, 90);
    }

    if (const auto* timeout = document["timeout"].as_table())
    {
        assign_integer(*timeout, "idle_ms", settings.idle_timeout_ms, 100, 60'000);
        assign_integer(*timeout, "maximum_duration_ms", settings.maximum_duration_ms, 500, 600'000);
    }

    const auto default_double_ctrl = input::DoubleCtrlConfig{};
    if (const auto* double_ctrl = document["double_ctrl"].as_table())
    {
        assign_integer(*double_ctrl, "minimum_interval_ms", settings.double_ctrl.minimum_interval_ms, 50, 1'000);
        assign_integer(*double_ctrl, "maximum_interval_ms", settings.double_ctrl.maximum_interval_ms, 100, 2'000);
        assign_integer(*double_ctrl, "cooldown_ms", settings.double_ctrl.cooldown_ms, 0, 5'000);
        if (settings.double_ctrl.minimum_interval_ms > settings.double_ctrl.maximum_interval_ms)
        {
            settings.double_ctrl.minimum_interval_ms = default_double_ctrl.minimum_interval_ms;
            settings.double_ctrl.maximum_interval_ms = default_double_ctrl.maximum_interval_ms;
        }
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

[[nodiscard]] bool is_general_header(std::string_view line) noexcept
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
    return name == "general";
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

void append_preference(std::string& output, const std::string_view key, const bool value)
{
    if (!output.empty() && output.back() != '\n')
    {
        output.push_back('\n');
    }
    output.append(key);
    output.append(value ? " = true\n" : " = false\n");
}

[[nodiscard]] std::string patch_boolean(std::string_view contents, const std::string_view key, const bool value)
{
    std::string output;
    output.reserve(contents.size() + key.size() + 16);

    bool in_general = false;
    bool found_general = false;
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
            if (in_general && !replaced)
            {
                append_preference(output, key, value);
                replaced = true;
            }
            in_general = is_general_header(content);
            found_general = found_general || in_general;
        }

        if (in_general)
        {
            if (const auto value_offset = assignment_value_offset(content, key))
            {
                const auto old_value = content.substr(*value_offset);
                std::size_t old_value_length = 0;
                if (old_value.starts_with("true"))
                {
                    old_value_length = 4;
                }
                else if (old_value.starts_with("false"))
                {
                    old_value_length = 5;
                }
                if (old_value_length != 0)
                {
                    output.append(content.substr(0, *value_offset));
                    output.append(value ? "true" : "false");
                    output.append(content.substr(*value_offset + old_value_length));
                    output.append(line.substr(content.size()));
                    replaced = true;
                    offset = end;
                    continue;
                }
            }
        }

        output.append(line);
        offset = end;
    }

    if (found_general)
    {
        if (!replaced)
        {
            append_preference(output, key, value);
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
    output.append("[general]\n");
    append_preference(output, key, value);
    return output;
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

std::string_view default_toml_text() noexcept
{
    return default_configuration;
}
} // namespace zmouse::config

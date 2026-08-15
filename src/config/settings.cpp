#include "zmouse/config/settings.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <type_traits>

namespace zmouse::config
{
namespace
{
constexpr std::uintmax_t maximum_config_size = 64U * 1024U;
constexpr std::string_view default_text = R"ini(; ZMouseShow portable configuration
; Unknown, malformed, or out-of-range values are ignored.

[general]
shake_enabled=false
auto_timeout_enabled=false

[overlay]
radius_dip=120
dim_opacity_percent=60

[timeout]
idle_ms=1200
maximum_duration_ms=5000

[double_ctrl]
minimum_interval_ms=100
maximum_interval_ms=500
cooldown_ms=500

[shake]
interval_ms=1000
minimum_distance=1000.0
minimum_path_to_diagonal_ratio=4.0
minimum_reversals=3
cooldown_ms=800
)ini";

[[nodiscard]] constexpr bool ascii_space(const char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

[[nodiscard]] constexpr char ascii_lower(const char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] constexpr std::string_view trim(std::string_view value) noexcept
{
    while (!value.empty() && ascii_space(value.front()))
    {
        value.remove_prefix(1);
    }
    while (!value.empty() && ascii_space(value.back()))
    {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] constexpr bool equals_ignore_case(const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (ascii_lower(left[index]) != ascii_lower(right[index]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool parse_bool(const std::string_view text, bool& output) noexcept
{
    const auto value = trim(text);
    if (equals_ignore_case(value, "true") || equals_ignore_case(value, "yes") || equals_ignore_case(value, "on") ||
        value == "1")
    {
        output = true;
        return true;
    }
    if (equals_ignore_case(value, "false") || equals_ignore_case(value, "no") || equals_ignore_case(value, "off") ||
        value == "0")
    {
        output = false;
        return true;
    }
    return false;
}

template <typename Integer>
[[nodiscard]] bool parse_integer(const std::string_view text, Integer& output, const Integer minimum,
                                 const Integer maximum) noexcept
{
    static_assert(std::is_integral_v<Integer>);
    const auto value = trim(text);
    Integer parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed < minimum || parsed > maximum)
    {
        return false;
    }
    output = parsed;
    return true;
}

[[nodiscard]] bool parse_double(const std::string_view text, double& output, const double minimum,
                                const double maximum) noexcept
{
    const auto value = trim(text);
    double parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !std::isfinite(parsed) ||
        parsed < minimum || parsed > maximum)
    {
        return false;
    }
    output = parsed;
    return true;
}

void apply_general(Settings& settings, const std::string_view key, const std::string_view value) noexcept
{
    if (equals_ignore_case(key, "shake_enabled"))
    {
        static_cast<void>(parse_bool(value, settings.shake_enabled));
    }
    else if (equals_ignore_case(key, "auto_timeout_enabled"))
    {
        static_cast<void>(parse_bool(value, settings.auto_timeout_enabled));
    }
}

void apply_overlay(Settings& settings, const std::string_view key, const std::string_view value) noexcept
{
    if (equals_ignore_case(key, "radius_dip"))
    {
        static_cast<void>(parse_integer(value, settings.spotlight_radius_dip, 32, 512));
    }
    else if (equals_ignore_case(key, "dim_opacity_percent"))
    {
        static_cast<void>(parse_integer(value, settings.dim_opacity_percent, 10U, 90U));
    }
}

void apply_timeout(Settings& settings, const std::string_view key, const std::string_view value) noexcept
{
    if (equals_ignore_case(key, "idle_ms"))
    {
        static_cast<void>(parse_integer(value, settings.idle_timeout_ms, 100ULL, 60'000ULL));
    }
    else if (equals_ignore_case(key, "maximum_duration_ms"))
    {
        static_cast<void>(parse_integer(value, settings.maximum_duration_ms, 500ULL, 600'000ULL));
    }
}

void apply_double_ctrl(Settings& settings, const std::string_view key, const std::string_view value) noexcept
{
    if (equals_ignore_case(key, "minimum_interval_ms"))
    {
        static_cast<void>(parse_integer(value, settings.double_ctrl.minimum_interval_ms, 50ULL, 1'000ULL));
    }
    else if (equals_ignore_case(key, "maximum_interval_ms"))
    {
        static_cast<void>(parse_integer(value, settings.double_ctrl.maximum_interval_ms, 100ULL, 2'000ULL));
    }
    else if (equals_ignore_case(key, "cooldown_ms"))
    {
        static_cast<void>(parse_integer(value, settings.double_ctrl.cooldown_ms, 0ULL, 5'000ULL));
    }
}

void apply_shake(Settings& settings, const std::string_view key, const std::string_view value) noexcept
{
    if (equals_ignore_case(key, "interval_ms"))
    {
        static_cast<void>(parse_integer(value, settings.shake.interval_ms, 100ULL, 5'000ULL));
    }
    else if (equals_ignore_case(key, "minimum_distance"))
    {
        static_cast<void>(parse_double(value, settings.shake.minimum_distance, 100.0, 10'000.0));
    }
    else if (equals_ignore_case(key, "minimum_path_to_diagonal_ratio"))
    {
        static_cast<void>(parse_double(value, settings.shake.minimum_path_to_diagonal_ratio, 1.1, 20.0));
    }
    else if (equals_ignore_case(key, "minimum_reversals"))
    {
        static_cast<void>(parse_integer(value, settings.shake.minimum_reversals, std::size_t{2}, std::size_t{20}));
    }
    else if (equals_ignore_case(key, "cooldown_ms"))
    {
        static_cast<void>(parse_integer(value, settings.shake.cooldown_ms, 0ULL, 10'000ULL));
    }
}

void apply_entry(Settings& settings, const std::string_view section, const std::string_view key,
                 const std::string_view value) noexcept
{
    if (equals_ignore_case(section, "general"))
    {
        apply_general(settings, key, value);
    }
    else if (equals_ignore_case(section, "overlay"))
    {
        apply_overlay(settings, key, value);
    }
    else if (equals_ignore_case(section, "timeout"))
    {
        apply_timeout(settings, key, value);
    }
    else if (equals_ignore_case(section, "double_ctrl"))
    {
        apply_double_ctrl(settings, key, value);
    }
    else if (equals_ignore_case(section, "shake"))
    {
        apply_shake(settings, key, value);
    }
}
} // namespace

Settings parse_ini(std::string_view text) noexcept
{
    Settings settings{};
    if (text.starts_with("\xEF\xBB\xBF"))
    {
        text.remove_prefix(3);
    }

    std::string_view section;
    while (!text.empty())
    {
        const auto newline = text.find('\n');
        auto line = trim(text.substr(0, newline));
        if (newline == std::string_view::npos)
        {
            text = {};
        }
        else
        {
            text.remove_prefix(newline + 1);
        }

        if (line.empty() || line.front() == ';' || line.front() == '#')
        {
            continue;
        }
        if (line.front() == '[' && line.back() == ']')
        {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string_view::npos)
        {
            continue;
        }
        apply_entry(settings, section, trim(line.substr(0, separator)), trim(line.substr(separator + 1)));
    }

    const Settings defaults{};
    if (settings.double_ctrl.minimum_interval_ms > settings.double_ctrl.maximum_interval_ms)
    {
        settings.double_ctrl.minimum_interval_ms = defaults.double_ctrl.minimum_interval_ms;
        settings.double_ctrl.maximum_interval_ms = defaults.double_ctrl.maximum_interval_ms;
    }
    return settings;
}

std::optional<Settings> load_ini(const std::filesystem::path& path) noexcept
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
        return parse_ini(contents);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool write_default_ini(const std::filesystem::path& path) noexcept
{
    try
    {
        std::ofstream stream(path, std::ios::binary | std::ios::out | std::ios::noreplace);
        if (!stream)
        {
            return false;
        }
        stream.write(default_text.data(), static_cast<std::streamsize>(default_text.size()));
        stream.flush();
        return stream.good();
    }
    catch (...)
    {
        return false;
    }
}

std::string_view default_ini_text() noexcept
{
    return default_text;
}
} // namespace zmouse::config

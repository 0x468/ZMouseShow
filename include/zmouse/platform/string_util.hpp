#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace zmouse::platform
{
// Converts a wide string to UTF-8. Returns empty string on error or empty input.
[[nodiscard]] inline std::string wide_to_utf8(const std::wstring_view value) noexcept
{
    try
    {
        if (value.empty())
        {
            return {};
        }
        const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                                 static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            return {};
        }
        std::string result(static_cast<std::size_t>(required), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                result.data(), required, nullptr, nullptr) != required)
        {
            return {};
        }
        return result;
    }
    catch (...)
    {
        return {};
    }
}

// Converts a null-terminated wide string to UTF-8.
[[nodiscard]] inline std::string wide_to_utf8(const wchar_t* value) noexcept
{
    if (value == nullptr || *value == L'\0')
    {
        return {};
    }
    return wide_to_utf8(std::wstring_view{value});
}
} // namespace zmouse::platform

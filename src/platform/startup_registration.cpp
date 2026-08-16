#include "startup_registration.hpp"

#include <Windows.h>

#include <string>
#include <vector>

namespace zmouse::platform
{
namespace
{
constexpr wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t value_name[] = L"ZMouseShow";

std::wstring startup_command() noexcept
{
    try
    {
        std::vector<wchar_t> path(32'768);
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size())
        {
            return {};
        }
        return L"\"" + std::wstring(path.data(), length) + L"\"";
    }
    catch (...)
    {
        return {};
    }
}
} // namespace

bool startup_registration_enabled() noexcept
{
    const auto expected = startup_command();
    if (expected.empty())
    {
        return false;
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS size_status = RegQueryValueExW(key, value_name, nullptr, &type, nullptr, &size);
    constexpr DWORD maximum_command_bytes = 32'768 * sizeof(wchar_t);
    if (size_status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t) ||
        size > maximum_command_bytes || size % sizeof(wchar_t) != 0)
    {
        static_cast<void>(RegCloseKey(key));
        return false;
    }
    std::vector<wchar_t> value;
    try
    {
        value.assign(size / sizeof(wchar_t) + 1, L'\0');
    }
    catch (...)
    {
        static_cast<void>(RegCloseKey(key));
        return false;
    }
    const LSTATUS read_status =
        RegQueryValueExW(key, value_name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &size);
    static_cast<void>(RegCloseKey(key));
    if (read_status != ERROR_SUCCESS)
    {
        return false;
    }
    return CompareStringOrdinal(value.data(), -1, expected.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool set_startup_registration_enabled(const bool enabled) noexcept
{
    if (!enabled)
    {
        HKEY key = nullptr;
        const LSTATUS open_status = RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_SET_VALUE, &key);
        if (open_status == ERROR_FILE_NOT_FOUND)
        {
            return true;
        }
        if (open_status != ERROR_SUCCESS)
        {
            return false;
        }
        const LSTATUS status = RegDeleteValueW(key, value_name);
        static_cast<void>(RegCloseKey(key));
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    const auto command = startup_command();
    if (command.empty())
    {
        return false;
    }
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, run_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
    {
        return false;
    }
    const auto bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LSTATUS status =
        RegSetValueExW(key, value_name, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), bytes);
    static_cast<void>(RegCloseKey(key));
    return status == ERROR_SUCCESS;
}
} // namespace zmouse::platform

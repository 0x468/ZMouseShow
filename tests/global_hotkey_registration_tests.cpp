#include "zmouse/platform/global_hotkey_registration.hpp"
#include <iostream>
#include <string_view>
#include <utility>

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

[[nodiscard]] zmouse::input::HotkeyConfig test_hotkey() noexcept
{
    return {
        .enabled = true,
        .key = VK_F24,
        .control = true,
        .alt = true,
        .shift = true,
        .windows = false,
    };
}

void test_registration_owns_and_releases_the_combination()
{
    HWND window = CreateWindowExW(0, L"STATIC", L"ZMouseShow hotkey test", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    check(window != nullptr, "message-only test window can be created");
    if (window == nullptr)
    {
        return;
    }

    constexpr int first_identifier = 0x5A11;
    constexpr int second_identifier = 0x5A12;
    const auto config = test_hotkey();

    zmouse::platform::GlobalHotkeyRegistration first;
    zmouse::platform::GlobalHotkeyRegistration second;
    check(first.acquire(window, first_identifier, config), "first registration acquires the combination");
    check(first.active() && first.identifier() == first_identifier, "active registration exposes its identifier");
    check(first.matches(config), "active registration matches the acquired configuration");
    check(!second.acquire(window, second_identifier, config), "the same global combination cannot be acquired twice");

    first.reset();
    check(!first.active(), "reset releases the active registration");
    check(second.acquire(window, second_identifier, config), "released combination can be acquired again");

    zmouse::platform::GlobalHotkeyRegistration moved(std::move(second));
    check(moved.active() && !second.active(), "move transfers registration ownership exactly once");
    moved.reset();

    zmouse::input::HotkeyConfig invalid = config;
    invalid.key = 'S';
    invalid.alt = false;
    invalid.shift = false;
    check(!first.acquire(window, first_identifier, invalid), "unsafe configurations are rejected before registration");

    static_cast<void>(DestroyWindow(window));
}
} // namespace

int main(const int argument_count, char** arguments)
{
    if (argument_count == 2 && std::string_view(arguments[1]) == "--hold-ctrl-alt-f11")
    {
        HWND window = CreateWindowExW(0, L"STATIC", L"ZMouseShow hotkey conflict holder", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                      nullptr, GetModuleHandleW(nullptr), nullptr);
        zmouse::platform::GlobalHotkeyRegistration holder;
        const zmouse::input::HotkeyConfig config{
            .enabled = true,
            .key = VK_F11,
            .control = true,
            .alt = true,
            .shift = false,
            .windows = false,
        };
        if (window == nullptr || !holder.acquire(window, 0x5A13, config))
        {
            return 2;
        }
        std::cout << "Holding Ctrl+Alt+F11 for integration testing.\n" << std::flush;
        Sleep(INFINITE);
    }

    test_registration_owns_and_releases_the_combination();
    if (failures == 0)
    {
        std::cout << "All global hotkey registration tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}

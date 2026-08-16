#pragma once

#include <windows.h>

#include "zmouse/magnifier/settings.hpp"

namespace zmouse::platform
{
class DesktopDuplicationCapture;

class MagnifierWindow final
{
  public:
    MagnifierWindow() noexcept;
    ~MagnifierWindow();
    MagnifierWindow(const MagnifierWindow&) = delete;
    MagnifierWindow& operator=(const MagnifierWindow&) = delete;

    [[nodiscard]] bool initialize(HINSTANCE instance) noexcept;
    void configure(const magnifier::Settings& settings) noexcept;
    [[nodiscard]] bool render(DesktopDuplicationCapture& capture, POINT cursor, UINT dpi) noexcept;
    void hide() noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] HWND window() const noexcept;

  private:
    struct Impl;
    Impl* impl_{};
};
} // namespace zmouse::platform

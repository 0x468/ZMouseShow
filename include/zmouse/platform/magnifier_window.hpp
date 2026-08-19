#pragma once

#include <windows.h>

#include "zmouse/magnifier/settings.hpp"
#include <memory>

namespace zmouse::platform
{
class DesktopDuplicationCapture;

enum class MagnifierRenderResult
{
    presented,
    no_frame,
    capture_failure,
    render_failure,
};

class MagnifierWindow final
{
  public:
    MagnifierWindow() noexcept;
    ~MagnifierWindow();
    MagnifierWindow(const MagnifierWindow&) = delete;
    MagnifierWindow& operator=(const MagnifierWindow&) = delete;

    [[nodiscard]] bool initialize(HINSTANCE instance) noexcept;
    void configure(const magnifier::Settings& settings) noexcept;
    [[nodiscard]] MagnifierRenderResult render(DesktopDuplicationCapture& capture, POINT cursor, UINT dpi) noexcept;
    void hide() noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] HWND window() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace zmouse::platform

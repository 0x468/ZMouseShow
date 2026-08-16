#pragma once

#include <windows.h>

#include "zmouse/capture/capture_backend.hpp"
#include "zmouse/render/pointer_shape.hpp"
#include <cstdint>

struct ID3D11Device;
struct ID3D11Texture2D;

namespace zmouse::platform
{
[[nodiscard]] bool exclude_window_from_capture(HWND window) noexcept;

struct DuplicationPointer
{
    bool visible{};
    bool has_shape{};
    POINT position{};
    render::PointerShape shape{};
};

// A deliberately small Win32 adapter around Desktop Duplication. The rest of
// the application only sees opaque lifecycle results and privacy-safe
// diagnostics; DXGI resources never enter the configuration or input models.
class DesktopDuplicationCapture final
{
  public:
    struct Impl;

    DesktopDuplicationCapture() noexcept;
    ~DesktopDuplicationCapture();

    DesktopDuplicationCapture(const DesktopDuplicationCapture&) = delete;
    DesktopDuplicationCapture& operator=(const DesktopDuplicationCapture&) = delete;

    [[nodiscard]] bool start(HMONITOR monitor) noexcept;
    void stop() noexcept;
    [[nodiscard]] capture::FrameResult acquire(std::uint32_t timeout_ms = 0) noexcept;
    [[nodiscard]] capture::FrameResult acquire_frame(ID3D11Texture2D** texture, std::uint32_t timeout_ms = 0) noexcept;
    void release_frame() noexcept;
    [[nodiscard]] ID3D11Device* device() const noexcept;
    [[nodiscard]] RECT output_bounds() const noexcept;
    [[nodiscard]] const DuplicationPointer& pointer() const noexcept;
    void mark_exclusion(bool applied) noexcept;
    [[nodiscard]] const capture::Diagnostics& diagnostics() const noexcept;
    [[nodiscard]] bool running() const noexcept;

  private:
    Impl* impl_{};
};
} // namespace zmouse::platform

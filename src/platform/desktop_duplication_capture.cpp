#include "zmouse/platform/desktop_duplication_capture.hpp"

#include <windows.h>

#include "zmouse/platform/string_util.hpp"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <iterator>
#include <new>
#include <string>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace zmouse::platform
{
bool exclude_window_from_capture(const HWND window) noexcept
{
    return window != nullptr && SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE) != FALSE;
}

struct DesktopDuplicationCapture::Impl
{
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> output;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    capture::Lifecycle lifecycle;
    RECT output_bounds{};
    bool frame_acquired{};
    std::vector<std::byte> pointer_buffer;
    DuplicationPointer pointer;
};

namespace
{
void set_failure(DesktopDuplicationCapture::Impl& impl, const HRESULT result) noexcept
{
    using capture::FailureCategory;
    if (result == DXGI_ERROR_WAIT_TIMEOUT)
    {
        impl.lifecycle.mark_failure(FailureCategory::timeout);
    }
    else if (result == DXGI_ERROR_ACCESS_LOST)
    {
        impl.lifecycle.mark_failure(FailureCategory::access_lost);
    }
    else if (result == DXGI_ERROR_UNSUPPORTED)
    {
        impl.lifecycle.mark_failure(FailureCategory::mode_unsupported);
    }
    else if (result == DXGI_ERROR_SESSION_DISCONNECTED)
    {
        impl.lifecycle.mark_failure(FailureCategory::session_disconnected);
    }
    else if (result == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
    {
        impl.lifecycle.mark_failure(FailureCategory::duplication_limit);
    }
    else if (result == E_ACCESSDENIED)
    {
        impl.lifecycle.mark_failure(FailureCategory::protected_content);
    }
    else if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
    {
        impl.lifecycle.mark_failure(FailureCategory::device_removed);
    }
    else
    {
        impl.lifecycle.mark_failure(FailureCategory::unknown);
    }
}
} // namespace

DesktopDuplicationCapture::DesktopDuplicationCapture() noexcept
{
    try
    {
        impl_ = new Impl{};
    }
    catch (...)
    {
        impl_ = nullptr;
    }
}

DesktopDuplicationCapture::~DesktopDuplicationCapture()
{
    stop();
    delete impl_;
}

bool DesktopDuplicationCapture::start(const HMONITOR monitor) noexcept
{
    if (impl_ == nullptr || monitor == nullptr)
    {
        return false;
    }
    stop();
    impl_->lifecycle.enable();
    impl_->lifecycle.diagnostics().available = false;

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    {
        impl_->lifecycle.mark_failure(capture::FailureCategory::unknown);
        return false;
    }

    DXGI_ADAPTER_DESC1 adapter_desc{};
    for (UINT adapter_index = 0; factory->EnumAdapters1(adapter_index, &impl_->adapter) != DXGI_ERROR_NOT_FOUND;
         ++adapter_index)
    {
        if (FAILED(impl_->adapter->GetDesc1(&adapter_desc)))
        {
            impl_->adapter.Reset();
            continue;
        }
        bool matched = false;
        for (UINT output_index = 0; impl_->adapter->EnumOutputs(output_index, &impl_->output) != DXGI_ERROR_NOT_FOUND;
             ++output_index)
        {
            DXGI_OUTPUT_DESC output_desc{};
            if (SUCCEEDED(impl_->output->GetDesc(&output_desc)) && output_desc.Monitor == monitor)
            {
                impl_->lifecycle.diagnostics().output = wide_to_utf8(output_desc.DeviceName);
                impl_->output_bounds = output_desc.DesktopCoordinates;
                matched = true;
                break;
            }
            impl_->output.Reset();
        }
        if (matched)
        {
            break;
        }
        impl_->adapter.Reset();
    }
    if (impl_->adapter == nullptr || impl_->output == nullptr)
    {
        impl_->lifecycle.mark_failure(capture::FailureCategory::mode_unsupported);
        return false;
    }
    impl_->lifecycle.diagnostics().adapter = wide_to_utf8(adapter_desc.Description);

    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selected{};
    const auto result = D3D11CreateDevice(
        impl_->adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &impl_->device, &selected, &impl_->context);
    if (FAILED(result))
    {
        set_failure(*impl_, result);
        return false;
    }

    ComPtr<IDXGIOutput1> output1;
    const auto output_result = impl_->output.As(&output1);
    const auto duplication_result =
        SUCCEEDED(output_result) ? output1->DuplicateOutput(impl_->device.Get(), &impl_->duplication) : output_result;
    if (FAILED(duplication_result))
    {
        set_failure(*impl_, duplication_result);
        return false;
    }
    impl_->lifecycle.diagnostics().available = true;
    impl_->lifecycle.mark_ready();
    return true;
}

void DesktopDuplicationCapture::stop() noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }
    release_frame();
    impl_->duplication.Reset();
    impl_->context.Reset();
    impl_->device.Reset();
    impl_->output.Reset();
    impl_->adapter.Reset();
    impl_->lifecycle.diagnostics().available = false;
    impl_->lifecycle.disable();
}

capture::FrameResult DesktopDuplicationCapture::acquire(const std::uint32_t timeout_ms) noexcept
{
    ComPtr<ID3D11Texture2D> texture;
    const auto result = acquire_frame(&texture, timeout_ms);
    release_frame();
    return result;
}

capture::FrameResult DesktopDuplicationCapture::acquire_frame(ID3D11Texture2D** texture,
                                                              const std::uint32_t timeout_ms) noexcept
{
    if (texture == nullptr)
    {
        return capture::FrameResult::unavailable;
    }
    *texture = nullptr;
    if (impl_ == nullptr || !impl_->lifecycle.active() || impl_->duplication == nullptr)
    {
        return capture::FrameResult::unavailable;
    }
    if (impl_->frame_acquired)
    {
        release_frame();
    }
    DXGI_OUTDUPL_FRAME_INFO frame_info{};
    ComPtr<IDXGIResource> resource;
    const auto result = impl_->duplication->AcquireNextFrame(timeout_ms, &frame_info, &resource);
    if (result == DXGI_ERROR_WAIT_TIMEOUT)
    {
        // Zero-timeout polling returning no frame is expected; do not record
        // it as a failure so that last_failure retains the most recent real error.
        return capture::FrameResult::no_frame;
    }
    if (FAILED(result))
    {
        set_failure(*impl_, result);
        return (result == DXGI_ERROR_ACCESS_LOST || result == DXGI_ERROR_DEVICE_REMOVED ||
                result == DXGI_ERROR_DEVICE_RESET)
                   ? capture::FrameResult::rebuild_required
                   : capture::FrameResult::unavailable;
    }
    if (frame_info.LastMouseUpdateTime.QuadPart != 0)
    {
        impl_->pointer.visible = frame_info.PointerPosition.Visible != FALSE;
        impl_->pointer.position = frame_info.PointerPosition.Position;
    }
    if (frame_info.LastMouseUpdateTime.QuadPart != 0 && frame_info.PointerShapeBufferSize > 0)
    {
        try
        {
            impl_->pointer_buffer.resize(frame_info.PointerShapeBufferSize);
        }
        catch (...)
        {
            static_cast<void>(impl_->duplication->ReleaseFrame());
            set_failure(*impl_, E_OUTOFMEMORY);
            return capture::FrameResult::unavailable;
        }
        DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info{};
        UINT required_size = 0;
        const auto pointer_result = impl_->duplication->GetFramePointerShape(
            static_cast<UINT>(impl_->pointer_buffer.size()), impl_->pointer_buffer.data(), &required_size, &shape_info);
        if (SUCCEEDED(pointer_result))
        {
            render::PointerShapeType type{};
            bool known_type = true;
            switch (shape_info.Type)
            {
            case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
                type = render::PointerShapeType::color;
                break;
            case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
                type = render::PointerShapeType::monochrome;
                break;
            case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
                type = render::PointerShapeType::masked_color;
                break;
            default:
                impl_->pointer.has_shape = false;
                known_type = false;
                break;
            }
            if (known_type)
            {
                const auto logical_height =
                    type == render::PointerShapeType::monochrome ? shape_info.Height / 2U : shape_info.Height;
                impl_->pointer.shape = {
                    .type = type,
                    .width = shape_info.Width,
                    .height = logical_height,
                    .pitch = shape_info.Pitch,
                    .hotspot_x = shape_info.HotSpot.x,
                    .hotspot_y = shape_info.HotSpot.y,
                    .pixels = impl_->pointer_buffer,
                };
                impl_->pointer.has_shape = logical_height > 0 && shape_info.Width > 0;
            }
        }
        else
        {
            impl_->pointer.has_shape = false;
        }
    }
    ComPtr<ID3D11Texture2D> frame;
    if (FAILED(resource.As(&frame)))
    {
        static_cast<void>(impl_->duplication->ReleaseFrame());
        set_failure(*impl_, E_NOINTERFACE);
        return capture::FrameResult::unavailable;
    }
    impl_->frame_acquired = true;
    *texture = frame.Detach();
    impl_->lifecycle.diagnostics().last_failure = capture::FailureCategory::none;
    return capture::FrameResult::frame;
}

void DesktopDuplicationCapture::release_frame() noexcept
{
    if (impl_ != nullptr && impl_->frame_acquired && impl_->duplication != nullptr)
    {
        static_cast<void>(impl_->duplication->ReleaseFrame());
        impl_->frame_acquired = false;
    }
}

ID3D11Device* DesktopDuplicationCapture::device() const noexcept
{
    return impl_ == nullptr ? nullptr : impl_->device.Get();
}

RECT DesktopDuplicationCapture::output_bounds() const noexcept
{
    return impl_ == nullptr ? RECT{} : impl_->output_bounds;
}

const DuplicationPointer& DesktopDuplicationCapture::pointer() const noexcept
{
    static const DuplicationPointer unavailable{};
    return impl_ == nullptr ? unavailable : impl_->pointer;
}

void DesktopDuplicationCapture::mark_exclusion(const bool applied) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->lifecycle.diagnostics().exclusion_applied = applied;
    }
}

const capture::Diagnostics& DesktopDuplicationCapture::diagnostics() const noexcept
{
    static const capture::Diagnostics unavailable{};
    return impl_ == nullptr ? unavailable : impl_->lifecycle.diagnostics();
}

bool DesktopDuplicationCapture::running() const noexcept
{
    return impl_ != nullptr && impl_->lifecycle.active();
}
} // namespace zmouse::platform

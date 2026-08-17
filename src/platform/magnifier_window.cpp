#include "zmouse/platform/magnifier_window.hpp"

#include "zmouse/magnifier/geometry.hpp"
#include "zmouse/platform/desktop_duplication_capture.hpp"
#include <algorithm>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <new>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace zmouse::platform
{
namespace
{
constexpr wchar_t magnifier_class_name[] = L"ZMouseShow.ContentMagnifier";

LRESULT CALLBACK magnifier_window_proc(const HWND window, const UINT message, const WPARAM w_param,
                                       const LPARAM l_param) noexcept
{
    if (message == WM_NCHITTEST)
    {
        return HTTRANSPARENT;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

[[nodiscard]] D2D1_COLOR_F color(const float red, const float green, const float blue,
                                 const float alpha = 1.0F) noexcept
{
    return {red, green, blue, alpha};
}

[[nodiscard]] bool clip_window_to_output(const HWND window, const RECT destination, const RECT output) noexcept
{
    if (window == nullptr)
    {
        return false;
    }
    const LONG left = (std::max)(destination.left, output.left) - destination.left;
    const LONG top = (std::max)(destination.top, output.top) - destination.top;
    const LONG right = (std::min)(destination.right, output.right) - destination.left;
    const LONG bottom = (std::min)(destination.bottom, output.bottom) - destination.top;
    if (right <= left || bottom <= top)
    {
        return false;
    }
    HRGN region = CreateRectRgn(left, top, right, bottom);
    if (region == nullptr)
    {
        return false;
    }
    if (SetWindowRgn(window, region, TRUE) == 0)
    {
        DeleteObject(region);
        return false;
    }
    return true;
}
} // namespace

struct MagnifierWindow::Impl
{
    HINSTANCE instance{};
    ATOM window_class{};
    HWND window{};
    magnifier::Settings settings{};
    ComPtr<ID3D11Device> bound_device;
    ComPtr<IDXGISwapChain1> swap_chain;
    ComPtr<ID2D1Factory1> d2d_factory;
    ComPtr<ID2D1Device> d2d_device;
    ComPtr<ID2D1DeviceContext> d2d_context;
    ComPtr<ID2D1Bitmap1> target_bitmap;
    ComPtr<ID2D1Geometry> clip_geometry;
    ComPtr<ID2D1Layer> clip_layer;
    ComPtr<ID2D1SolidColorBrush> dark_brush;
    ComPtr<ID2D1SolidColorBrush> light_brush;
    ComPtr<IDCompositionDevice> composition_device;
    ComPtr<IDCompositionTarget> composition_target;
    ComPtr<IDCompositionVisual> composition_visual;
    std::int32_t surface_size{};
    bool visible{};

    void reset_graphics() noexcept
    {
        light_brush.Reset();
        dark_brush.Reset();
        clip_layer.Reset();
        clip_geometry.Reset();
        target_bitmap.Reset();
        d2d_context.Reset();
        d2d_device.Reset();
        d2d_factory.Reset();
        composition_visual.Reset();
        composition_target.Reset();
        composition_device.Reset();
        swap_chain.Reset();
        bound_device.Reset();
        surface_size = 0;
    }

    [[nodiscard]] bool create_graphics(ID3D11Device* device, const std::int32_t size) noexcept
    {
        reset_graphics();
        if (device == nullptr || size <= 0)
        {
            return false;
        }
        bound_device = device;

        ComPtr<IDXGIDevice> dxgi_device;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(bound_device.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(&adapter)) ||
            FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
        {
            reset_graphics();
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 descriptor{};
        descriptor.Width = static_cast<UINT>(size);
        descriptor.Height = static_cast<UINT>(size);
        descriptor.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        descriptor.SampleDesc.Count = 1;
        descriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        descriptor.BufferCount = 2;
        descriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        descriptor.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(factory->CreateSwapChainForComposition(bound_device.Get(), &descriptor, nullptr, &swap_chain)))
        {
            reset_graphics();
            return false;
        }

        D2D1_FACTORY_OPTIONS factory_options{};
#if defined(_DEBUG)
        factory_options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_options, d2d_factory.GetAddressOf())) ||
            FAILED(d2d_factory->CreateDevice(dxgi_device.Get(), &d2d_device)) ||
            FAILED(d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_context)))
        {
            reset_graphics();
            return false;
        }

        ComPtr<IDXGISurface> target_surface;
        if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&target_surface))))
        {
            reset_graphics();
            return false;
        }
        const auto target_properties =
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(d2d_context->CreateBitmapFromDxgiSurface(target_surface.Get(), &target_properties, &target_bitmap)))
        {
            reset_graphics();
            return false;
        }
        d2d_context->SetTarget(target_bitmap.Get());

        const float surface = static_cast<float>(size);
        if (settings.shape == magnifier::Shape::circle)
        {
            ComPtr<ID2D1EllipseGeometry> ellipse;
            if (FAILED(d2d_factory->CreateEllipseGeometry(
                    D2D1::Ellipse({surface / 2.0F, surface / 2.0F}, surface / 2.0F - 6.0F, surface / 2.0F - 6.0F),
                    &ellipse)))
            {
                reset_graphics();
                return false;
            }
            clip_geometry = ellipse;
        }
        else
        {
            ComPtr<ID2D1RoundedRectangleGeometry> rounded;
            const auto rounded_rect =
                D2D1::RoundedRect({6.0F, 6.0F, surface - 6.0F, surface - 6.0F}, std::max(18.0F, surface * 0.12F),
                                  std::max(18.0F, surface * 0.12F));
            if (FAILED(d2d_factory->CreateRoundedRectangleGeometry(rounded_rect, &rounded)))
            {
                reset_graphics();
                return false;
            }
            clip_geometry = rounded;
        }
        if (FAILED(d2d_context->CreateLayer(&clip_layer)) ||
            FAILED(d2d_context->CreateSolidColorBrush(color(0.03F, 0.03F, 0.03F, 0.95F), &dark_brush)) ||
            FAILED(d2d_context->CreateSolidColorBrush(color(1.0F, 1.0F, 1.0F, 0.92F), &light_brush)))
        {
            reset_graphics();
            return false;
        }

        if (FAILED(DCompositionCreateDevice(dxgi_device.Get(), IID_PPV_ARGS(&composition_device))) ||
            FAILED(composition_device->CreateTargetForHwnd(window, TRUE, &composition_target)) ||
            FAILED(composition_device->CreateVisual(&composition_visual)) ||
            FAILED(composition_visual->SetContent(swap_chain.Get())) ||
            FAILED(composition_target->SetRoot(composition_visual.Get())) || FAILED(composition_device->Commit()))
        {
            reset_graphics();
            return false;
        }
        surface_size = size;
        return true;
    }

    [[nodiscard]] bool draw(ID3D11Texture2D* frame, const magnifier::Geometry& geometry,
                            const RECT output_bounds) noexcept
    {
        ComPtr<IDXGISurface> source_surface;
        ComPtr<ID2D1Bitmap1> source_bitmap;
        if (frame == nullptr || FAILED(frame->QueryInterface(IID_PPV_ARGS(&source_surface))))
        {
            return false;
        }
        const auto source_properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(d2d_context->CreateBitmapFromDxgiSurface(source_surface.Get(), &source_properties, &source_bitmap)))
        {
            return false;
        }

        const float size = static_cast<float>(surface_size);
        const D2D1_RECT_F destination{0.0F, 0.0F, size, size};
        const D2D1_RECT_F source{
            static_cast<float>(geometry.source.left - output_bounds.left),
            static_cast<float>(geometry.source.top - output_bounds.top),
            static_cast<float>(geometry.source.right - output_bounds.left),
            static_cast<float>(geometry.source.bottom - output_bounds.top),
        };

        D2D1_LAYER_PARAMETERS1 layer_parameters = D2D1::LayerParameters1();
        layer_parameters.geometricMask = clip_geometry.Get();
        d2d_context->BeginDraw();
        d2d_context->Clear(color(0.0F, 0.0F, 0.0F, 0.0F));
        d2d_context->PushLayer(layer_parameters, clip_layer.Get());
        d2d_context->DrawBitmap(source_bitmap.Get(), destination, 1.0F, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                                source);
        d2d_context->PopLayer();

        if (settings.shape == magnifier::Shape::circle)
        {
            const auto outline = D2D1::Ellipse({size / 2.0F, size / 2.0F}, size / 2.0F - 4.0F, size / 2.0F - 4.0F);
            d2d_context->DrawEllipse(outline, dark_brush.Get(), 7.0F);
            d2d_context->DrawEllipse(outline, light_brush.Get(), 2.0F);
        }
        else
        {
            const auto outline = D2D1::RoundedRect({4.0F, 4.0F, size - 4.0F, size - 4.0F},
                                                   std::max(18.0F, size * 0.12F), std::max(18.0F, size * 0.12F));
            d2d_context->DrawRoundedRectangle(outline, dark_brush.Get(), 7.0F);
            d2d_context->DrawRoundedRectangle(outline, light_brush.Get(), 2.0F);
        }
        if (FAILED(d2d_context->EndDraw()))
        {
            return false;
        }
        const HRESULT present_result = swap_chain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
        if (present_result == DXGI_ERROR_WAS_STILL_DRAWING)
        {
            return true;
        }
        if (FAILED(present_result))
        {
            return false;
        }
        return SUCCEEDED(composition_device->Commit());
    }
};

MagnifierWindow::MagnifierWindow() noexcept
{
    try
    {
        impl_ = std::make_unique<Impl>();
    }
    catch (...)
    {
        impl_.reset();
    }
}

MagnifierWindow::~MagnifierWindow()
{
    if (impl_ != nullptr)
    {
        impl_->reset_graphics();
        if (impl_->window != nullptr)
        {
            DestroyWindow(impl_->window);
        }
        if (impl_->window_class != 0)
        {
            UnregisterClassW(magnifier_class_name, impl_->instance);
        }
    }
    // unique_ptr releases Impl here; destructor must be defined in the
    // translation unit where Impl is complete (i.e. this .cpp file).
}

bool MagnifierWindow::initialize(const HINSTANCE instance) noexcept
{
    if (impl_ == nullptr)
    {
        return false;
    }
    impl_->instance = instance;
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = magnifier_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = magnifier_class_name;
    impl_->window_class = RegisterClassExW(&window_class);
    if (impl_->window_class == 0)
    {
        return false;
    }
    impl_->window =
        CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP,
                        magnifier_class_name, nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    return impl_->window != nullptr;
}

void MagnifierWindow::configure(const magnifier::Settings& settings) noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }
    if (impl_->settings.diameter_dip != settings.diameter_dip || impl_->settings.shape != settings.shape)
    {
        impl_->reset_graphics();
    }
    impl_->settings = settings;
    if (!settings.enabled)
    {
        hide();
    }
}

bool MagnifierWindow::render(DesktopDuplicationCapture& backend, const POINT cursor, const UINT dpi) noexcept
{
    if (impl_ == nullptr || impl_->window == nullptr || !impl_->settings.enabled || !magnifier::valid(impl_->settings))
    {
        return false;
    }
    const auto bounds = backend.output_bounds();
    const magnifier::Rect output{bounds.left, bounds.top, bounds.right, bounds.bottom};
    const auto geometry = magnifier::compute_geometry({cursor.x, cursor.y}, output, dpi, impl_->settings.diameter_dip,
                                                      impl_->settings.zoom_percent);
    const auto size = geometry.destination.width();
    if (impl_->bound_device.Get() != backend.device() || impl_->surface_size != size)
    {
        if (!impl_->create_graphics(backend.device(), size))
        {
            return false;
        }
    }

    ComPtr<ID3D11Texture2D> frame;
    const auto result = backend.acquire_frame(&frame, 0);
    if (result == capture::FrameResult::no_frame)
    {
        return impl_->visible;
    }
    if (result != capture::FrameResult::frame)
    {
        return false;
    }
    const bool drawn = impl_->draw(frame.Get(), geometry, bounds);
    backend.release_frame();
    if (!drawn)
    {
        impl_->reset_graphics();
        return false;
    }

    const RECT destination{geometry.destination.left, geometry.destination.top, geometry.destination.right,
                           geometry.destination.bottom};
    if (!clip_window_to_output(impl_->window, destination, bounds) ||
        SetWindowPos(impl_->window, HWND_TOPMOST, destination.left, destination.top, size, size,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW) == FALSE)
    {
        hide();
        return false;
    }
    impl_->visible = true;
    return true;
}

void MagnifierWindow::hide() noexcept
{
    if (impl_ != nullptr && impl_->window != nullptr)
    {
        ShowWindow(impl_->window, SW_HIDE);
        impl_->visible = false;
    }
}

bool MagnifierWindow::visible() const noexcept
{
    return impl_ != nullptr && impl_->visible;
}

HWND MagnifierWindow::window() const noexcept
{
    return impl_ == nullptr ? nullptr : impl_->window;
}
} // namespace zmouse::platform

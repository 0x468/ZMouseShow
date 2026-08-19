#include "zmouse/platform/magnifier_window.hpp"

#include "zmouse/magnifier/geometry.hpp"
#include "zmouse/platform/desktop_duplication_capture.hpp"
#include "zmouse/render/pointer_shape.hpp"
#include <algorithm>
#include <cstdint>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <magnification.h>
#include <new>
#include <vector>
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
    ComPtr<ID3D11DeviceContext> d3d_context;
    ComPtr<ID3D11Texture2D> cached_frame;
    ComPtr<IDXGISwapChain1> swap_chain;
    ComPtr<ID2D1Factory1> d2d_factory;
    ComPtr<ID2D1Device> d2d_device;
    ComPtr<ID2D1DeviceContext> d2d_context;
    ComPtr<ID2D1Bitmap1> target_bitmap;
    ComPtr<ID2D1Geometry> clip_geometry;
    ComPtr<ID2D1Layer> clip_layer;
    ComPtr<ID2D1SolidColorBrush> dark_brush;
    ComPtr<ID2D1SolidColorBrush> light_brush;
    ComPtr<ID3D11Texture2D> pointer_staging;
    ComPtr<ID2D1Bitmap1> pointer_bitmap;
    std::vector<std::uint32_t> pointer_background;
    std::vector<std::uint32_t> pointer_composed;
    UINT pointer_width{};
    UINT pointer_height{};
    ComPtr<IDCompositionDevice> composition_device;
    ComPtr<IDCompositionTarget> composition_target;
    ComPtr<IDCompositionVisual> composition_visual;
    std::int32_t surface_size{};
    bool visible{};
    bool magnification_initialized{};
    bool system_cursor_hidden{};

    void restore_system_cursor() noexcept
    {
        if (system_cursor_hidden && MagShowSystemCursor(TRUE) != FALSE)
        {
            system_cursor_hidden = false;
        }
    }

    [[nodiscard]] bool hide_system_cursor() noexcept
    {
        if (system_cursor_hidden)
        {
            return true;
        }
        if (!magnification_initialized)
        {
            if (MagInitialize() == FALSE)
            {
                return false;
            }
            magnification_initialized = true;
        }
        if (MagShowSystemCursor(FALSE) == FALSE)
        {
            return false;
        }
        system_cursor_hidden = true;
        return true;
    }

    void reset_pointer_resources() noexcept
    {
        pointer_bitmap.Reset();
        pointer_staging.Reset();
        pointer_background.clear();
        pointer_composed.clear();
        pointer_width = 0;
        pointer_height = 0;
    }

    void reset_graphics() noexcept
    {
        restore_system_cursor();
        reset_pointer_resources();
        cached_frame.Reset();
        d3d_context.Reset();
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
        bound_device->GetImmediateContext(&d3d_context);
        if (d3d_context == nullptr)
        {
            reset_graphics();
            return false;
        }

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

    [[nodiscard]] bool update_cached_frame(ID3D11Texture2D* frame) noexcept
    {
        if (frame == nullptr)
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC source_descriptor{};
        frame->GetDesc(&source_descriptor);
        bool create_cache = cached_frame == nullptr;
        if (!create_cache)
        {
            D3D11_TEXTURE2D_DESC cache_descriptor{};
            cached_frame->GetDesc(&cache_descriptor);
            create_cache = cache_descriptor.Width != source_descriptor.Width ||
                           cache_descriptor.Height != source_descriptor.Height ||
                           cache_descriptor.Format != source_descriptor.Format;
        }
        if (create_cache)
        {
            cached_frame.Reset();
            auto cache_descriptor = source_descriptor;
            cache_descriptor.Usage = D3D11_USAGE_DEFAULT;
            cache_descriptor.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            cache_descriptor.CPUAccessFlags = 0;
            cache_descriptor.MiscFlags = 0;
            if (FAILED(bound_device->CreateTexture2D(&cache_descriptor, nullptr, &cached_frame)))
            {
                return false;
            }
        }
        d3d_context->CopyResource(cached_frame.Get(), frame);
        return true;
    }

    struct PreparedPointer final
    {
        bool embedded{};
        bool drawable{};
        D2D1_RECT_F destination{};
    };

    [[nodiscard]] bool ensure_pointer_resources(const render::PointerShape& shape) noexcept
    {
        constexpr UINT maximum_pointer_extent = 1'024;
        if (shape.width == 0 || shape.height == 0 || shape.width > maximum_pointer_extent ||
            shape.height > maximum_pointer_extent)
        {
            return false;
        }
        if (pointer_staging != nullptr && pointer_bitmap != nullptr && pointer_width == shape.width &&
            pointer_height == shape.height)
        {
            return true;
        }

        reset_pointer_resources();
        try
        {
            const auto count = static_cast<std::size_t>(shape.width) * shape.height;
            pointer_background.resize(count);
            pointer_composed.resize(count);
        }
        catch (...)
        {
            reset_pointer_resources();
            return false;
        }

        D3D11_TEXTURE2D_DESC staging_descriptor{};
        staging_descriptor.Width = shape.width;
        staging_descriptor.Height = shape.height;
        staging_descriptor.MipLevels = 1;
        staging_descriptor.ArraySize = 1;
        staging_descriptor.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        staging_descriptor.SampleDesc.Count = 1;
        staging_descriptor.Usage = D3D11_USAGE_STAGING;
        staging_descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(bound_device->CreateTexture2D(&staging_descriptor, nullptr, &pointer_staging)))
        {
            reset_pointer_resources();
            return false;
        }

        const auto bitmap_properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(d2d_context->CreateBitmap(D2D1::SizeU(shape.width, shape.height), nullptr,
                                             shape.width * sizeof(std::uint32_t), &bitmap_properties, &pointer_bitmap)))
        {
            reset_pointer_resources();
            return false;
        }
        pointer_width = shape.width;
        pointer_height = shape.height;
        return true;
    }

    [[nodiscard]] bool prepare_pointer(ID3D11Texture2D* frame, const DuplicationPointer& pointer,
                                       const magnifier::Geometry& geometry, const RECT output_bounds,
                                       const POINT cursor, PreparedPointer& prepared) noexcept
    {
        prepared = {};
        if (!pointer.visible && !system_cursor_hidden)
        {
            CURSORINFO cursor_info{.cbSize = sizeof(CURSORINFO)};
            if (GetCursorInfo(&cursor_info) == FALSE || (cursor_info.flags & CURSOR_SHOWING) == 0)
            {
                return true;
            }
            prepared.embedded = true;
            return true;
        }
        if (!pointer.has_shape || !ensure_pointer_resources(pointer.shape))
        {
            return false;
        }

        const LONG output_width = output_bounds.right - output_bounds.left;
        const LONG output_height = output_bounds.bottom - output_bounds.top;
        const LONG pointer_left =
            system_cursor_hidden ? cursor.x - output_bounds.left - pointer.shape.hotspot_x : pointer.position.x;
        const LONG pointer_top =
            system_cursor_hidden ? cursor.y - output_bounds.top - pointer.shape.hotspot_y : pointer.position.y;
        const LONG copy_left = (std::max)(0L, pointer_left);
        const LONG copy_top = (std::max)(0L, pointer_top);
        const LONG copy_right = (std::min)(output_width, pointer_left + static_cast<LONG>(pointer.shape.width));
        const LONG copy_bottom = (std::min)(output_height, pointer_top + static_cast<LONG>(pointer.shape.height));
        if (copy_right <= copy_left || copy_bottom <= copy_top)
        {
            return false;
        }

        const UINT destination_x = static_cast<UINT>(copy_left - pointer_left);
        const UINT destination_y = static_cast<UINT>(copy_top - pointer_top);
        const D3D11_BOX source_box{
            .left = static_cast<UINT>(copy_left),
            .top = static_cast<UINT>(copy_top),
            .front = 0,
            .right = static_cast<UINT>(copy_right),
            .bottom = static_cast<UINT>(copy_bottom),
            .back = 1,
        };
        d3d_context->CopySubresourceRegion(pointer_staging.Get(), 0, destination_x, destination_y, 0, frame, 0,
                                           &source_box);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(d3d_context->Map(pointer_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return false;
        }
        std::fill(pointer_background.begin(), pointer_background.end(), 0xFF000000U);
        const auto copy_width = static_cast<std::size_t>(copy_right - copy_left);
        const auto copy_height = static_cast<std::size_t>(copy_bottom - copy_top);
        for (std::size_t row = 0; row < copy_height; ++row)
        {
            const auto* source_row = reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::byte*>(mapped.pData) + (row + destination_y) * mapped.RowPitch);
            auto destination = pointer_background.begin() +
                               static_cast<std::ptrdiff_t>((row + destination_y) * pointer.shape.width + destination_x);
            std::copy_n(source_row + destination_x, copy_width, destination);
        }
        d3d_context->Unmap(pointer_staging.Get(), 0);

        if (!render::compose_pointer_shape(pointer.shape, pointer_background, pointer_composed))
        {
            return false;
        }
        for (std::size_t index = 0; index < pointer_composed.size(); ++index)
        {
            if ((pointer_composed[index] & 0x00FFFFFFU) == (pointer_background[index] & 0x00FFFFFFU))
            {
                pointer_composed[index] = 0;
            }
            else
            {
                pointer_composed[index] |= 0xFF000000U;
            }
        }
        if (FAILED(pointer_bitmap->CopyFromMemory(nullptr, pointer_composed.data(),
                                                  pointer.shape.width * sizeof(std::uint32_t))))
        {
            return false;
        }

        const float scale = static_cast<float>(surface_size) / static_cast<float>(geometry.source.width());
        const float absolute_left = static_cast<float>(output_bounds.left + pointer_left);
        const float absolute_top = static_cast<float>(output_bounds.top + pointer_top);
        prepared.drawable = true;
        prepared.destination = {
            (absolute_left - static_cast<float>(geometry.source.left)) * scale,
            (absolute_top - static_cast<float>(geometry.source.top)) * scale,
            (absolute_left + static_cast<float>(pointer.shape.width) - static_cast<float>(geometry.source.left)) *
                scale,
            (absolute_top + static_cast<float>(pointer.shape.height) - static_cast<float>(geometry.source.top)) * scale,
        };
        return true;
    }

    [[nodiscard]] bool draw(ID3D11Texture2D* frame, const DuplicationPointer& pointer,
                            const magnifier::Geometry& geometry, const RECT output_bounds, const POINT cursor) noexcept
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

        PreparedPointer prepared_pointer;
        if (!prepare_pointer(frame, pointer, geometry, output_bounds, cursor, prepared_pointer))
        {
            restore_system_cursor();
            return false;
        }
        if ((prepared_pointer.embedded || prepared_pointer.drawable) && !hide_system_cursor())
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
        if (prepared_pointer.drawable)
        {
            d2d_context->DrawBitmap(pointer_bitmap.Get(), prepared_pointer.destination, 1.0F,
                                    D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        }
        d2d_context->PopLayer();

        const float dark_stroke = settings.edge_effect == magnifier::EdgeEffect::subtle ? 9.0F : 7.0F;
        const float light_stroke = settings.edge_effect == magnifier::EdgeEffect::subtle ? 3.0F : 2.0F;
        if (settings.shape == magnifier::Shape::circle)
        {
            const auto outline = D2D1::Ellipse({size / 2.0F, size / 2.0F}, size / 2.0F - 4.0F, size / 2.0F - 4.0F);
            d2d_context->DrawEllipse(outline, dark_brush.Get(), dark_stroke);
            d2d_context->DrawEllipse(outline, light_brush.Get(), light_stroke);
        }
        else
        {
            const auto outline = D2D1::RoundedRect({4.0F, 4.0F, size - 4.0F, size - 4.0F},
                                                   std::max(18.0F, size * 0.12F), std::max(18.0F, size * 0.12F));
            d2d_context->DrawRoundedRectangle(outline, dark_brush.Get(), dark_stroke);
            d2d_context->DrawRoundedRectangle(outline, light_brush.Get(), light_stroke);
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
        if (impl_->magnification_initialized)
        {
            static_cast<void>(MagUninitialize());
            impl_->magnification_initialized = false;
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

MagnifierRenderResult MagnifierWindow::render(DesktopDuplicationCapture& backend, const POINT cursor,
                                              const UINT dpi) noexcept
{
    if (impl_ == nullptr || impl_->window == nullptr || !impl_->settings.enabled || !magnifier::valid(impl_->settings))
    {
        return MagnifierRenderResult::render_failure;
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
            return MagnifierRenderResult::render_failure;
        }
    }

    ComPtr<ID3D11Texture2D> frame;
    const auto result = backend.acquire_frame(&frame, 0);
    if (result == capture::FrameResult::no_frame)
    {
        if (impl_->cached_frame == nullptr)
        {
            return MagnifierRenderResult::no_frame;
        }
    }
    else if (result != capture::FrameResult::frame)
    {
        return MagnifierRenderResult::capture_failure;
    }
    else
    {
        const bool cached = impl_->update_cached_frame(frame.Get());
        backend.release_frame();
        if (!cached)
        {
            impl_->reset_graphics();
            return MagnifierRenderResult::render_failure;
        }
    }
    const bool drawn = impl_->draw(impl_->cached_frame.Get(), backend.pointer(), geometry, bounds, cursor);
    if (!drawn)
    {
        impl_->reset_graphics();
        return MagnifierRenderResult::render_failure;
    }

    const RECT destination{geometry.destination.left, geometry.destination.top, geometry.destination.right,
                           geometry.destination.bottom};
    if (!clip_window_to_output(impl_->window, destination, bounds) ||
        SetWindowPos(impl_->window, HWND_TOPMOST, destination.left, destination.top, size, size,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW) == FALSE)
    {
        hide();
        return MagnifierRenderResult::render_failure;
    }
    impl_->visible = true;
    return MagnifierRenderResult::presented;
}

void MagnifierWindow::hide() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->restore_system_cursor();
        if (impl_->window != nullptr)
        {
            ShowWindow(impl_->window, SW_HIDE);
        }
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

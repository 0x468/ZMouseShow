#include "zmouse/capture/capture_backend.hpp"
#include "zmouse/config/settings.hpp"
#include "zmouse/magnifier/geometry.hpp"
#include <cassert>

int main()
{
    using namespace zmouse::magnifier;
    const Rect output{0, 0, 1920, 1080};
    const auto edge = compute_geometry({0, 0}, output, 96, 280, 200);
    assert(edge.source.left == 0 && edge.source.top == 0);
    assert(edge.source.right > 0 && edge.source.bottom > 0);
    assert(edge.destination.width() == 280 && edge.destination.height() == 280);

    const auto high_dpi = compute_geometry({1919, 1079}, output, 144, 160, 400);
    assert(high_dpi.source.right <= output.right && high_dpi.source.bottom <= output.bottom);
    assert(high_dpi.destination.right > output.right && high_dpi.destination.bottom > output.bottom);
    assert(high_dpi.scale == 4.0F);

    zmouse::capture::Lifecycle lifecycle;
    lifecycle.enable();
    lifecycle.mark_ready();
    assert(lifecycle.active());
    lifecycle.mark_failure(zmouse::capture::FailureCategory::access_lost);
    assert(!lifecycle.active());
    assert(lifecycle.diagnostics().device_rebuild_count == 1);
    assert(zmouse::capture::failure_name(zmouse::capture::FailureCategory::protected_content) == "protected_content");

    const auto settings = zmouse::config::parse_toml(R"(
[overlay]
dim_enabled = false
[magnifier]
enabled = true
zoom_percent = 350
diameter_dip = 640
shape = "rounded_rectangle"
follow_mode = "centered"
edge_effect = "off"
)");
    assert(settings);
    assert(!settings->dim_enabled && settings->magnifier.enabled);
    assert(settings->magnifier.zoom_percent == 350 && settings->magnifier.diameter_dip == 640);
    assert(settings->magnifier.shape == Shape::rounded_rectangle);
    assert(settings->magnifier.edge_effect == EdgeEffect::off);
}

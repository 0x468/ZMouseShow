#include "zmouse/capture/capture_backend.hpp"
#include "zmouse/config/settings.hpp"
#include "zmouse/magnifier/geometry.hpp"
#include "zmouse/overlay/geometry.hpp"
#include <cstdlib>
#include <iostream>
#include <string_view>

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
} // namespace

int main()
{
    using namespace zmouse::magnifier;
    const Rect output{0, 0, 1920, 1080};
    const auto edge = compute_geometry({0, 0}, output, 96, 280, 200);
    check(edge.source.left == 0 && edge.source.top == 0, "edge cursor: source origin at 0,0");
    check(edge.source.right > 0 && edge.source.bottom > 0, "edge cursor: source has positive extent");
    check(edge.destination.width() == 280 && edge.destination.height() == 280, "edge cursor: destination matches diameter_dip");

    const auto high_dpi = compute_geometry({1919, 1079}, output, 144, 160, 400);
    check(high_dpi.source.right <= output.right && high_dpi.source.bottom <= output.bottom,
          "high DPI edge: source clamped to output");
    check(high_dpi.destination.right > output.right && high_dpi.destination.bottom > output.bottom,
          "high DPI edge: destination extends past output");
    check(high_dpi.scale == 4.0F, "high DPI edge: scale matches zoom_percent");

    zmouse::capture::Lifecycle lifecycle;
    lifecycle.enable();
    lifecycle.mark_ready();
    check(lifecycle.active(), "lifecycle is active after enable+mark_ready");
    lifecycle.mark_failure(zmouse::capture::FailureCategory::access_lost);
    check(!lifecycle.active(), "lifecycle deactivated on access_lost");
    check(lifecycle.diagnostics().device_rebuild_count == 1, "device_rebuild_count incremented on access_lost");
    check(zmouse::capture::failure_name(zmouse::capture::FailureCategory::protected_content) == "protected_content",
          "failure_name returns correct string");

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
    check(settings.has_value(), "magnifier TOML parses successfully");
    check(!settings->dim_enabled && settings->magnifier.enabled, "dim disabled, magnifier enabled");
    check(settings->magnifier.zoom_percent == 350 && settings->magnifier.diameter_dip == 640,
          "zoom and diameter parsed correctly");
    check(settings->magnifier.shape == Shape::rounded_rectangle, "shape is rounded_rectangle");
    check(settings->magnifier.edge_effect == EdgeEffect::off, "edge_effect is off");

    // dip_to_pixels edge cases
    check(zmouse::overlay::dip_to_pixels(120, 0) >= 1, "dip_to_pixels with dpi=0 returns positive value");
    check(zmouse::overlay::dip_to_pixels(120, 96) == 120, "dip_to_pixels at 96 DPI is identity");
    check(zmouse::magnifier::dip_to_pixels(120, 0) >= 1, "magnifier dip_to_pixels with dpi=0 returns positive");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

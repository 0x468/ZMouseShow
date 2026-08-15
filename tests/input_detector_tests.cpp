#include "zmouse/input/double_ctrl_detector.hpp"
#include "zmouse/input/shake_detector.hpp"
#include "zmouse/overlay/geometry.hpp"
#include "zmouse/overlay/locator_animation.hpp"
#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
int failures = 0;

void check(const bool condition, const std::string_view description)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}

void test_clean_double_ctrl_triggers()
{
    zmouse::input::DoubleCtrlDetector detector;
    check(!detector.process({zmouse::input::KeyKind::left_control, true, false, 1000}, false, false),
          "first Ctrl press is only a candidate");
    check(!detector.process({zmouse::input::KeyKind::left_control, false, false, 1050}, false, false),
          "first Ctrl release does not trigger");
    check(detector.process({zmouse::input::KeyKind::left_control, true, false, 1200}, false, false),
          "clean second Ctrl press triggers");
    check(!detector.process({zmouse::input::KeyKind::left_control, false, false, 1250}, false, false),
          "trigger Ctrl release does not retrigger");
}

void test_ctrl_chords_cancel_candidate()
{
    zmouse::input::DoubleCtrlDetector detector;
    static_cast<void>(detector.process({zmouse::input::KeyKind::left_control, true, false, 1000}, false, false));
    static_cast<void>(detector.process({zmouse::input::KeyKind::left_control, false, false, 1050}, false, false));
    static_cast<void>(detector.process({zmouse::input::KeyKind::other, true, false, 1100}, true, false));
    check(!detector.process({zmouse::input::KeyKind::left_control, true, false, 1200}, false, false),
          "Ctrl+C or Ctrl+V sequence cannot complete a double-Ctrl gesture");
}

void test_other_held_key_and_mouse_button_block_ctrl()
{
    zmouse::input::DoubleCtrlDetector detector;
    check(!detector.process({zmouse::input::KeyKind::left_control, true, false, 1000}, true, false),
          "an already-held keyboard key blocks candidate start");

    detector.reset();
    static_cast<void>(detector.process({zmouse::input::KeyKind::left_control, true, false, 2000}, false, false));
    static_cast<void>(detector.process({zmouse::input::KeyKind::left_control, false, false, 2050}, false, false));
    detector.on_mouse_button_event();
    check(!detector.process({zmouse::input::KeyKind::left_control, true, false, 2200}, false, false),
          "a mouse button event cancels a pending candidate");
}

void test_repeat_and_timing_do_not_trigger()
{
    zmouse::input::DoubleCtrlDetector detector;
    static_cast<void>(detector.process({zmouse::input::KeyKind::left_control, true, false, 1000}, false, false));
    check(!detector.process({zmouse::input::KeyKind::left_control, true, true, 1030}, false, false),
          "keyboard auto-repeat is ignored");
    static_cast<void>(detector.process({zmouse::input::KeyKind::left_control, false, false, 1060}, false, false));
    check(!detector.process({zmouse::input::KeyKind::left_control, true, false, 1700}, false, false),
          "a Ctrl press after the maximum interval starts a new candidate");
}

zmouse::input::ShakeDetector make_test_shake_detector()
{
    return zmouse::input::ShakeDetector({
        .interval_ms = 1000,
        .minimum_distance = 500.0,
        .minimum_path_to_diagonal_ratio = 4.0,
        .minimum_reversals = 3,
        .cooldown_ms = 500,
    });
}

void test_single_direction_is_not_a_shake()
{
    auto detector = make_test_shake_detector();
    bool triggered = false;
    for (std::uint64_t index = 0; index < 6; ++index)
    {
        triggered = detector.process({100, 0, 100 + index * 50}, false) || triggered;
    }
    check(!triggered, "fast single-direction travel is not a shake");
}

void test_reversals_trigger_shake()
{
    auto detector = make_test_shake_detector();
    bool triggered = false;
    for (std::uint64_t index = 0; index < 7; ++index)
    {
        const std::int32_t dx = (index % 2 == 0) ? 100 : -100;
        triggered = detector.process({dx, 0, 100 + index * 50}, false) || triggered;
    }
    check(triggered, "alternating movement with enough path and reversals triggers");
}

void test_mouse_buttons_clear_shake_history()
{
    auto detector = make_test_shake_detector();
    static_cast<void>(detector.process({100, 0, 100}, false));
    static_cast<void>(detector.process({-100, 0, 150}, false));
    static_cast<void>(detector.process({100, 0, 200}, false));
    detector.on_mouse_buttons_changed(true);
    check(!detector.process({-100, 0, 250}, false), "movement while a mouse button is held is ignored");
    detector.on_mouse_buttons_changed(false);
    check(!detector.process({100, 0, 300}, false), "release starts a fresh movement history");
    check(!detector.process({-100, 0, 350}, false), "pre-drag movement is not retained after release");
}

void test_active_overlay_blocks_and_clears_shake_history()
{
    auto detector = make_test_shake_detector();
    static_cast<void>(detector.process({100, 0, 100}, false));
    static_cast<void>(detector.process({-100, 0, 150}, false));
    static_cast<void>(detector.process({100, 0, 200}, false));
    check(!detector.process({-100, 0, 250}, true), "an active overlay cannot be retriggered by shaking");
    check(!detector.process({100, 0, 300}, false), "movement after the overlay starts with fresh history");
}

void test_locator_animation_transitions()
{
    zmouse::overlay::LocatorAnimation animation;
    check(animation.phase() == zmouse::overlay::AnimationPhase::hidden, "animation starts hidden");

    animation.show(1'000);
    const auto first = animation.frame(1'000);
    check(first.surface_visible && first.needs_more_frames, "show starts an active transition");
    check(first.dim_progress == 0.0 && first.ring_scale == 4.0, "show starts transparent with a large ring");

    const auto middle = animation.frame(1'110);
    check(middle.dim_progress > 0.0 && middle.dim_progress < 1.0, "fade-in progresses over time");
    check(middle.ring_scale > 1.0 && middle.ring_scale < 4.0, "the pulse ring contracts over time");

    const auto shown = animation.frame(1'220);
    check(shown.surface_visible && !shown.needs_more_frames, "fade-in reaches a stable visible state");
    check(shown.dim_progress == 1.0 && shown.ring_scale == 1.0, "stable frame reaches final opacity and scale");

    animation.hide(2'000);
    const auto fading = animation.frame(2'110);
    check(fading.surface_visible && fading.needs_more_frames, "hide starts a fade-out transition");
    check(fading.dim_progress > 0.0 && fading.dim_progress < 1.0, "fade-out reduces dim opacity");

    const auto hidden = animation.frame(2'220);
    check(!hidden.surface_visible && !hidden.needs_more_frames, "fade-out finishes hidden");
    check(animation.phase() == zmouse::overlay::AnimationPhase::hidden, "finished fade-out resets the state");
}

void test_locator_animation_can_reverse_during_fade_in()
{
    zmouse::overlay::LocatorAnimation animation;
    animation.show(100);
    const double partial_opacity = animation.frame(150).dim_progress;
    animation.hide(150);
    const auto first_fade_frame = animation.frame(150);
    check(std::abs(first_fade_frame.dim_progress - partial_opacity) < 0.001,
          "fade-out continues from the current fade-in opacity");
    check(!animation.frame(370).surface_visible, "an interrupted fade-in still finishes hidden");
}

void test_overlay_geometry_handles_negative_monitor_coordinates()
{
    constexpr zmouse::overlay::Rect left_monitor{
        .left = -2560,
        .top = -120,
        .right = 0,
        .bottom = 1320,
    };
    constexpr zmouse::overlay::Point cursor{-1280, 600};
    constexpr auto hole = zmouse::overlay::hole_bounds_in_monitor(cursor, left_monitor, 150);

    check(hole.left == 1130 && hole.top == 570 && hole.right == 1430 && hole.bottom == 870,
          "screen coordinates convert to monitor-local hole bounds");
    check(zmouse::overlay::dip_to_pixels(120, 120) == 150, "DIP radius scales at 125 percent DPI");
}
} // namespace

int main()
{
    test_clean_double_ctrl_triggers();
    test_ctrl_chords_cancel_candidate();
    test_other_held_key_and_mouse_button_block_ctrl();
    test_repeat_and_timing_do_not_trigger();
    test_single_direction_is_not_a_shake();
    test_reversals_trigger_shake();
    test_mouse_buttons_clear_shake_history();
    test_active_overlay_blocks_and_clears_shake_history();
    test_locator_animation_transitions();
    test_locator_animation_can_reverse_during_fade_in();
    test_overlay_geometry_handles_negative_monitor_coordinates();

    if (failures == 0)
    {
        std::cout << "All input detector tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}

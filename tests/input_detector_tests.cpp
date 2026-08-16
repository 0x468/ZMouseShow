#include "zmouse/input/double_ctrl_detector.hpp"
#include "zmouse/input/hotkey_detector.hpp"
#include "zmouse/input/overlay_input_rules.hpp"
#include "zmouse/input/shake_detector.hpp"
#include "zmouse/overlay/geometry.hpp"
#include "zmouse/overlay/locator_animation.hpp"
#include <array>
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

void test_overlay_trigger_routing_state_matrix()
{
    using zmouse::input::OverlayInputState;
    using zmouse::input::triggers_armed;

    check(triggers_armed({}), "triggers are armed while the application is idle");
    check(!triggers_armed(OverlayInputState{.paused = true}), "pause blocks all triggers");
    check(!triggers_armed(OverlayInputState{.activation_pending = true}),
          "a pending activation blocks duplicate triggers");
    check(!triggers_armed(OverlayInputState{.overlay_visible = true}), "a visible overlay blocks overlapping triggers");
    check(!triggers_armed(OverlayInputState{.tray_menu_open = true}), "the tray menu blocks all triggers");
    check(!triggers_armed(OverlayInputState{.settings_dialog_open = true}), "the settings dialog blocks all triggers");
}

void test_overlay_key_release_does_not_dismiss()
{
    using zmouse::input::should_dismiss_overlay_for_key_event;

    check(!should_dismiss_overlay_for_key_event(true, false, false),
          "releasing a trigger key does not dismiss the overlay");
    check(!should_dismiss_overlay_for_key_event(true, true, true), "a repeated key press does not dismiss the overlay");
    check(should_dismiss_overlay_for_key_event(true, true, false), "a new key press dismisses the visible overlay");
    check(!should_dismiss_overlay_for_key_event(false, true, false),
          "keyboard input does not request dismissal while the overlay is hidden");
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

void test_disabled_double_ctrl_never_starts_a_candidate()
{
    zmouse::input::DoubleCtrlDetector detector({.enabled = false});
    check(!detector.process({zmouse::input::KeyKind::left_control, true, false, 1'000}, false, false),
          "disabled double Ctrl ignores the first press");
    check(!detector.process({zmouse::input::KeyKind::left_control, false, false, 1'050}, false, false),
          "disabled double Ctrl ignores the first release");
    check(!detector.process({zmouse::input::KeyKind::left_control, true, false, 1'200}, false, false),
          "disabled double Ctrl ignores the second press");
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

void test_right_ctrl_and_either_side_configuration()
{
    zmouse::input::DoubleCtrlDetector right_detector({.side = zmouse::input::ControlSide::right});
    check(!right_detector.process({zmouse::input::KeyKind::left_control, true, false, 1'000}, false, false),
          "left Ctrl is ignored when right Ctrl is configured");
    check(!right_detector.process({zmouse::input::KeyKind::right_control, true, false, 2'000}, false, false),
          "first right Ctrl press starts a candidate");
    static_cast<void>(
        right_detector.process({zmouse::input::KeyKind::right_control, false, false, 2'050}, false, false));
    check(right_detector.process({zmouse::input::KeyKind::right_control, true, false, 2'200}, false, false),
          "double right Ctrl triggers when configured");

    zmouse::input::DoubleCtrlDetector either_detector({.side = zmouse::input::ControlSide::either});
    static_cast<void>(
        either_detector.process({zmouse::input::KeyKind::left_control, true, false, 3'000}, false, false));
    static_cast<void>(
        either_detector.process({zmouse::input::KeyKind::left_control, false, false, 3'050}, false, false));
    check(!either_detector.process({zmouse::input::KeyKind::right_control, true, false, 3'200}, false, false),
          "mixing Ctrl sides does not complete a gesture");
}

void test_custom_hotkey_requires_an_exact_clean_chord()
{
    zmouse::input::HotkeyDetector detector(
        {.enabled = true, .key = 0x7AU, .control = true, .alt = true, .shift = false, .windows = false});
    const zmouse::input::HotkeyEvent valid{
        .key = 0x7AU,
        .pressed = true,
        .repeated = false,
        .control_down = true,
        .alt_down = true,
        .shift_down = false,
        .windows_down = false,
        .other_key_down = false,
    };
    check(detector.process(valid), "the configured Ctrl+Alt+F11 chord triggers");

    auto repeated = valid;
    repeated.repeated = true;
    check(!detector.process(repeated), "hotkey auto-repeat is ignored");

    auto extra_modifier = valid;
    extra_modifier.shift_down = true;
    check(!detector.process(extra_modifier), "an extra modifier blocks the hotkey");

    auto other_key = valid;
    other_key.other_key_down = true;
    check(!detector.process(other_key), "an unrelated held key blocks the hotkey");
}

void test_hotkey_safety_validation()
{
    const auto default_hotkey = zmouse::input::validate_hotkey_config(
        {.enabled = true, .key = 0x7AU, .control = true, .alt = true, .shift = false, .windows = false});
    check(default_hotkey.accepted && !default_hotkey.requires_confirmation,
          "default Ctrl+Alt+F11 hotkey is accepted without a warning");

    const auto debugger_reserved = zmouse::input::validate_hotkey_config(
        {.enabled = true, .key = 0x7BU, .control = true, .alt = true, .shift = false, .windows = false});
    check(!debugger_reserved.accepted &&
              debugger_reserved.issue == zmouse::input::HotkeyIssue::reserved_system_shortcut,
          "F12 is rejected because Windows reserves it for the debugger");

    const auto weak_letter = zmouse::input::validate_hotkey_config(
        {.enabled = true, .key = 'S', .control = true, .alt = false, .shift = false, .windows = false});
    check(!weak_letter.accepted && weak_letter.issue == zmouse::input::HotkeyIssue::insufficient_modifiers,
          "a single-modifier letter shortcut is rejected");

    const auto common_chord = zmouse::input::validate_hotkey_config(
        {.enabled = true, .key = 'S', .control = true, .alt = false, .shift = true, .windows = false});
    check(common_chord.accepted && common_chord.requires_confirmation,
          "a common application shortcut requires confirmation");

    const auto reserved_chord = zmouse::input::validate_hotkey_config(
        {.enabled = true, .key = 'L', .control = true, .alt = false, .shift = false, .windows = true});
    check(!reserved_chord.accepted && reserved_chord.issue == zmouse::input::HotkeyIssue::reserved_system_shortcut,
          "a reserved Windows shortcut is rejected");
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

[[nodiscard]] bool feed_default_shake(zmouse::input::ShakeDetector& detector, const std::uint32_t samples_per_leg,
                                      const std::uint64_t timestamp_step_ms, const std::uint64_t start_timestamp = 100)
{
    constexpr std::array<std::int32_t, 6> legs{240, -240, 240, -240, 240, -240};
    std::uint64_t timestamp = start_timestamp;
    std::uint32_t sub_millisecond_sample = 0;
    for (const auto leg : legs)
    {
        const auto movement = leg / static_cast<std::int32_t>(samples_per_leg);
        for (std::uint32_t sample = 0; sample < samples_per_leg; ++sample)
        {
            if (detector.process({movement, 0, timestamp}, false))
            {
                return true;
            }
            if (timestamp_step_ms == 0)
            {
                ++sub_millisecond_sample;
                if (sub_millisecond_sample == 8)
                {
                    ++timestamp;
                    sub_millisecond_sample = 0;
                }
            }
            else
            {
                timestamp += timestamp_step_ms;
            }
        }
    }
    return false;
}

void test_default_shake_is_stable_across_polling_rates()
{
    zmouse::input::ShakeDetector low_polling_detector;
    check(feed_default_shake(low_polling_detector, 1, 8), "default shake triggers with 125 Hz-sized samples");

    zmouse::input::ShakeDetector standard_polling_detector;
    check(feed_default_shake(standard_polling_detector, 8, 1), "default shake triggers with 1000 Hz-sized samples");

    zmouse::input::ShakeDetector high_polling_detector;
    check(feed_default_shake(high_polling_detector, 16, 0), "default shake triggers with high polling-rate samples");
}

void test_default_shake_rejects_jitter_and_slow_motion()
{
    zmouse::input::ShakeDetector jitter_detector;
    bool jitter_triggered = false;
    for (std::uint64_t index = 0; index < 20; ++index)
    {
        const std::int32_t dx = index % 2 == 0 ? 20 : -20;
        jitter_triggered = jitter_detector.process({dx, 0, 100 + index * 10}, false) || jitter_triggered;
    }
    check(!jitter_triggered, "small high-frequency jitter stays below the default distance threshold");

    zmouse::input::ShakeDetector slow_detector;
    bool slow_triggered = false;
    for (std::uint64_t index = 0; index < 8; ++index)
    {
        const std::int32_t dx = index % 2 == 0 ? 240 : -240;
        slow_triggered = slow_detector.process({dx, 0, 100 + index * 300}, false) || slow_triggered;
    }
    check(!slow_triggered, "slow alternating movement is pruned by the default one-second window");
}

void test_default_shake_cooldown_blocks_repeat_activation()
{
    zmouse::input::ShakeDetector detector;
    check(feed_default_shake(detector, 8, 1, 100), "the initial default shake triggers");
    check(!feed_default_shake(detector, 8, 1, 300), "a second shake inside the default cooldown is blocked");
    check(feed_default_shake(detector, 8, 1, 1'500), "a new shake triggers after the default cooldown");
}

void test_shake_rearms_after_overlay_session_ends()
{
    zmouse::input::ShakeDetector detector;
    check(feed_default_shake(detector, 8, 1, 100), "a shake can start the first overlay session");
    detector.reset();
    check(feed_default_shake(detector, 8, 1, 300),
          "ending the overlay session rearms shake detection without toggling the setting");
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
    check(first.dim_progress == 0.0 && first.focus_opacity == 0.0 && first.ripple_scale == 1.0 &&
              first.ripple_opacity == 1.0,
          "show starts with a compact ripple and a transparent focus ring");

    const auto middle = animation.frame(1'090);
    check(middle.dim_progress > 0.0 && middle.dim_progress < 1.0, "fade-in progresses over time");
    check(middle.focus_opacity > 0.0 && middle.focus_opacity < 1.0, "the focus ring fades in over time");
    check(middle.ripple_scale > 1.0 && middle.ripple_scale < 1.75 && middle.ripple_opacity > 0.0 &&
              middle.ripple_opacity < 1.0,
          "the ripple expands and fades over time");

    const auto shown = animation.frame(1'180);
    check(shown.surface_visible && !shown.needs_more_frames, "fade-in reaches a stable visible state");
    check(shown.dim_progress == 1.0 && shown.focus_opacity == 1.0 && shown.ripple_scale == 1.75 &&
              shown.ripple_opacity == 0.0,
          "stable frame keeps the focus ring and retires the ripple");

    animation.hide(2'000);
    const auto fading = animation.frame(2'090);
    check(fading.surface_visible && fading.needs_more_frames, "hide starts a fade-out transition");
    check(fading.dim_progress > 0.0 && fading.dim_progress < 1.0, "fade-out reduces dim opacity");

    const auto hidden = animation.frame(2'180);
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
    test_overlay_trigger_routing_state_matrix();
    test_overlay_key_release_does_not_dismiss();
    test_clean_double_ctrl_triggers();
    test_disabled_double_ctrl_never_starts_a_candidate();
    test_ctrl_chords_cancel_candidate();
    test_other_held_key_and_mouse_button_block_ctrl();
    test_repeat_and_timing_do_not_trigger();
    test_right_ctrl_and_either_side_configuration();
    test_custom_hotkey_requires_an_exact_clean_chord();
    test_hotkey_safety_validation();
    test_single_direction_is_not_a_shake();
    test_reversals_trigger_shake();
    test_default_shake_is_stable_across_polling_rates();
    test_default_shake_rejects_jitter_and_slow_motion();
    test_default_shake_cooldown_blocks_repeat_activation();
    test_shake_rearms_after_overlay_session_ends();
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

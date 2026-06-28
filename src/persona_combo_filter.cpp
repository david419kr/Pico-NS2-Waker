#include "ds5onswitch/persona_combo_filter.h"

namespace ds5onswitch {

PersonaComboFilterResult PersonaComboFilter::process(uint32_t raw_buttons,
                                                     uint32_t now_ms)
{
    return process(raw_buttons, kDs5Ps, kDs5TouchpadClick, now_ms);
}

PersonaComboFilterResult PersonaComboFilter::process(uint32_t raw_buttons,
                                                     uint32_t home_button,
                                                     uint32_t capture_button,
                                                     uint32_t now_ms)
{
    (void)now_ms;

    PersonaComboFilterResult result{};
    result.buttons = raw_buttons;

    const bool home_down = (raw_buttons & home_button) != 0;
    const bool capture_down = (raw_buttons & capture_button) != 0;
    const bool home_release = !home_down && home_prev_;
    const bool capture_edge = capture_down && !capture_prev_;
    bool combo_consumed_capture = false;

    if (suppress_capture_until_release_) {
        if (capture_down) {
            combo_consumed_capture = true;
        } else {
            suppress_capture_until_release_ = false;
        }
    }

    if (home_down) {
        // Home always passes through. Capture clicks while Home is held are
        // persona combo taps, so keep them out of game input as Capture.
        if (capture_edge && tap_count_ < 0xFFu) {
            ++tap_count_;
        }
        if (capture_down) {
            combo_consumed_capture = true;
        }
    } else if (home_release) {
        if (tap_count_ == 1u) {
            result.request = PersonaComboRequest::SwitchPro;
        } else if (tap_count_ >= 2u) {
            result.request = PersonaComboRequest::Switch2Pro;
        }

        if (tap_count_ != 0u) {
            combo_consumed_capture = capture_down;
            suppress_capture_until_release_ = capture_down;
        }
        tap_count_ = 0;
    }

    home_prev_ = home_down;
    capture_prev_ = capture_down;

    if (combo_consumed_capture) {
        result.buttons &= ~capture_button;
    }

    return result;
}

void PersonaComboFilter::reset()
{
    home_prev_ = false;
    capture_prev_ = false;
    suppress_capture_until_release_ = false;
    tap_count_ = 0;
}

} // namespace ds5onswitch

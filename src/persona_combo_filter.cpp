#include "ds5onswitch/persona_combo_filter.h"

namespace ds5onswitch {

PersonaComboFilterResult PersonaComboFilter::process(uint32_t raw_buttons,
                                                     uint32_t now_ms)
{
    (void)now_ms;

    PersonaComboFilterResult result{};
    result.buttons = raw_buttons;

    const bool ps_down = (raw_buttons & kDs5Ps) != 0;
    const bool tp_down = (raw_buttons & kDs5TouchpadClick) != 0;
    const bool ps_release = !ps_down && ps_prev_;
    const bool tp_edge = tp_down && !tp_prev_;
    bool combo_consumed_tp = false;

    if (suppress_tp_until_release_) {
        if (tp_down) {
            combo_consumed_tp = true;
        } else {
            suppress_tp_until_release_ = false;
        }
    }

    if (ps_down) {
        // PS/Home always passes through. Touchpad clicks while PS is held are
        // persona combo taps, so keep them out of game input as Capture.
        if (tp_edge && tap_count_ < 0xFFu) {
            ++tap_count_;
        }
        if (tp_down) {
            combo_consumed_tp = true;
        }
    } else if (ps_release) {
        if (tap_count_ == 1u) {
            result.request = PersonaComboRequest::SwitchPro;
        } else if (tap_count_ >= 2u) {
            result.request = PersonaComboRequest::Switch2Pro;
        }

        if (tap_count_ != 0u) {
            combo_consumed_tp = tp_down;
            suppress_tp_until_release_ = tp_down;
        }
        tap_count_ = 0;
    }

    ps_prev_ = ps_down;
    tp_prev_ = tp_down;

    if (combo_consumed_tp) {
        result.buttons &= ~static_cast<uint32_t>(kDs5TouchpadClick);
    }

    return result;
}

void PersonaComboFilter::reset()
{
    ps_prev_ = false;
    tp_prev_ = false;
    suppress_tp_until_release_ = false;
    tap_count_ = 0;
}

} // namespace ds5onswitch

#pragma once

namespace switchbridge {

void bootsel_button_task();
bool bootsel_button_interaction_active();
bool bootsel_button_debug_usb_requested();
bool bootsel_button_normal_led_enabled();
bool bootsel_button_led_override(bool &enabled);

} // namespace switchbridge

#pragma once

namespace ds5onswitch {

void bootsel_button_task();
bool bootsel_button_solid_led_enabled();
bool bootsel_button_led_override(bool &enabled);

} // namespace ds5onswitch

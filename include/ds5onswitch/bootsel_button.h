#pragma once

namespace ds5onswitch {

void bootsel_button_task();
bool bootsel_button_interaction_active();
bool bootsel_button_led_override(bool &enabled);

} // namespace ds5onswitch

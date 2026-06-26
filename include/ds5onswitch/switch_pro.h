#pragma once

#include "ds5onswitch/types.h"

namespace ds5onswitch {

void pack_switch_stick12(uint8_t out[3], uint16_t x, uint16_t y);
void switch_pro_button_bytes(uint32_t buttons, uint8_t out[3]);
SwitchProInputReport build_switch_pro_input_report(const SwitchProState &state,
                                                   uint8_t packet_timer,
                                                   bool vibration_enabled);

} // namespace ds5onswitch

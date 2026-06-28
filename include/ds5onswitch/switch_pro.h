#pragma once

#include "ds5onswitch/types.h"

namespace ds5onswitch {

void pack_switch_stick12(uint8_t out[3], uint16_t x, uint16_t y);
void switch_pro_button_bytes(uint32_t buttons, uint8_t out[3]);
// Switch 2 Pro (report 0x09) packs the kNs* button bits verbatim into 3 bytes
// (byte0 = bits 0..7, byte1 = bits 8..15, byte2 = bits 16..20). This differs
// from the Switch Pro 1 (report 0x30) remap in switch_pro_button_bytes().
void switch2_pro_button_bytes(uint32_t buttons, uint8_t out[3]);
SwitchProInputReport build_switch_pro_input_report(const SwitchProState &state,
                                                   uint8_t packet_timer,
                                                   bool vibration_enabled);

} // namespace ds5onswitch

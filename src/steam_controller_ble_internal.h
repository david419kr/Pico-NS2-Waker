#pragma once

#include "switch_bridge/switch_usb.h"

#include <cstdint>

namespace switchbridge {

bool steam_controller_ble_advertisement_matches(const uint8_t *packet);
bool steam_controller_ble_gatt_busy();
void steam_controller_ble_reset();
void steam_controller_ble_on_hids_connected(uint32_t now_ms);
void steam_controller_ble_on_hid_report_written();
void steam_controller_ble_schedule_heartbeat(uint32_t next_ms);
void steam_controller_ble_service(uint32_t now_ms);
bool steam_controller_ble_send_switch_feedback(const SwitchUsbFeedback &feedback);

} // namespace switchbridge

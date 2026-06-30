#pragma once

#include "switch_bridge/switch_usb.h"
#include "switch_bridge/types.h"

#include <cstddef>
#include <cstdint>

namespace switchbridge {

using Procon2BleDataCallback = void (*)(const SwitchProState &state);

void procon2_ble_register_data_callback(Procon2BleDataCallback callback);
void procon2_ble_stack_init();
void procon2_ble_service(uint32_t now_ms);

// Called by the existing BLE scanner. Returns true when the advertisement was
// consumed and a Pro Controller 2 connection attempt has started.
bool procon2_ble_handle_advertising_report(uint8_t *packet);

bool procon2_ble_is_active();
bool procon2_ble_is_ready();
bool procon2_ble_send_switch_feedback(const SwitchUsbFeedback &feedback);
bool procon2_ble_disconnect();

} // namespace switchbridge

#pragma once

#include "switch_bridge/switch_usb.h"

#include <cstddef>
#include <cstdint>

namespace switchbridge {

enum class BleControllerKind : uint8_t {
    Unknown,
    SteamController,
    Xbox,
};

using BleControllerDataCallback = void (*)(BleControllerKind kind,
                                           const uint8_t *data,
                                           std::size_t size);

void ble_controller_hub_register_data_callback(BleControllerDataCallback callback);
void ble_controller_hub_stack_init();
void ble_controller_hub_service(uint32_t now_ms);

bool ble_controller_hub_is_connected();
BleControllerKind ble_controller_hub_controller_kind();
bool ble_controller_hub_send_switch_feedback(const SwitchUsbFeedback &feedback);
bool ble_controller_hub_disconnect();

void ble_controller_hub_pause_for_switch2_wake();
void ble_controller_hub_resume_after_switch2_wake();

// Suppress the continuous LE HID discovery scan. The active 100%-duty scan
// starves the coexisting Classic ACL audio link, so it is turned off while a
// Classic controller (e.g. DualSense) is connected.
void ble_controller_hub_set_scan_suppressed(bool suppressed);

} // namespace switchbridge

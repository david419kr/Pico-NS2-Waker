#pragma once

#include <cstdint>

namespace switchbridge {

// Register the firmware-only Switch 2 BLE wake state machine with BTstack.
// Call before powering on HCI so the persisted target address can be loaded on
// HCI_STATE_WORKING.
void switch2_wake_init();

// Save the Switch 2 console BD_ADDR learned from USB BT manual-pairing. The
// address must be in HID/USB wire order, exactly as embedded into the BLE wake
// advertisement payload.
void switch2_wake_set_target_from_usb_pairing(const uint8_t addr_wire[6]);
bool switch2_wake_has_target();

// Request a wake attempt. The caller is responsible for checking user policy
// such as "USB has been inactive long enough" and "PS/Home edge happened".
bool switch2_wake_request(bool force = false);

// Non-blocking state-machine service. A normal wake attempt is canceled when
// USB becomes active. A forced attempt waits for an inactive-to-active
// transition or its advertising timeout.
void switch2_wake_service(bool usb_active);

} // namespace switchbridge

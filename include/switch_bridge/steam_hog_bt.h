#pragma once

#include "switch_bridge/switch_usb.h"

#include <cstddef>
#include <cstdint>

namespace switchbridge {

enum class SteamHogBtControllerKind : uint8_t {
    Unknown,
    SteamController,
    Xbox,
};

using SteamHogBtDataCallback = void (*)(SteamHogBtControllerKind kind,
                                        const uint8_t *data,
                                        std::size_t size);

void steam_hog_bt_register_data_callback(SteamHogBtDataCallback callback);
void steam_hog_bt_stack_init();
void steam_hog_bt_service(uint32_t now_ms);

bool steam_hog_bt_is_connected();
SteamHogBtControllerKind steam_hog_bt_controller_kind();
bool steam_hog_bt_send_switch_feedback(const SwitchUsbFeedback &feedback);
bool steam_hog_bt_disconnect();
bool steam_hog_bt_power_off();

void steam_hog_bt_pause_for_switch2_wake();
void steam_hog_bt_resume_after_switch2_wake();

// Suppress the continuous LE HID discovery scan. The active 100%-duty scan
// starves the coexisting Classic ACL audio link, so it is turned off while a
// Classic controller (e.g. DualSense) is connected.
void steam_hog_bt_set_scan_suppressed(bool suppressed);

} // namespace switchbridge

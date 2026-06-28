#pragma once

#include "ds5onswitch/switch_usb.h"

#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

using SteamHogBtDataCallback = void (*)(const uint8_t *data, std::size_t size);

void steam_hog_bt_register_data_callback(SteamHogBtDataCallback callback);
void steam_hog_bt_stack_init();
void steam_hog_bt_service(uint32_t now_ms);

bool steam_hog_bt_is_connected();
bool steam_hog_bt_send_switch_feedback(const SwitchUsbFeedback &feedback);
bool steam_hog_bt_disconnect();
bool steam_hog_bt_power_off();

void steam_hog_bt_pause_for_switch2_wake();
void steam_hog_bt_resume_after_switch2_wake();

} // namespace ds5onswitch

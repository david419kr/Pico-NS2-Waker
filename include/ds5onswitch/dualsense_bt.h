#pragma once

#include "ds5onswitch/dualsense_output.h"

#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

enum class DualSenseBtChannel {
    Interrupt,
    Control,
};

using DualSenseBtDataCallback =
    void (*)(DualSenseBtChannel channel, const uint8_t *data, std::size_t size);

void dualsense_bt_register_data_callback(DualSenseBtDataCallback callback);
bool dualsense_bt_init();
bool dualsense_bt_is_connected();
bool dualsense_bt_send_output_report(const DualSenseBtOutputReport &report);
bool dualsense_bt_send_raw_output_report(const uint8_t *report, std::size_t size);
bool dualsense_bt_disconnect();
void dualsense_bt_bootsel_click_action();
void dualsense_bt_forget_pairings();
void dualsense_bt_persist_blacklist_if_dirty();

} // namespace ds5onswitch

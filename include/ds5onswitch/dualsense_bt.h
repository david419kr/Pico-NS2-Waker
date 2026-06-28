#pragma once

#include "ds5onswitch/dualsense_output.h"

#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

enum class DualSenseBtChannel {
    Interrupt,
    Control,
};

enum class BtControllerKind {
    Unknown,
    DualSense,
    DualShock4,
    SwitchPro,
    Xbox,
};

using DualSenseBtDataCallback =
    void (*)(DualSenseBtChannel channel, const uint8_t *data, std::size_t size);

void dualsense_bt_register_data_callback(DualSenseBtDataCallback callback);
bool dualsense_bt_init();
bool dualsense_bt_is_connected();
BtControllerKind dualsense_bt_controller_kind();
bool dualsense_bt_send_output_report(const DualSenseBtOutputReport &report);
bool dualsense_bt_send_raw_output_report(const uint8_t *report, std::size_t size);
bool dualsense_bt_power_off();
bool dualsense_bt_disconnect();
void dualsense_bt_bootsel_click_action();
void dualsense_bt_forget_pairings();
void dualsense_bt_persist_blacklist_if_dirty();

// Pause Classic discovery/page scan while the Switch 2 BLE wake advertiser runs.
// The active DualSense ACL is intentionally kept alive for the coexistence
// experiment; if it drops anyway, inquiry will not auto-restart until resume.
void dualsense_bt_pause_for_switch2_wake();
// Resume normal Classic operation. If the DualSense ACL is still alive, do not
// start inquiry again.
void dualsense_bt_resume_after_switch2_wake();

} // namespace ds5onswitch

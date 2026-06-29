#pragma once

#include "switch_bridge/dualsense_output.h"

#include <cstddef>
#include <cstdint>

namespace switchbridge {

enum class ClassicHidChannel {
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

using ClassicHidDataCallback =
    void (*)(ClassicHidChannel channel, const uint8_t *data, std::size_t size);

void classic_hid_register_data_callback(ClassicHidDataCallback callback);
bool classic_hid_init();
bool classic_hid_is_connected();
BtControllerKind classic_hid_controller_kind();
bool classic_hid_send_dualsense_output_report(const DualSenseBtOutputReport &report);
bool classic_hid_send_raw_output_report(const uint8_t *report, std::size_t size);
std::size_t classic_hid_debug_counters_report(uint8_t *out, std::size_t len);
void classic_hid_reset_debug_counters();
bool classic_hid_power_off_active_controller();
bool classic_hid_disconnect();
void classic_hid_bootsel_click_action();
void classic_hid_forget_pairings();
void classic_hid_persist_blacklist_if_dirty();

// Pause Classic discovery/page scan while the Switch 2 BLE wake advertiser runs.
// The active Classic HID ACL is intentionally kept alive for the coexistence
// experiment; if it drops anyway, inquiry will not auto-restart until resume.
void classic_hid_pause_for_switch2_wake();
// Resume normal Classic operation. If the active ACL is still alive, do not
// start inquiry again.
void classic_hid_resume_after_switch2_wake();

} // namespace switchbridge

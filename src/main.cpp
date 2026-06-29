#include "switch_bridge/mapper.h"
#include "switch_bridge/bootsel_button.h"
#include "switch_bridge/bt_debug_log.h"
#include "switch_bridge/controller_feedback.h"
#include "switch_bridge/debug_flags.h"
#include "switch_bridge/dualshock4_output.h"
#include "switch_bridge/dualshock4_report.h"
#include "switch_bridge/dualsense_audio.h"
#include "switch_bridge/classic_hid_bt.h"
#include "switch_bridge/dualsense_output.h"
#include "switch_bridge/dualsense_report.h"
#include "switch_bridge/procon2_handshake.h"
#include "switch_bridge/persona_combo_filter.h"
#include "switch_bridge/steam_controller.h"
#include "switch_bridge/steam_hog_bt.h"
#include "switch_bridge/switch2_auto_registration.h"
#include "switch_bridge/switch_identity.h"
#include "switch_bridge/switch_pro.h"
#include "switch_bridge/switch_usb.h"
#include "switch_bridge/switch2_wake_runtime.h"
#include "switch_bridge/usb_descriptors.h"
#include "switch_bridge/xbox_controller.h"

#include "bsp/board_api.h"
#include "cyw43.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include <algorithm>
#include <array>
#include <cstring>

using namespace switchbridge;

namespace {

SwitchUsbEmulator g_switch_usb;
DualSenseOutputState g_ds_output;
DualShock4OutputState g_ds4_output;
std::array<SwitchReport, 3> g_pending_reports{};
uint8_t g_pending_count = 0;
uint8_t g_ds_output_sequence = 0;
uint32_t g_last_haptics_ms = 0;
uint32_t g_last_ds_output_ms = 0;
uint32_t g_last_ds4_output_ms = 0;
bool g_ds_output_dirty = false;
bool g_ds4_output_dirty = false;
UsbAudioPersona g_usb_persona = UsbAudioPersona::HidOnly;
bool g_usb_reconnect_pending = false;
uint32_t g_usb_reconnect_at_ms = 0;
bool g_debug_usb_mode = false;
bool g_ds_mute_button_down = false;
bool g_usb_unmounted = false;
uint32_t g_usb_unmounted_since_ms = 0;
uint32_t g_last_ds_buttons = 0;
uint32_t g_last_ds4_buttons = 0;
uint32_t g_last_ns_buttons = 0;
uint32_t g_last_steam_buttons = 0;
uint32_t g_last_xbox_buttons = 0;
bool g_home_wake_pending = false;
uint32_t g_home_wake_pending_ms = 0;
bool g_auto_wake_armed = false;
uint32_t g_controller_connected_since_ms = 0;
uint8_t g_switch2_report_counter = 0;
PersonaComboFilter g_persona_combo_filter;
Switch2AutoRegistration g_switch2_auto_registration;
SwitchReport g_procon_passthrough_report{};
bool g_procon_passthrough_valid = false;
uint32_t g_debug_log_read_offset = 0;
HapticCommand g_xbox_haptics{};
uint32_t g_last_xbox_output_ms = 0;
bool g_xbox_output_dirty = false;
XboxControllerInput g_xbox_input{};
SteamTrackpadHapticState g_steam_trackpad_haptics{};

constexpr uint32_t kUsbUnmountDebounceMs = 1000;
constexpr uint32_t kHomeWakePendingWindowMs = 3000;
constexpr uint8_t kDebugLogReadFeatureReportId = 0x7e;
constexpr uint8_t kDebugLogControlFeatureReportId = 0x7f;
constexpr uint8_t kDebugAudioCountersFeatureReportId = 0x7c;
constexpr uint8_t kDebugClassicCountersFeatureReportId = 0x7d;

void configure_switch_identity_from_radio()
{
    // The CYW43 BTstack transport sets the controller's public Bluetooth
    // address to (Wi-Fi MAC + 1) on the last byte (see the SDK's
    // btstack_hci_transport_cyw43.c, restored to stock by
    // tools/patch-pico-cyw43-bdaddr.ps1). Mirror that here so the USB Switch Pro
    // persona reports the controller's *real* radio address. Keeping the
    // reported identity equal to the radio address is what allows a
    // general-purpose Switch 2 wake (the console can register this controller's
    // own address into its BLE wake whitelist), instead of borrowing a hardcoded
    // anomalous Joy-Con address.
    SwitchControllerBdAddr identity{};
    std::memcpy(identity.data(), cyw43_state.mac, identity.size());
    bool any_nonzero = false;
    bool any_not_ff = false;
    for (uint8_t b : identity) {
        any_nonzero = any_nonzero || b != 0x00u;
        any_not_ff = any_not_ff || b != 0xffu;
    }
    if (!any_nonzero || !any_not_ff) {
        // cyw43_state.mac is still zero before HCI reaches WORKING on some
        // boots. Do not turn that into 00:00:00:00:00:01; classic_hid_bt.cpp
        // will set the identity from BTstack's real local BD_ADDR later.
        return;
    }
    if (identity[5] == 0xffu) {
        // Avoid wrapping the last byte into 0x00 (unusable address). Fall back
        // to the captured Pro Controller identity in that rare case.
        switch_identity_set_bd_addr(kSwitchProControllerBdAddrFallback);
        return;
    }
    ++identity[5];
    switch_identity_set_bd_addr(identity);
}

void controller_addr_wire(uint8_t out[6])
{
    const SwitchControllerBdAddr &bd_addr = switch_identity_bd_addr();
    for (std::size_t i = 0; i < bd_addr.size(); ++i) {
        out[i] = bd_addr[bd_addr.size() - 1u - i];
    }
}

bool valid_addr_wire(const uint8_t addr[6])
{
    if (addr == nullptr) {
        return false;
    }
    bool any_nonzero = false;
    bool any_not_ff = false;
    for (std::size_t i = 0; i < 6u; ++i) {
        any_nonzero = any_nonzero || addr[i] != 0x00u;
        any_not_ff = any_not_ff || addr[i] != 0xffu;
    }
    return any_nonzero && any_not_ff;
}

bool procon2_extract_console_addr_wire(const uint8_t *request,
                                       std::size_t size,
                                       uint8_t out_addr_wire[6])
{
    if (request == nullptr || out_addr_wire == nullptr || size < 16u) {
        return false;
    }
    // Captured 0x03 bulk request:
    //   03 91 00 0d 00 08 00 00 01 00 <console addr wire[6]>
    if (request[0] != kProcon2ReportMac ||
        request[1] != kProcon2DirHostRequest) {
        return false;
    }
    if (!valid_addr_wire(&request[10])) {
        return false;
    }
    std::memcpy(out_addr_wire, &request[10], 6);
    return true;
}

SwitchReport build_switch2_registration_input_report()
{
    SwitchReport out{};
    const SwitchProState &state = g_switch_usb.state();
    uint8_t buttons[3]{};
    // Switch 2 Pro report 0x09 uses the verbatim button bit layout, not the
    // Switch Pro 1 (0x30) remap. Using the wrong packer made every button wrong
    // in Switch 2 persona, so the console saw no usable input.
    switch2_pro_button_bytes(state.buttons, buttons);

    out[0] = 0x09u;
    out[1] = g_switch2_report_counter++;
    out[2] = 0x23u; // SC_SWITCH2_POWER_USB_FULL (USB powered / full battery).
    std::memcpy(&out[3], buttons, sizeof(buttons));
    pack_switch_stick12(&out[6], state.left_x, state.left_y);
    pack_switch_stick12(&out[9], state.right_x, state.right_y);
    // Switch 2 registration is button/stick/trigger only: no IMU or rumble
    // feature lane is advertised, so byte 12 stays at the no-motion marker.
    out[12] = 0x30u;
    return out;
}

void request_usb_reconnect(uint32_t delay_ms = 250u)
{
    tud_disconnect();
    g_usb_reconnect_pending = true;
    g_usb_reconnect_at_ms = board_millis() + delay_ms;
}

// Switch into the Switch Pro 1 persona (normal gameplay use). Audio interfaces
// follow whatever the DualSense currently has plugged into its 3.5mm jack.
void select_switch_pro_persona()
{
    if (usb_descriptors_controller_persona() == UsbControllerPersona::SwitchPro) {
        return;
    }
    usb_descriptors_set_controller_persona(UsbControllerPersona::SwitchPro);
    g_usb_persona = classic_hid_controller_kind() == BtControllerKind::DualSense
        ? dualsense_audio_desired_persona()
        : UsbAudioPersona::HidOnly;
    usb_descriptors_set_audio_persona(g_usb_persona);
    request_usb_reconnect();
}

// Switch into the Switch 2 Pro persona (business-card / pairing exchange only).
// Only the interfaces a genuine ProCon2 exposes are advertised: no DualSense
// USB-audio interfaces at all.
void select_switch2_pro_persona()
{
    if (usb_descriptors_controller_persona() ==
        UsbControllerPersona::Switch2ProRegistration) {
        return;
    }
    g_switch2_report_counter = 0;
    procon2_handshake_reset();
    usb_descriptors_set_controller_persona(
        UsbControllerPersona::Switch2ProRegistration);
    g_usb_persona = UsbAudioPersona::HidOnly;
    usb_descriptors_set_audio_persona(UsbAudioPersona::HidOnly);
    request_usb_reconnect();
}

void push_pending(const SwitchReport &report)
{
    if (g_pending_count >= g_pending_reports.size()) {
        return;
    }
    g_pending_reports[g_pending_count++] = report;
}

bool __not_in_flash_func(pop_pending)(SwitchReport &report)
{
    if (g_pending_count == 0) {
        return false;
    }

    report = g_pending_reports[0];
    for (uint8_t i = 1; i < g_pending_count; ++i) {
        g_pending_reports[i - 1] = g_pending_reports[i];
    }
    --g_pending_count;
    return true;
}

void __not_in_flash_func(send_report)(const SwitchReport &report)
{
    (void)tud_hid_report(report[0], report.data() + 1, report.size() - 1);
}

bool copy_switch_pro_full_report_from_bt(const uint8_t *data, std::size_t size,
                                         SwitchReport &out)
{
    if (data == nullptr || size == 0) {
        return false;
    }

    if (data[0] == 0xa1u) {
        ++data;
        --size;
    }
    if (size < 49u || data[0] != kSwitchProFullInputReportId) {
        return false;
    }

    out.fill(0);
    const std::size_t copy_len = std::min<std::size_t>(size, out.size());
    std::memcpy(out.data(), data, copy_len);
    return true;
}

bool bt_controller_is_dualsense()
{
    return classic_hid_controller_kind() == BtControllerKind::DualSense;
}

bool bt_controller_is_dualshock4()
{
    return classic_hid_controller_kind() == BtControllerKind::DualShock4;
}

bool bt_controller_is_switch_pro()
{
    return classic_hid_controller_kind() == BtControllerKind::SwitchPro;
}

bool bt_controller_is_xbox()
{
    return classic_hid_controller_kind() == BtControllerKind::Xbox;
}

bool bt_controller_connected()
{
    return classic_hid_is_connected() || steam_hog_bt_is_connected();
}

bool steam_hog_controller_is_xbox()
{
    return steam_hog_bt_controller_kind() == SteamHogBtControllerKind::Xbox;
}

void __not_in_flash_func(handle_xbox_input)(XboxControllerInput input)
{
    const uint32_t now = board_millis();
    const PersonaComboFilterResult combo =
        g_persona_combo_filter.process(input.buttons, kXboxBtnGuide,
                                       kXboxBtnBack, now);
    input.buttons = combo.buttons;
    if (combo.request == PersonaComboRequest::SwitchPro) {
        select_switch_pro_persona();
    } else if (combo.request == PersonaComboRequest::Switch2Pro) {
        select_switch2_pro_persona();
    }

    const bool capture_combo =
        (input.buttons & (kXboxBtnBack | kXboxBtnMenu)) ==
        (kXboxBtnBack | kXboxBtnMenu);
    if (capture_combo) {
        input.buttons &= ~(kXboxBtnBack | kXboxBtnMenu);
    }

    const bool home_down = (input.buttons & kXboxBtnGuide) != 0;
    const bool home_edge =
        home_down && (g_last_xbox_buttons & kXboxBtnGuide) == 0;
    g_last_xbox_buttons = input.buttons;
    if (home_edge) {
        g_home_wake_pending = true;
        g_home_wake_pending_ms = now;
    }

    g_procon_passthrough_valid = false;
    SwitchProState state = map_xbox_to_switch(input);
    if (capture_combo) {
        state.buttons |= kNsCapture;
    }
    g_switch_usb.set_state(state);
}

void __not_in_flash_func(on_classic_hid_data)(ClassicHidChannel channel,
                                               const uint8_t *data,
                                               std::size_t size)
{
    if (channel != ClassicHidChannel::Interrupt) {
        return;
    }

    if (bt_controller_is_switch_pro()) {
        SwitchProState state{};
        if (!parse_switch_pro_bt_report(data, size, state)) {
            return;
        }

        const uint32_t now = board_millis();
        const PersonaComboFilterResult combo =
            g_persona_combo_filter.process(state.buttons, kNsHome, kNsCapture,
                                           now);
        state.buttons = combo.buttons;
        if (combo.request == PersonaComboRequest::SwitchPro) {
            select_switch_pro_persona();
        } else if (combo.request == PersonaComboRequest::Switch2Pro) {
            select_switch2_pro_persona();
        }

        const bool home_down = (state.buttons & kNsHome) != 0;
        const bool home_edge = home_down && (g_last_ns_buttons & kNsHome) == 0;
        g_last_ns_buttons = state.buttons;
        if (home_edge) {
            g_home_wake_pending = true;
            g_home_wake_pending_ms = now;
        }

        SwitchReport raw_report{};
        if (copy_switch_pro_full_report_from_bt(data, size, raw_report)) {
            write_switch_pro_report_buttons(raw_report, state.buttons);
            g_procon_passthrough_report = raw_report;
            g_procon_passthrough_valid = true;
        }
        g_switch_usb.set_state(state);
        return;
    }

    if (bt_controller_is_xbox()) {
        if (!parse_xbox_bt_report(data, size, g_xbox_input)) {
            return;
        }
        handle_xbox_input(g_xbox_input);
        return;
    }

    if (bt_controller_is_dualshock4()) {
        PlayStationInput input{};
        if (!parse_dualshock4_bt_report(data, size, input)) {
            return;
        }

        const uint32_t now = board_millis();
        const PersonaComboFilterResult combo =
            g_persona_combo_filter.process(input.buttons, now);
        input.buttons = combo.buttons;
        if (combo.request == PersonaComboRequest::SwitchPro) {
            select_switch_pro_persona();
        } else if (combo.request == PersonaComboRequest::Switch2Pro) {
            select_switch2_pro_persona();
        }

        const bool home_down = (input.buttons & kPsButton) != 0;
        const bool home_edge =
            home_down && (g_last_ds4_buttons & kPsButton) == 0;
        g_last_ds4_buttons = input.buttons;
        if (home_edge) {
            g_home_wake_pending = true;
            g_home_wake_pending_ms = now;
        }

        g_procon_passthrough_valid = false;
        g_switch_usb.set_state(map_playstation_to_switch(input));
        return;
    }

    if (size >= 4u && data[0] == 0xa1 && data[1] == 0x31 &&
        (data[2] & 0x02u) != 0) {
        dualsense_audio_add_mic_frame(data + 4, size - 4u);
        return;
    }

    PlayStationInput input{};
    if (!parse_dualsense_bt_report(data, size, input)) {
        return;
    }

    const uint32_t now = board_millis();
    const PersonaComboFilterResult combo =
        g_persona_combo_filter.process(input.buttons, now);
    input.buttons = combo.buttons;
    if (combo.request == PersonaComboRequest::SwitchPro) {
        select_switch_pro_persona();
    } else if (combo.request == PersonaComboRequest::Switch2Pro) {
        select_switch2_pro_persona();
    }

    const bool home_down = (input.buttons & kPsButton) != 0;
    const bool home_edge = home_down && (g_last_ds_buttons & kPsButton) == 0;
    g_last_ds_buttons = input.buttons;
    if (home_edge) {
        g_home_wake_pending = true;
        g_home_wake_pending_ms = now;
    }

    const bool mute_button_down = (input.buttons & kPsMute) != 0;
    if (mute_button_down && !g_ds_mute_button_down) {
        const bool muted = dualsense_audio_toggle_mic_muted();
        g_ds_output.set_mic_muted(muted);
        g_ds_output_dirty = true;
    }
    g_ds_mute_button_down = mute_button_down;

    dualsense_audio_update_status(input);
    if (usb_descriptors_controller_persona() == UsbControllerPersona::SwitchPro &&
        !g_switch2_auto_registration.blocks_audio_reconnect() &&
        bt_controller_is_dualsense() &&
        dualsense_audio_desired_persona() != g_usb_persona) {
        g_usb_persona = dualsense_audio_desired_persona();
        usb_descriptors_set_audio_persona(g_usb_persona);
        request_usb_reconnect();
    }

    g_switch_usb.set_state(map_playstation_to_switch(input));
}

void __not_in_flash_func(on_steam_hog_bt_data)(SteamHogBtControllerKind kind,
                                               const uint8_t *data,
                                               std::size_t size)
{
    if (kind == SteamHogBtControllerKind::Xbox) {
        if (!parse_xbox_bt_report(data, size, g_xbox_input)) {
            return;
        }
        handle_xbox_input(g_xbox_input);
        return;
    }

    SteamControllerInput input{};
    if (!parse_steam_controller_report(data, size, input)) {
        return;
    }

    const uint32_t now = board_millis();
    const PersonaComboFilterResult combo =
        g_persona_combo_filter.process(input.buttons, kSteamBtnSteam,
                                       kSteamBtnSteamMenu, now);
    input.buttons = combo.buttons;
    if (combo.request == PersonaComboRequest::SwitchPro) {
        select_switch_pro_persona();
    } else if (combo.request == PersonaComboRequest::Switch2Pro) {
        select_switch2_pro_persona();
    }

    const bool home_down = (input.buttons & kSteamBtnSteam) != 0;
    const bool home_edge =
        home_down && (g_last_steam_buttons & kSteamBtnSteam) == 0;
    g_last_steam_buttons = input.buttons;
    if (home_edge) {
        g_home_wake_pending = true;
        g_home_wake_pending_ms = now;
    }

    const SteamTrackpadHapticAction haptic_action =
        update_steam_trackpad_haptics(input, g_steam_trackpad_haptics, now);
    if (haptic_action.left_click) {
        steam_hog_bt_pulse_trackpad_haptic(true, true);
    } else if (haptic_action.left_tick) {
        steam_hog_bt_pulse_trackpad_haptic(true, false);
    }
    if (haptic_action.right_click) {
        steam_hog_bt_pulse_trackpad_haptic(false, true);
    } else if (haptic_action.right_tick) {
        steam_hog_bt_pulse_trackpad_haptic(false, false);
    }

    g_procon_passthrough_valid = false;
    g_switch_usb.set_state(map_steam_to_switch(input));
}

void __not_in_flash_func(apply_switch_feedback)(
    const SwitchUsbFeedback &feedback)
{
    if (!feedback.has_rumble) {
        return;
    }

    if (feedback.haptics.active) {
        g_ds_output.apply_haptics(feedback.haptics);
        g_ds4_output.apply_haptics(feedback.haptics);
        g_xbox_haptics = feedback.haptics;
        g_last_haptics_ms = board_millis();
    } else {
        g_ds_output.clear_motors();
        g_ds4_output.clear_motors();
        g_xbox_haptics = {};
    }
    g_ds_output_dirty = true;
    g_ds4_output_dirty = true;
    g_xbox_output_dirty = true;
}

void __not_in_flash_func(forward_switch_output_to_bt_procon)(
    const SwitchReport &report, std::size_t size)
{
    if (!bt_controller_is_switch_pro() || size == 0) {
        return;
    }
    if (report[0] != kSwitchOutputSubcommand &&
        report[0] != kSwitchOutputRumbleOnly) {
        return;
    }
    (void)classic_hid_send_raw_output_report(report.data(), size);
}

void __not_in_flash_func(service_classic_hid_connection)()
{
    static bool was_connected = false;
    static BtControllerKind last_kind = BtControllerKind::Unknown;
    const bool connected = classic_hid_is_connected();
    const BtControllerKind kind = classic_hid_controller_kind();
    steam_hog_bt_set_scan_suppressed(connected);
    if (connected == was_connected && kind == last_kind) {
        return;
    }

    const BtControllerKind previous_kind = last_kind;
    const bool became_connected = connected && !was_connected;
    const bool became_identified =
        connected && kind != BtControllerKind::Unknown &&
        (became_connected || previous_kind == BtControllerKind::Unknown);
    was_connected = connected;
    last_kind = kind;

    if (!connected) {
        dualsense_audio_set_connected(false);
        g_ds_output.reset();
        g_ds4_output.reset();
        g_xbox_haptics = {};
        g_ds_output_dirty = false;
        g_ds4_output_dirty = false;
        g_xbox_output_dirty = false;
        g_ds_mute_button_down = false;
        g_last_ds_buttons = 0;
        g_last_ds4_buttons = 0;
        g_last_ns_buttons = 0;
        g_last_xbox_buttons = 0;
        g_xbox_input = {};
        g_procon_passthrough_valid = false;
        g_persona_combo_filter.reset();
        if (steam_hog_bt_is_connected()) {
            return;
        }
        g_auto_wake_armed = false;
        g_usb_reconnect_pending = false;
        usb_descriptors_set_controller_persona(UsbControllerPersona::SwitchPro);
        g_usb_persona = UsbAudioPersona::HidOnly;
        usb_descriptors_set_audio_persona(g_usb_persona);
        if (!g_debug_usb_mode) {
            tud_disconnect();
        }

        PlayStationInput neutral{};
        neutral.has_imu = true;
        g_switch_usb.set_state(map_playstation_to_switch(neutral));
        return;
    }

    if (became_connected) {
        g_debug_usb_mode = false;
        g_auto_wake_armed = true;
        g_controller_connected_since_ms = board_millis();
        request_usb_reconnect();
    }

    const bool dualsense_connected = kind == BtControllerKind::DualSense;
    const bool dualshock4_connected = kind == BtControllerKind::DualShock4;
    const bool xbox_connected = kind == BtControllerKind::Xbox;
    dualsense_audio_set_connected(dualsense_connected);
    g_ds_output.reset();
    g_ds4_output.reset();
    g_xbox_haptics = {};
    g_ds_output.set_mic_muted(dualsense_audio_mic_muted());
    g_ds_output_dirty = dualsense_connected;
    g_ds4_output_dirty = dualshock4_connected;
    g_xbox_output_dirty = false;
    if (!dualsense_connected) {
        g_ds_mute_button_down = false;
        g_last_ds_buttons = 0;
    }
    if (!dualshock4_connected) {
        g_last_ds4_buttons = 0;
    }
    if (!xbox_connected) {
        g_last_xbox_buttons = 0;
        g_xbox_input = {};
    }
    if (became_identified) {
        controller_feedback_play(ControllerFeedbackCue::Connected);
    }
}

void __not_in_flash_func(service_steam_connection)()
{
    static bool was_connected = false;
    static SteamHogBtControllerKind last_kind = SteamHogBtControllerKind::Unknown;
    const bool connected = steam_hog_bt_is_connected();
    const SteamHogBtControllerKind kind = steam_hog_bt_controller_kind();
    if (connected == was_connected && kind == last_kind) {
        return;
    }

    const SteamHogBtControllerKind previous_kind = last_kind;
    const bool became_connected = connected && !was_connected;
    const bool became_identified =
        connected && kind != SteamHogBtControllerKind::Unknown &&
        (became_connected || previous_kind == SteamHogBtControllerKind::Unknown);
    was_connected = connected;
    last_kind = kind;
    if (kind != previous_kind) {
        g_steam_trackpad_haptics = {};
    }

    if (!connected) {
        g_last_steam_buttons = 0;
        g_last_xbox_buttons = 0;
        g_xbox_input = {};
        g_steam_trackpad_haptics = {};
        if (classic_hid_is_connected()) {
            return;
        }
        g_auto_wake_armed = false;
        g_usb_reconnect_pending = false;
        g_procon_passthrough_valid = false;
        g_persona_combo_filter.reset();
        usb_descriptors_set_controller_persona(UsbControllerPersona::SwitchPro);
        g_usb_persona = UsbAudioPersona::HidOnly;
        usb_descriptors_set_audio_persona(g_usb_persona);
        if (!g_debug_usb_mode) {
            tud_disconnect();
        }

        SwitchProState neutral{};
        neutral.has_imu = true;
        g_switch_usb.set_state(neutral);
        return;
    }

    if (became_connected) {
        g_debug_usb_mode = false;
        g_auto_wake_armed = true;
        g_controller_connected_since_ms = board_millis();
        g_procon_passthrough_valid = false;
        usb_descriptors_set_controller_persona(UsbControllerPersona::SwitchPro);
        g_usb_persona = UsbAudioPersona::HidOnly;
        usb_descriptors_set_audio_persona(g_usb_persona);
        request_usb_reconnect();
    }
    if (became_identified) {
        controller_feedback_play(ControllerFeedbackCue::Connected);
    }
}

void __not_in_flash_func(service_usb_mount_tracking)()
{
    const uint32_t now = board_millis();
    if (tud_mounted()) {
        g_usb_unmounted = false;
        g_usb_unmounted_since_ms = 0;
        return;
    }

    if (!g_usb_unmounted) {
        g_usb_unmounted = true;
        g_usb_unmounted_since_ms = now;
    }
}

bool __not_in_flash_func(usb_sleep_candidate)(uint32_t now)
{
    return g_usb_unmounted && !g_usb_reconnect_pending &&
           now - g_usb_unmounted_since_ms >= kUsbUnmountDebounceMs;
}

void __not_in_flash_func(service_switch2_auto_registration)()
{
    uint8_t ctrl_addr_wire[6]{};
    controller_addr_wire(ctrl_addr_wire);

    const bool switch2_persona_active =
        usb_descriptors_controller_persona() ==
        UsbControllerPersona::Switch2ProRegistration;

    Switch2AutoRegistrationInputs inputs{};
    inputs.now_ms = board_millis();
    inputs.usb_mounted = tud_mounted();
    inputs.controller_connected = bt_controller_connected();
    inputs.debug_usb_mode = g_debug_usb_mode;
    inputs.usb_reconnect_pending = g_usb_reconnect_pending;
    inputs.switch2_persona_active = switch2_persona_active;
    inputs.registration_complete =
        switch2_wake_registration_complete(ctrl_addr_wire);

    const Switch2AutoRegistrationAction action =
        g_switch2_auto_registration.service(inputs);
    switch (action) {
    case Switch2AutoRegistrationAction::EnterSwitch2RegistrationPersona:
        select_switch2_pro_persona();
        break;
    case Switch2AutoRegistrationAction::RevertToSwitchProPersona:
        select_switch_pro_persona();
        break;
    case Switch2AutoRegistrationAction::None:
    default:
        break;
    }
}

void __not_in_flash_func(service_switch2_wake_policy)()
{
    const uint32_t now = board_millis();
    const bool mounted = tud_mounted();

    if (mounted) {
        g_home_wake_pending = false;
        g_auto_wake_armed = false;
    }

    if (g_home_wake_pending) {
        if (mounted ||
            now - g_home_wake_pending_ms > kHomeWakePendingWindowMs) {
            g_home_wake_pending = false;
        } else if (usb_sleep_candidate(now) && switch2_wake_has_target()) {
            g_home_wake_pending = false;
            g_auto_wake_armed = false;
            (void)switch2_wake_request();
        }
    }

    if (g_auto_wake_armed && bt_controller_connected()) {
        if (mounted) {
            g_auto_wake_armed = false;
        } else if (now - g_controller_connected_since_ms >= kUsbUnmountDebounceMs &&
                   usb_sleep_candidate(now) && switch2_wake_has_target()) {
            g_auto_wake_armed = false;
            (void)switch2_wake_request();
        }
    }

    switch2_wake_service(mounted);
}

void __not_in_flash_func(service_dualsense_output)()
{
    if (!classic_hid_is_connected() || !bt_controller_is_dualsense()) {
        return;
    }

    const uint32_t now = board_millis();
    if (g_ds_output.motors_active() && now - g_last_haptics_ms > 96u) {
        g_ds_output.clear_motors();
        g_ds_output_dirty = true;
    }

    if (dualsense_audio_speaker_active()) {
        g_last_ds_output_ms = now;
        g_ds_output_dirty = false;
        return;
    }

    if (!g_ds_output_dirty &&
        (!g_ds_output.motors_active() || now - g_last_ds_output_ms < 20u)) {
        return;
    }

    const DualSenseBtOutputReport report =
        g_ds_output.build_report(g_ds_output_sequence++);
    if (classic_hid_send_dualsense_output_report(report)) {
        g_last_ds_output_ms = now;
        g_ds_output_dirty = false;
    }
}

void __not_in_flash_func(service_dualshock4_output)()
{
    if (!classic_hid_is_connected() || !bt_controller_is_dualshock4()) {
        return;
    }

    const uint32_t now = board_millis();
    if (g_ds4_output.motors_active() && now - g_last_haptics_ms > 96u) {
        g_ds4_output.clear_motors();
        g_ds4_output_dirty = true;
    }

    if (!g_ds4_output_dirty &&
        (!g_ds4_output.motors_active() || now - g_last_ds4_output_ms < 20u)) {
        return;
    }

    const DualShock4BtOutputReport report = g_ds4_output.build_report();
    if (classic_hid_send_raw_output_report(report.data(), report.size())) {
        g_last_ds4_output_ms = now;
        g_ds4_output_dirty = false;
    }
}

void __not_in_flash_func(service_xbox_output)()
{
    if (!classic_hid_is_connected() || !bt_controller_is_xbox()) {
        return;
    }

    const uint32_t now = board_millis();
    if (g_xbox_haptics.active && now - g_last_haptics_ms > 96u) {
        g_xbox_haptics = {};
        g_xbox_output_dirty = true;
    }

    if (!g_xbox_output_dirty &&
        (!g_xbox_haptics.active || now - g_last_xbox_output_ms < 20u)) {
        return;
    }

    uint8_t report[kXboxRumbleReportSize]{};
    const std::size_t len =
        build_xbox_rumble_output(g_xbox_haptics, report, sizeof(report));
    if (len != 0u &&
        classic_hid_send_raw_output_report(report, len)) {
        g_last_xbox_output_ms = now;
        g_xbox_output_dirty = false;
    }
}

void __not_in_flash_func(service_switch2_registration)()
{
    if (usb_descriptors_controller_persona() !=
        UsbControllerPersona::Switch2ProRegistration) {
        return;
    }

    uint8_t request[64]{};
    while (tud_vendor_available() > 0) {
        const uint32_t n = tud_vendor_read(request, sizeof(request));
        if (n == 0) {
            break;
        }

        uint8_t console_addr[6]{};
        if (procon2_extract_console_addr_wire(request, n, console_addr)) {
            switch2_wake_set_target_from_usb_pairing(console_addr);
            g_switch2_auto_registration.note_switch_console_seen(board_millis());
        }

        uint8_t ctrl_addr_wire[6]{};
        controller_addr_wire(ctrl_addr_wire);
        const Procon2Reply reply =
            procon2_build_reply(request, n, ctrl_addr_wire);
        if (reply.pairing_finalized) {
            (void)switch2_wake_mark_registration_complete(ctrl_addr_wire);
            g_switch2_auto_registration.note_pairing_finalized();
        }
        if (reply.valid) {
            (void)tud_vendor_write(reply.data.data(),
                                   static_cast<uint32_t>(reply.len));
            (void)tud_vendor_flush();
        }
    }
}

void __not_in_flash_func(service_usb_reports)()
{
    static uint32_t last_input_ms = 0;
    if (!tud_mounted() || !tud_hid_ready()) {
        return;
    }

    SwitchReport report{};
    if (pop_pending(report)) {
        send_report(report);
        return;
    }

    const uint32_t now = board_millis();
    if (now - last_input_ms < 8u) {
        return;
    }

    last_input_ms = now;
    if (usb_descriptors_controller_persona() ==
        UsbControllerPersona::Switch2ProRegistration) {
        send_report(build_switch2_registration_input_report());
    } else if (bt_controller_is_switch_pro() && g_procon_passthrough_valid) {
        send_report(g_procon_passthrough_report);
    } else {
        send_report(g_switch_usb.build_input_report());
    }
}

void __not_in_flash_func(service_usb_reconnect)()
{
    if (!g_usb_reconnect_pending) {
        return;
    }

    if (!bt_controller_connected()) {
        g_usb_reconnect_pending = false;
        return;
    }

    const uint32_t now = board_millis();
    if (static_cast<int32_t>(now - g_usb_reconnect_at_ms) < 0) {
        return;
    }

    tud_connect();
    g_usb_reconnect_pending = false;
}

void __not_in_flash_func(service_debug_usb_request)()
{
    if (!kEnableBtDebugTools) {
        return;
    }
    if (!bootsel_button_debug_usb_requested()) {
        return;
    }

    g_debug_usb_mode = true;
    g_usb_reconnect_pending = false;
    usb_descriptors_set_controller_persona(UsbControllerPersona::SwitchPro);
    g_usb_persona = UsbAudioPersona::HidOnly;
    usb_descriptors_set_audio_persona(g_usb_persona);
    tud_connect();
}

void service_led()
{
    static uint32_t last_ms = 0;
    static bool led = false;
    const uint32_t now = board_millis();
    bool override_led = false;
    if (bootsel_button_led_override(override_led)) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, override_led);
        return;
    }

    if (!bootsel_button_normal_led_enabled()) {
        led = false;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        return;
    }

    const bool mounted = tud_mounted();
    const bool connected = bt_controller_connected();
    if (!mounted || !connected) {
        const uint32_t interval = mounted ? 500u : 250u;
        if (now - last_ms < interval) {
            return;
        }

        last_ms = now;
        led = !led;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
        return;
    }

    led = true;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
}

void __no_inline_not_in_flash_func(app_loop)()
{
    while (true) {
        bootsel_button_task();
        if (bootsel_button_interaction_active()) {
            continue;
        }

        service_debug_usb_request();
        cyw43_arch_poll();
        tud_task();
        service_usb_mount_tracking();
        steam_hog_bt_service(board_millis());
        service_classic_hid_connection();
        service_steam_connection();
        service_switch2_auto_registration();
        service_switch2_wake_policy();
        dualsense_audio_loop(g_ds_output, g_ds_output_sequence);
        service_dualsense_output();
        service_dualshock4_output();
        service_xbox_output();
        controller_feedback_service(board_millis());
        service_switch2_registration();
        service_usb_reports();
        service_usb_reconnect();
        classic_hid_persist_blacklist_if_dirty();
        service_led();
    }
}

} // namespace

int main()
{
    board_init();
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
    tud_disconnect();
    board_init_after_tusb();

    if (cyw43_arch_init() != 0) {
        return 1;
    }
    configure_switch_identity_from_radio();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);

    bt_debug_log_init();
    g_ds_output.reset();
    g_ds4_output.reset();
    usb_descriptors_set_controller_persona(UsbControllerPersona::SwitchPro);
    usb_descriptors_set_audio_persona(UsbAudioPersona::HidOnly);
    dualsense_audio_init();
    switch2_wake_init();
    classic_hid_register_data_callback(on_classic_hid_data);
    steam_hog_bt_register_data_callback(on_steam_hog_bt_data);

    PlayStationInput neutral{};
    neutral.has_imu = true;
    g_switch_usb.set_state(map_playstation_to_switch(neutral));

    if (!classic_hid_init()) {
        return 1;
    }

    app_loop();
    return 0;
}

extern "C" uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                           hid_report_type_t report_type,
                                           uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    if (buffer == nullptr) {
        return 0;
    }
    if (kEnableBtDebugTools && report_type == HID_REPORT_TYPE_FEATURE &&
        report_id == kDebugLogReadFeatureReportId) {
        std::memset(buffer, 0xff, reqlen);
        const uint16_t out_len = static_cast<uint16_t>(bt_debug_log_read(
            g_debug_log_read_offset, buffer, reqlen));
        g_debug_log_read_offset += out_len;
        return reqlen;
    }
    if (kEnableBtDebugTools && report_type == HID_REPORT_TYPE_FEATURE &&
        report_id == kDebugAudioCountersFeatureReportId) {
        return static_cast<uint16_t>(
            dualsense_audio_debug_counters_report(buffer, reqlen));
    }
    if (kEnableBtDebugTools && report_type == HID_REPORT_TYPE_FEATURE &&
        report_id == kDebugClassicCountersFeatureReportId) {
        return static_cast<uint16_t>(
            classic_hid_debug_counters_report(buffer, reqlen));
    }
    if (report_type != HID_REPORT_TYPE_INPUT) {
        return 0;
    }

    SwitchReport report{};
    bool ok = false;
    if (usb_descriptors_controller_persona() ==
        UsbControllerPersona::Switch2ProRegistration) {
        if (report_id == 0 || report_id == 0x09u || report_id == 0x05u) {
            report = build_switch2_registration_input_report();
            ok = true;
        }
    } else if (report_id == kSwitchProFullInputReportId || report_id == 0) {
        report = g_switch_usb.build_input_report();
        ok = true;
    } else if (report_id == kSwitchProUsbResponseReportId) {
        ok = g_switch_usb.build_usb_response(kSwitchUsbCmdHandshake, report);
    } else if (report_id == kSwitchProSubcommandReplyReportId) {
        ok = g_switch_usb.build_subcommand_reply(kSwitchSubcmdRequestDeviceInfo,
                                                 nullptr, 0, report);
    }

    if (!ok) {
        return 0;
    }

    const uint16_t offset = report_id == 0 ? 0 : 1;
    const uint16_t available = static_cast<uint16_t>(report.size() - offset);
    const uint16_t out_len = std::min<uint16_t>(reqlen, available);
    std::memcpy(buffer, report.data() + offset, out_len);
    return out_len;
}

extern "C" void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                                       hid_report_type_t report_type,
                                       uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    if (kEnableBtDebugTools && report_type == HID_REPORT_TYPE_FEATURE &&
        report_id == kDebugLogControlFeatureReportId) {
        if (buffer != nullptr && bufsize >= 4u) {
            g_debug_log_read_offset =
                static_cast<uint32_t>(buffer[0]) |
                (static_cast<uint32_t>(buffer[1]) << 8u) |
                (static_cast<uint32_t>(buffer[2]) << 16u) |
                (static_cast<uint32_t>(buffer[3]) << 24u);
            if (g_debug_log_read_offset > bt_debug_log_size()) {
                g_debug_log_read_offset = bt_debug_log_size();
            }
            if (bufsize >= 5u && buffer[4] == 1u) {
                bt_debug_log_clear();
                g_debug_log_read_offset = 0;
            }
            if (bufsize >= 6u && buffer[5] == 1u) {
                dualsense_audio_reset_debug_counters();
                classic_hid_reset_debug_counters();
            }
        }
        return;
    }
    if (report_type != HID_REPORT_TYPE_OUTPUT || buffer == nullptr || bufsize == 0) {
        return;
    }

    SwitchReport report{};
    std::size_t size = 0;
    if (report_id != 0) {
        report[0] = report_id;
        size = std::min<std::size_t>(bufsize, report.size() - 1u);
        std::memcpy(report.data() + 1, buffer, size);
        ++size;
    } else {
        size = std::min<std::size_t>(bufsize, report.size());
        std::memcpy(report.data(), buffer, size);
    }

    if (usb_descriptors_controller_persona() ==
        UsbControllerPersona::Switch2ProRegistration) {
        uint8_t console_addr[6]{};
        if (procon2_extract_console_addr_wire(report.data(), size,
                                              console_addr)) {
            switch2_wake_set_target_from_usb_pairing(console_addr);
            g_switch2_auto_registration.note_switch_console_seen(board_millis());
        }
        uint8_t ctrl_addr_wire[6]{};
        controller_addr_wire(ctrl_addr_wire);
        const Procon2Reply reply =
            procon2_build_reply(report.data(), size, ctrl_addr_wire);
        if (reply.pairing_finalized) {
            (void)switch2_wake_mark_registration_complete(ctrl_addr_wire);
            g_switch2_auto_registration.note_pairing_finalized();
        }
        if (reply.valid) {
            SwitchReport hid_reply{};
            // HID output reports cap at the 64-byte SwitchReport. The few
            // larger ProCon2 replies (SPI flash reads) only arrive over the
            // vendor bulk path, so clamping here is safe.
            const std::size_t copy_len =
                std::min<std::size_t>(reply.len, hid_reply.size());
            std::copy(reply.data.begin(), reply.data.begin() + copy_len,
                      hid_reply.begin());
            push_pending(hid_reply);
        }
        return;
    }

    const SwitchUsbOutputResult result =
        g_switch_usb.handle_output_report(report.data(), size);
    forward_switch_output_to_bt_procon(report, size);
    if (result.has_switch2_console_addr) {
        switch2_wake_set_target_from_usb_pairing(
            result.switch2_console_addr_wire);
        g_switch2_auto_registration.note_switch_console_seen(board_millis());
    }
    if (steam_hog_bt_is_connected()) {
        (void)steam_hog_bt_send_switch_feedback(result.feedback);
    } else if (!bt_controller_is_switch_pro()) {
        apply_switch_feedback(result.feedback);
    }
    if (result.has_reply) {
        push_pending(result.reply);
    }
    if (result.queue_full_report) {
        push_pending(g_switch_usb.build_input_report());
    }
}

// Switch 2 Pro enumeration-time vendor control requests.
//
// A real Switch 2 console, after SET_CONFIGURATION, issues vendor control
// requests *to the device* before it will talk to the bulk endpoint:
//   * 0xC0 / bRequest 0x03 (wValue 0, wIndex 0) -> 64-byte device-info block
//     (serial + VID/PID + colour data)
//   * 0xC0 / bRequest 0x02 (wValue 0, wIndex 0) -> 16-byte block whose tail is
//     the controller's BD_ADDR
//   * 0x40 / bRequest 0x04 (wValue 0x0276, wLength 0) -> zero-length ACK
// This is captured in BLE-captures/procon2-usb-handshake.pcapng (frames 107,
// 111, 128, 137, 155) and matches the working nRF5340 reference
// (sc_switch2_vendor_to_host / sc_switch2_vendor_to_dev in app/src/sc_usb.c).
//
// TinyUSB routes *every* vendor-type control request to this callback (see
// process_control_request() in usbd.c). Without it the requests NAK then STALL,
// and the console aborts enumeration -> the ProCon2 persona is never
// recognised, which is exactly what BLE-captures/pro2-persona-notworking.pcapng
// shows (the host loops on 0xC0/0x03 forever).
extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                           tusb_control_request_t const *request)
{
    if (request == nullptr) {
        return false;
    }
    if (usb_descriptors_controller_persona() !=
        UsbControllerPersona::Switch2ProRegistration) {
        return false;
    }
    if (stage != CONTROL_STAGE_SETUP) {
        // Data / ACK stages need no further action.
        return true;
    }
    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR ||
        request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_DEVICE) {
        return false;
    }

    if (request->bmRequestType_bit.direction == TUSB_DIR_IN) {
        if (request->bRequest == 0x03u && request->wValue == 0u &&
            request->wIndex == 0u) {
            // 64-byte device-info block, identical to the genuine ProCon2.
            static const uint8_t kVendorInfo03[64] = {
                0x01u, 0x00u, 0x48u, 0x45u, 0x4bu, 0x31u, 0x30u, 0x30u,
                0x30u, 0x30u, 0x32u, 0x32u, 0x30u, 0x38u, 0x39u, 0x36u,
                0x00u, 0x00u, 0x7eu, 0x05u, 0x69u, 0x20u, 0x01u, 0x06u,
                0x01u, 0x23u, 0x23u, 0x23u, 0xa0u, 0xa0u, 0xa0u, 0xe6u,
                0xe6u, 0xe6u, 0x32u, 0x32u, 0x32u, 0xffu, 0xffu, 0xffu,
                0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
                0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
                0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
            };
            return tud_control_xfer(
                rhport, request,
                const_cast<uint8_t *>(kVendorInfo03), sizeof(kVendorInfo03));
        }
        if (request->bRequest == 0x02u && request->wValue == 0u &&
            request->wIndex == 0u) {
            // 16-byte block whose tail carries the controller's own BD_ADDR
            // (same byte order as the 0x15/0x01 bulk business card), so the
            // address the console whitelists is the Pico's real BLE address.
            static uint8_t s_vendor_info02[16] = {
                0x02u, 0x01u, 0x04u, 0x00u, 0x00u, 0x00u, 0x0cu, 0x00u,
                0x02u, 0x03u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
            };
            controller_addr_wire(&s_vendor_info02[10]);
            return tud_control_xfer(rhport, request, s_vendor_info02,
                                    sizeof(s_vendor_info02));
        }
        return false;
    }

    // Vendor OUT: the console issues 0x04 (wValue 0x0276) with no data payload.
    if (request->bRequest == 0x04u && request->wValue == 0x0276u &&
        request->wLength == 0u) {
        return tud_control_status(rhport, request);
    }
    return false;
}

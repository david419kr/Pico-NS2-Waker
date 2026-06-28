#include "ds5onswitch/mapper.h"
#include "ds5onswitch/bootsel_button.h"
#include "ds5onswitch/dualsense_audio.h"
#include "ds5onswitch/dualsense_bt.h"
#include "ds5onswitch/dualsense_output.h"
#include "ds5onswitch/dualsense_report.h"
#include "ds5onswitch/procon2_handshake.h"
#include "ds5onswitch/switch_identity.h"
#include "ds5onswitch/switch_pro.h"
#include "ds5onswitch/switch_usb.h"
#include "ds5onswitch/switch2_wake_runtime.h"
#include "ds5onswitch/usb_descriptors.h"

#include "bsp/board_api.h"
#include "cyw43.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include <algorithm>
#include <array>
#include <cstring>

using namespace ds5onswitch;

namespace {

SwitchUsbEmulator g_switch_usb;
DualSenseOutputState g_ds_output;
std::array<SwitchReport, 3> g_pending_reports{};
uint8_t g_pending_count = 0;
uint8_t g_ds_output_sequence = 0;
uint32_t g_last_haptics_ms = 0;
uint32_t g_last_ds_output_ms = 0;
bool g_ds_output_dirty = false;
UsbAudioPersona g_usb_persona = UsbAudioPersona::HidOnly;
bool g_usb_reconnect_pending = false;
uint32_t g_usb_reconnect_at_ms = 0;
bool g_ds_mute_button_down = false;
bool g_usb_unmounted = false;
uint32_t g_usb_unmounted_since_ms = 0;
uint32_t g_last_ds_buttons = 0;
bool g_home_wake_pending = false;
uint32_t g_home_wake_pending_ms = 0;
bool g_auto_wake_armed = false;
uint32_t g_ds_connected_since_ms = 0;
uint8_t g_switch2_report_counter = 0;

// Manual persona toggle: PS button acts as a modifier; touchpad-click taps
// while PS is held are counted. On PS release, 1 tap selects the Switch Pro 1
// persona (normal use) and 2 taps selects the Switch 2 Pro persona (business
// card / pairing exchange only). Combo-consumed PS/touchpad presses are kept
// out of the game input so they do not register as Home/Capture; PS alone is
// still emitted as a one-frame Home click on release.
bool g_combo_ps_prev = false;
bool g_combo_tp_prev = false;
bool g_combo_suppress_tp_until_release = false;
uint8_t g_combo_tap_count = 0;

constexpr uint32_t kUsbUnmountDebounceMs = 1000;
constexpr uint32_t kHomeWakePendingWindowMs = 3000;

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
        // boots. Do not turn that into 00:00:00:00:00:01; dualsense_bt.cpp
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
    g_usb_persona = dualsense_bt_is_connected()
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

void __not_in_flash_func(on_dualsense_bt_data)(DualSenseBtChannel channel,
                                               const uint8_t *data,
                                               std::size_t size)
{
    if (channel != DualSenseBtChannel::Interrupt) {
        return;
    }

    if (size >= 4u && data[0] == 0xa1 && data[1] == 0x31 &&
        (data[2] & 0x02u) != 0) {
        dualsense_audio_add_mic_frame(data + 4, size - 4u);
        return;
    }

    DualSenseInput input{};
    if (!parse_dualsense_bt_report(data, size, input)) {
        return;
    }

    // --- Manual persona toggle combo (PS held + touchpad-click taps) ---------
    const bool ps_down = (input.buttons & kDs5Ps) != 0;
    const bool tp_down = (input.buttons & kDs5TouchpadClick) != 0;
    const bool ps_release = !ps_down && g_combo_ps_prev;
    const bool tp_edge = tp_down && !g_combo_tp_prev;
    bool combo_consumed_ps = false;
    bool combo_consumed_tp = false;
    bool synthesize_ps_click = false;

    if (g_combo_suppress_tp_until_release) {
        if (tp_down) {
            combo_consumed_tp = true;
        } else {
            g_combo_suppress_tp_until_release = false;
        }
    }

    if (ps_down) {
        // While PS is held, every touchpad click is a combo tap, not Capture.
        combo_consumed_ps = true;
        if (tp_edge && g_combo_tap_count < 0xFFu) {
            ++g_combo_tap_count;
        }
        if (tp_down) {
            combo_consumed_tp = true;
        }
    } else if (ps_release) {
        // PS released: apply the toggle if any touchpad taps were counted.
        if (g_combo_tap_count == 1u) {
            select_switch_pro_persona();
        } else if (g_combo_tap_count >= 2u) {
            select_switch2_pro_persona();
        }
        if (g_combo_tap_count == 0u) {
            // PS alone must keep its original Home/wake role. Since we hold PS
            // back while checking for a combo, emit a one-frame Home click on
            // release when no touchpad tap happened.
            synthesize_ps_click = true;
        } else {
            // A combo was issued; swallow the trailing touchpad state for one
            // or more frames so it does not leak into game input as Capture.
            combo_consumed_tp = tp_down;
            g_combo_suppress_tp_until_release = tp_down;
        }
        g_combo_tap_count = 0u;
    }

    g_combo_ps_prev = ps_down;
    g_combo_tp_prev = tp_down;

    // Strip combo-consumed presses so they never reach the Switch as
    // Home/Capture.
    if (combo_consumed_ps) {
        input.buttons &= ~static_cast<uint32_t>(kDs5Ps);
    }
    if (combo_consumed_tp) {
        input.buttons &= ~static_cast<uint32_t>(kDs5TouchpadClick);
    }
    if (synthesize_ps_click) {
        input.buttons |= static_cast<uint32_t>(kDs5Ps);
    }

    const bool home_down = (input.buttons & kDs5Ps) != 0;
    const bool home_edge = home_down && (g_last_ds_buttons & kDs5Ps) == 0;
    g_last_ds_buttons = input.buttons;
    if (home_edge) {
        g_home_wake_pending = true;
        g_home_wake_pending_ms = board_millis();
    }

    const bool mute_button_down = (input.buttons & kDs5Mute) != 0;
    if (mute_button_down && !g_ds_mute_button_down) {
        const bool muted = dualsense_audio_toggle_mic_muted();
        g_ds_output.set_mic_muted(muted);
        g_ds_output_dirty = true;
    }
    g_ds_mute_button_down = mute_button_down;

    dualsense_audio_update_status(input);
    if (usb_descriptors_controller_persona() == UsbControllerPersona::SwitchPro &&
        dualsense_audio_desired_persona() != g_usb_persona) {
        g_usb_persona = dualsense_audio_desired_persona();
        usb_descriptors_set_audio_persona(g_usb_persona);
        request_usb_reconnect();
    }

    g_switch_usb.set_state(map_dualsense_to_switch(input));
}

void __not_in_flash_func(apply_switch_feedback)(
    const SwitchUsbFeedback &feedback)
{
    if (!feedback.has_rumble) {
        return;
    }

    if (feedback.haptics.active) {
        g_ds_output.apply_haptics(feedback.haptics);
        g_last_haptics_ms = board_millis();
    } else {
        g_ds_output.clear_motors();
    }
    g_ds_output_dirty = true;
}

void __not_in_flash_func(service_dualsense_connection)()
{
    static bool was_connected = false;
    const bool connected = dualsense_bt_is_connected();
    if (connected == was_connected) {
        return;
    }

    was_connected = connected;
    dualsense_audio_set_connected(connected);
    g_ds_output.reset();
    g_ds_output.set_mic_muted(dualsense_audio_mic_muted());
    g_ds_output_dirty = connected;

    if (!connected) {
        g_ds_mute_button_down = false;
        g_last_ds_buttons = 0;
        g_auto_wake_armed = false;
        g_usb_reconnect_pending = false;
        g_combo_ps_prev = false;
        g_combo_tp_prev = false;
        g_combo_suppress_tp_until_release = false;
        g_combo_tap_count = 0;
        usb_descriptors_set_controller_persona(UsbControllerPersona::SwitchPro);
        g_usb_persona = UsbAudioPersona::HidOnly;
        usb_descriptors_set_audio_persona(g_usb_persona);
        tud_disconnect();

        DualSenseInput neutral{};
        neutral.has_imu = true;
        g_switch_usb.set_state(map_dualsense_to_switch(neutral));
        return;
    }

    g_auto_wake_armed = true;
    g_ds_connected_since_ms = board_millis();
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

    if (g_auto_wake_armed && dualsense_bt_is_connected()) {
        if (mounted) {
            g_auto_wake_armed = false;
        } else if (now - g_ds_connected_since_ms >= kUsbUnmountDebounceMs &&
                   usb_sleep_candidate(now) && switch2_wake_has_target()) {
            g_auto_wake_armed = false;
            (void)switch2_wake_request();
        }
    }

    switch2_wake_service(mounted);
}

void __not_in_flash_func(service_dualsense_output)()
{
    if (!dualsense_bt_is_connected()) {
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
    if (dualsense_bt_send_output_report(report)) {
        g_last_ds_output_ms = now;
        g_ds_output_dirty = false;
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
        }

        uint8_t ctrl_addr_wire[6]{};
        controller_addr_wire(ctrl_addr_wire);
        const Procon2Reply reply =
            procon2_build_reply(request, n, ctrl_addr_wire);
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
    } else {
        send_report(g_switch_usb.build_input_report());
    }
}

void __not_in_flash_func(service_usb_reconnect)()
{
    if (!g_usb_reconnect_pending) {
        return;
    }

    if (!dualsense_bt_is_connected()) {
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
    const bool connected = dualsense_bt_is_connected();
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

        cyw43_arch_poll();
        tud_task();
        service_usb_mount_tracking();
        service_dualsense_connection();
        service_switch2_wake_policy();
        dualsense_audio_loop(g_ds_output, g_ds_output_sequence);
        service_dualsense_output();
        service_switch2_registration();
        service_usb_reports();
        service_usb_reconnect();
        dualsense_bt_persist_blacklist_if_dirty();
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

    g_ds_output.reset();
    usb_descriptors_set_controller_persona(UsbControllerPersona::SwitchPro);
    usb_descriptors_set_audio_persona(UsbAudioPersona::HidOnly);
    dualsense_audio_init();
    switch2_wake_init();
    dualsense_bt_register_data_callback(on_dualsense_bt_data);

    DualSenseInput neutral{};
    neutral.has_imu = true;
    g_switch_usb.set_state(map_dualsense_to_switch(neutral));

    if (!dualsense_bt_init()) {
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
    if (buffer == nullptr || report_type != HID_REPORT_TYPE_INPUT) {
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
        }
        uint8_t ctrl_addr_wire[6]{};
        controller_addr_wire(ctrl_addr_wire);
        const Procon2Reply reply =
            procon2_build_reply(report.data(), size, ctrl_addr_wire);
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
    if (result.has_switch2_console_addr) {
        switch2_wake_set_target_from_usb_pairing(
            result.switch2_console_addr_wire);
    }
    apply_switch_feedback(result.feedback);
    if (result.has_reply) {
        push_pending(result.reply);
    }
    if (result.queue_full_report) {
        push_pending(g_switch_usb.build_input_report());
    }
}

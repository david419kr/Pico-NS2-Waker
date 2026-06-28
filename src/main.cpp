#include "ds5onswitch/mapper.h"
#include "ds5onswitch/bootsel_button.h"
#include "ds5onswitch/dualsense_audio.h"
#include "ds5onswitch/dualsense_bt.h"
#include "ds5onswitch/dualsense_output.h"
#include "ds5onswitch/dualsense_report.h"
#include "ds5onswitch/switch_identity.h"
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

constexpr uint32_t kUsbUnmountDebounceMs = 1000;
constexpr uint32_t kHomeWakePendingWindowMs = 3000;

void configure_cyw43_base_mac_for_bt_identity()
{
    static_assert(kSwitchProControllerBdAddr[5] != 0x00,
                  "CYW43 BT transport derives BT address as mac[5] + 1");

    std::memcpy(cyw43_state.mac, kSwitchProControllerBdAddr.data(),
                kSwitchProControllerBdAddr.size());
    --cyw43_state.mac[5];
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
    if (dualsense_audio_desired_persona() != g_usb_persona) {
        g_usb_persona = dualsense_audio_desired_persona();
        usb_descriptors_set_audio_persona(g_usb_persona);
        tud_disconnect();
        g_usb_reconnect_pending = true;
        g_usb_reconnect_at_ms = board_millis() + 250u;
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
        g_usb_persona = UsbAudioPersona::HidOnly;
        usb_descriptors_set_audio_persona(g_usb_persona);
        tud_disconnect();

        DualSenseInput neutral{};
        neutral.has_imu = true;
        g_switch_usb.set_state(map_dualsense_to_switch(neutral));
        return;
    }

    g_usb_persona = dualsense_audio_desired_persona();
    usb_descriptors_set_audio_persona(g_usb_persona);
    tud_disconnect();
    g_usb_reconnect_pending = true;
    g_usb_reconnect_at_ms = board_millis() + 250u;
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
    send_report(g_switch_usb.build_input_report());
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
    configure_cyw43_base_mac_for_bt_identity();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);

    g_ds_output.reset();
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
    if (report_id == kSwitchProFullInputReportId || report_id == 0) {
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

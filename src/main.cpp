#include "ds5onswitch/mapper.h"
#include "ds5onswitch/dualsense_bt.h"
#include "ds5onswitch/dualsense_output.h"
#include "ds5onswitch/dualsense_report.h"
#include "ds5onswitch/switch_usb.h"

#include "bsp/board_api.h"
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

void push_pending(const SwitchReport &report)
{
    if (g_pending_count >= g_pending_reports.size()) {
        return;
    }
    g_pending_reports[g_pending_count++] = report;
}

bool pop_pending(SwitchReport &report)
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

void send_report(const SwitchReport &report)
{
    (void)tud_hid_report(report[0], report.data() + 1, report.size() - 1);
}

void on_dualsense_bt_data(DualSenseBtChannel channel, const uint8_t *data,
                          std::size_t size)
{
    if (channel != DualSenseBtChannel::Interrupt) {
        return;
    }

    DualSenseInput input{};
    if (!parse_dualsense_bt_report(data, size, input)) {
        return;
    }

    g_switch_usb.set_state(map_dualsense_to_switch(input));
}

void apply_switch_feedback(const SwitchUsbFeedback &feedback)
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

void service_dualsense_connection()
{
    static bool was_connected = false;
    const bool connected = dualsense_bt_is_connected();
    if (connected == was_connected) {
        return;
    }

    was_connected = connected;
    g_ds_output.reset();
    g_ds_output_dirty = connected;

    if (!connected) {
        DualSenseInput neutral{};
        neutral.has_imu = true;
        g_switch_usb.set_state(map_dualsense_to_switch(neutral));
    }
}

void service_dualsense_output()
{
    if (!dualsense_bt_is_connected()) {
        return;
    }

    const uint32_t now = board_millis();
    if (g_ds_output.motors_active() && now - g_last_haptics_ms > 96u) {
        g_ds_output.clear_motors();
        g_ds_output_dirty = true;
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

void service_usb_reports()
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

void service_led()
{
    static uint32_t last_ms = 0;
    static bool led = false;
    const uint32_t now = board_millis();
    const uint32_t interval = tud_mounted() ? 1000u : 250u;
    if (now - last_ms < interval) {
        return;
    }

    last_ms = now;
    led = !led;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
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
    board_init_after_tusb();

    if (cyw43_arch_init() != 0) {
        return 1;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);

    g_ds_output.reset();
    dualsense_bt_register_data_callback(on_dualsense_bt_data);

    DualSenseInput neutral{};
    neutral.has_imu = true;
    g_switch_usb.set_state(map_dualsense_to_switch(neutral));

    if (!dualsense_bt_init()) {
        return 1;
    }

    while (true) {
        cyw43_arch_poll();
        tud_task();
        service_dualsense_connection();
        service_dualsense_output();
        service_usb_reports();
        service_led();
    }
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
    apply_switch_feedback(result.feedback);
    if (result.has_reply) {
        push_pending(result.reply);
    }
    if (result.queue_full_report) {
        push_pending(g_switch_usb.build_input_report());
    }
}

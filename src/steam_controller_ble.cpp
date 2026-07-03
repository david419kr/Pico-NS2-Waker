#include "switch_bridge/steam_controller_ble.h"

#include "switch_bridge/ble_controller_hub_internal.h"
#include "switch_bridge/steam_controller_ble_internal.h"
#include "switch_bridge/steam_controller.h"

#include "ad_parser.h"
#include "ble/gatt_client.h"
#include "bluetooth_gatt.h"
#include "btstack_event.h"
#include "btstack_hid.h"
#include "gap.h"
#include "hci.h"
#include "pico/time.h"

#include <cstdio>
#include <cstring>

namespace switchbridge {

namespace {

constexpr uint32_t kInitHeartbeatMs = 800;
constexpr uint32_t kPulseIntervalMs = 20;
constexpr uint16_t kValveCompanyId = 0x055d;
constexpr uint16_t kMicrosoftCompanyId = 0x0006;
constexpr uint8_t kValveGattServiceUuid[16] = {
    0x10, 0x0f, 0x6c, 0x32, 0x17, 0x35, 0x43, 0x13,
    0xb4, 0x02, 0x38, 0x56, 0x71, 0x31, 0xe5, 0xf3};
constexpr uint8_t kValveGattReportCharacteristicUuid[16] = {
    0x10, 0x0f, 0x6c, 0x34, 0x17, 0x35, 0x43, 0x13,
    0xb4, 0x02, 0x38, 0x56, 0x71, 0x31, 0xe5, 0xf3};

enum class ValveGattState : uint8_t {
    Idle,
    DiscoveringService,
    DiscoveringCharacteristic,
    Ready,
    Unavailable,
    WritingPowerOff,
};

ValveGattState g_valve_gatt_state = ValveGattState::Idle;
bool g_valve_service_found = false;
bool g_valve_power_off_pending = false;
gatt_client_service_t g_valve_service{};
gatt_client_characteristic_t g_valve_report_characteristic{};
uint16_t g_valve_report_value_handle = 0;
uint16_t g_valve_report_properties = 0;
uint32_t g_next_heartbeat_ms = 0;
uint32_t g_last_left_pulse_ms = 0;
uint32_t g_last_right_pulse_ms = 0;
uint8_t g_valve_power_off_value[kSteamPowerOffGattValueSize]{};

void valve_gatt_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size);

uint32_t now_ms()
{
    return to_ms_since_boot(get_absolute_time());
}

bool steam_controller_is_active()
{
    return ble_controller_hub_active_kind() == BleControllerKind::SteamController;
}

bool steam_controller_is_connected()
{
    return ble_controller_hub_state_is_connected() && steam_controller_is_active();
}

bool steam_link_open_for_gatt()
{
    return ble_controller_hub_can_use_active_steam_link();
}

bool valve_gatt_busy()
{
    return g_valve_gatt_state == ValveGattState::DiscoveringService ||
           g_valve_gatt_state == ValveGattState::DiscoveringCharacteristic ||
           g_valve_gatt_state == ValveGattState::WritingPowerOff;
}

void reset_valve_gatt()
{
    g_valve_gatt_state = ValveGattState::Idle;
    g_valve_service_found = false;
    g_valve_power_off_pending = false;
    std::memset(&g_valve_service, 0, sizeof(g_valve_service));
    std::memset(&g_valve_report_characteristic, 0,
                sizeof(g_valve_report_characteristic));
    g_valve_report_value_handle = 0;
    g_valve_report_properties = 0;
    std::memset(g_valve_power_off_value, 0, sizeof(g_valve_power_off_value));
}

bool start_valve_gatt_discovery()
{
    if (!steam_link_open_for_gatt() || valve_gatt_busy() ||
        g_valve_gatt_state == ValveGattState::Ready ||
        g_valve_gatt_state == ValveGattState::Unavailable) {
        return false;
    }

    g_valve_service_found = false;
    std::memset(&g_valve_service, 0, sizeof(g_valve_service));
    std::memset(&g_valve_report_characteristic, 0,
                sizeof(g_valve_report_characteristic));
    g_valve_report_value_handle = 0;
    g_valve_report_properties = 0;
    g_valve_gatt_state = ValveGattState::DiscoveringService;
    const uint8_t status = gatt_client_discover_primary_services_by_uuid128(
        valve_gatt_packet_handler, ble_controller_hub_connection_handle(),
        kValveGattServiceUuid);
    if (status != ERROR_CODE_SUCCESS) {
        std::printf("[STEAM-BLE] Valve GATT service discovery failed status=0x%02x\n",
                    status);
        g_valve_gatt_state = ValveGattState::Idle;
        return false;
    }
    return true;
}

bool try_valve_power_off_write()
{
    if (!steam_link_open_for_gatt() ||
        g_valve_gatt_state != ValveGattState::Ready ||
        g_valve_report_value_handle == 0) {
        return false;
    }

    const std::size_t len = build_steam_power_off_gatt_value(
        g_valve_power_off_value, sizeof(g_valve_power_off_value));
    if (len == 0) {
        return false;
    }

    const hci_con_handle_t con_handle = ble_controller_hub_connection_handle();
    uint8_t status = ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
    if ((g_valve_report_properties & ATT_PROPERTY_WRITE) != 0u) {
        g_valve_gatt_state = ValveGattState::WritingPowerOff;
        status = gatt_client_write_value_of_characteristic(
            valve_gatt_packet_handler, con_handle, g_valve_report_value_handle,
            static_cast<uint16_t>(len), g_valve_power_off_value);
        if (status == ERROR_CODE_SUCCESS) {
            g_valve_power_off_pending = false;
            std::printf("[STEAM-BLE] Valve GATT power-off write request queued\n");
            return true;
        }
        g_valve_gatt_state = ValveGattState::Ready;
    }

    if ((g_valve_report_properties & ATT_PROPERTY_WRITE_WITHOUT_RESPONSE) != 0u) {
        status = gatt_client_write_value_of_characteristic_without_response(
            con_handle, g_valve_report_value_handle, static_cast<uint16_t>(len),
            g_valve_power_off_value);
        if (status == ERROR_CODE_SUCCESS) {
            g_valve_power_off_pending = false;
            std::printf("[STEAM-BLE] Valve GATT power-off write command sent\n");
            return true;
        }
    }

    std::printf("[STEAM-BLE] Valve GATT power-off write failed status=0x%02x props=0x%04x\n",
                status, g_valve_report_properties);
    return false;
}

bool try_pending_valve_power_off()
{
    if (!g_valve_power_off_pending || !steam_link_open_for_gatt()) {
        return false;
    }
    if (g_valve_gatt_state == ValveGattState::Ready) {
        return try_valve_power_off_write();
    }
    if (g_valve_gatt_state == ValveGattState::Idle) {
        return start_valve_gatt_discovery();
    }
    if (g_valve_gatt_state == ValveGattState::Unavailable) {
        g_valve_power_off_pending = false;
        return false;
    }
    return valve_gatt_busy();
}

bool ascii_equal_ci(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = static_cast<char>(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
        b = static_cast<char>(b - 'A' + 'a');
    }
    return a == b;
}

bool bytes_contain_ci(const uint8_t *data, uint8_t len, const char *needle)
{
    const std::size_t needle_len = std::strlen(needle);
    if (data == nullptr || len < needle_len) {
        return false;
    }
    for (std::size_t i = 0; i + needle_len <= len; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle_len; ++j) {
            if (!ascii_equal_ci(static_cast<char>(data[i + j]), needle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool adv_name_contains_steam(const uint8_t *data, uint8_t len, bool &has_name)
{
    has_name = false;
    uint8_t pos = 0;
    while (pos < len) {
        const uint8_t field_len = data[pos++];
        if (field_len == 0u) {
            break;
        }
        if (static_cast<uint16_t>(pos) + field_len > len || field_len < 1u) {
            break;
        }
        const uint8_t type = data[pos++];
        const uint8_t value_len = static_cast<uint8_t>(field_len - 1u);
        const uint8_t *value = &data[pos];
        if (type == 0x08u || type == 0x09u) {
            has_name = true;
            if (bytes_contain_ci(value, value_len, "steam")) {
                return true;
            }
        }
        pos = static_cast<uint8_t>(pos + value_len);
    }
    return false;
}

bool ad_data_contains_company_id(const uint8_t *data, uint8_t len,
                                 uint16_t company_id)
{
    if (data == nullptr) {
        return false;
    }

    uint8_t pos = 0;
    while (pos < len) {
        const uint8_t field_len = data[pos++];
        if (field_len == 0u) {
            break;
        }
        if (static_cast<uint16_t>(pos) + field_len > len || field_len < 3u) {
            break;
        }
        const uint8_t type = data[pos++];
        const uint8_t value_len = static_cast<uint8_t>(field_len - 1u);
        const uint8_t *value = &data[pos];
        if (type == 0xffu && value_len >= 2u) {
            const uint16_t observed =
                static_cast<uint16_t>(value[0]) |
                (static_cast<uint16_t>(value[1]) << 8u);
            if (observed == company_id) {
                return true;
            }
        }
        pos = static_cast<uint8_t>(pos + value_len);
    }
    return false;
}

void enqueue_init_commands()
{
    uint8_t report[64]{};
    std::size_t len = build_steam_feature_command(
        kSteamCmdClearDigitalMappings, nullptr, 0, report, sizeof(report));
    if (len != 0u) {
        (void)ble_controller_hub_enqueue_hid_report(
            report, len, HID_REPORT_TYPE_FEATURE);
    }

    len = build_steam_settings_feature_command(report, sizeof(report));
    if (len != 0u) {
        (void)ble_controller_hub_enqueue_hid_report(
            report, len, HID_REPORT_TYPE_FEATURE);
    }
    ble_controller_hub_try_tx();
}

uint16_t pulse_on_us(uint8_t pulse)
{
    return static_cast<uint16_t>(1200u + static_cast<uint16_t>(pulse) * 16u);
}

int16_t pulse_gain_db(uint8_t pulse)
{
    const int16_t gain =
        static_cast<int16_t>(-24 + ((static_cast<uint16_t>(pulse) * 34u + 127u) / 255u));
    return gain;
}

int8_t pulse_tick_gain_db(uint8_t pulse)
{
    const int16_t gain = pulse_gain_db(pulse) / 4;
    return static_cast<int8_t>(gain);
}

void enqueue_pulse(uint8_t side, uint8_t pulse, uint32_t now_ms,
                   uint32_t &last_ms)
{
    if (pulse == 0u) {
        return;
    }
    if (last_ms != 0u && now_ms - last_ms < kPulseIntervalMs) {
        return;
    }
    last_ms = now_ms;

    uint8_t report[10]{};
    std::size_t len = build_steam_trackpad_pulse_output(
        side, pulse_on_us(pulse), 0, 1, pulse_gain_db(pulse), report,
        sizeof(report));
    if (len != 0u) {
        (void)ble_controller_hub_enqueue_hid_report(
            report, len, HID_REPORT_TYPE_OUTPUT);
    }

    len = build_steam_trackpad_command_output(
        side, kSteamHapticCommandTick, pulse_tick_gain_db(pulse), report,
        sizeof(report));
    if (len != 0u) {
        (void)ble_controller_hub_enqueue_hid_report(
            report, len, HID_REPORT_TYPE_OUTPUT);
    }
}

bool enqueue_trackpad_local_haptic(bool left, bool strong_click)
{
    if (!steam_controller_is_connected()) {
        return false;
    }

    const uint8_t side = left ? kSteamTrackpadHapticLeft
                              : kSteamTrackpadHapticRight;
    const uint8_t command = strong_click ? kSteamHapticCommandClick
                                         : kSteamHapticCommandTick;
    const int8_t command_gain = strong_click
                                    ? kSteamTrackpadClickCommandGainDb
                                    : kSteamTrackpadTouchGainDb;

    bool queued = false;
    uint8_t report[64]{};
    std::size_t len = build_steam_trackpad_command_output(
        side, command, command_gain, report, sizeof(report));
    if (len != 0u) {
        queued = ble_controller_hub_enqueue_hid_report(
                     report, len, HID_REPORT_TYPE_OUTPUT) ||
                 queued;
    }

    if (strong_click) {
        len = build_steam_trackpad_feature_pulse_command(
            side, kSteamTrackpadClickPulseUs, 0, 1,
            kSteamTrackpadClickPulseGainDb, report, sizeof(report));
        if (len != 0u) {
            queued = ble_controller_hub_enqueue_hid_report(
                         report, len, HID_REPORT_TYPE_FEATURE) ||
                     queued;
        }
    }
    return queued;
}

void valve_gatt_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
    case GATT_EVENT_SERVICE_QUERY_RESULT:
        if (g_valve_gatt_state == ValveGattState::DiscoveringService) {
            gatt_event_service_query_result_get_service(packet,
                                                        &g_valve_service);
            g_valve_service_found = true;
        }
        break;

    case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
        if (g_valve_gatt_state == ValveGattState::DiscoveringCharacteristic) {
            gatt_event_characteristic_query_result_get_characteristic(
                packet, &g_valve_report_characteristic);
            g_valve_report_value_handle =
                g_valve_report_characteristic.value_handle;
            g_valve_report_properties = g_valve_report_characteristic.properties;
        }
        break;

    case GATT_EVENT_QUERY_COMPLETE: {
        const uint8_t status = gatt_event_query_complete_get_att_status(packet);
        if (g_valve_gatt_state == ValveGattState::DiscoveringService) {
            if (status != ATT_ERROR_SUCCESS || !g_valve_service_found) {
                std::printf("[STEAM-BLE] Valve GATT service not found status=0x%02x\n",
                            status);
                g_valve_gatt_state = ValveGattState::Unavailable;
                g_valve_power_off_pending = false;
                break;
            }
            g_valve_gatt_state = ValveGattState::DiscoveringCharacteristic;
            const uint8_t discover_status =
                gatt_client_discover_characteristics_for_service_by_uuid128(
                    valve_gatt_packet_handler,
                    ble_controller_hub_connection_handle(), &g_valve_service,
                    kValveGattReportCharacteristicUuid);
            if (discover_status != ERROR_CODE_SUCCESS) {
                std::printf("[STEAM-BLE] Valve GATT characteristic discovery failed status=0x%02x\n",
                            discover_status);
                g_valve_gatt_state = ValveGattState::Idle;
            }
            break;
        }

        if (g_valve_gatt_state == ValveGattState::DiscoveringCharacteristic) {
            if (status == ATT_ERROR_SUCCESS && g_valve_report_value_handle != 0) {
                g_valve_gatt_state = ValveGattState::Ready;
                std::printf("[STEAM-BLE] Valve GATT report handle=0x%04x props=0x%04x\n",
                            g_valve_report_value_handle,
                            g_valve_report_properties);
                (void)try_pending_valve_power_off();
            } else {
                std::printf("[STEAM-BLE] Valve GATT report characteristic not found status=0x%02x\n",
                            status);
                g_valve_gatt_state = ValveGattState::Unavailable;
                g_valve_power_off_pending = false;
            }
            break;
        }

        if (g_valve_gatt_state == ValveGattState::WritingPowerOff) {
            g_valve_gatt_state = ValveGattState::Ready;
            std::printf("[STEAM-BLE] Valve GATT power-off write complete status=0x%02x\n",
                        status);
            ble_controller_hub_try_tx();
            break;
        }
        break;
    }

    default:
        break;
    }
}

} // namespace

bool steam_controller_ble_advertisement_matches(const uint8_t *packet)
{
    const uint8_t *ad_data = gap_event_advertising_report_get_data(packet);
    const uint8_t ad_len = gap_event_advertising_report_get_data_length(packet);
    if (ad_data_contains_company_id(ad_data, ad_len, kValveCompanyId)) {
        return true;
    }
    if (ad_data_contains_company_id(ad_data, ad_len, kMicrosoftCompanyId)) {
        return false;
    }

    const bool has_hid =
        ad_data_contains_uuid16(ad_len, ad_data,
                                ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE);
    if (!has_hid) {
        return false;
    }

    bool has_name = false;
    const bool name_is_steam = adv_name_contains_steam(ad_data, ad_len, has_name);
    return name_is_steam || !has_name;
}

bool steam_controller_ble_gatt_busy()
{
    return valve_gatt_busy();
}

void steam_controller_ble_reset()
{
    reset_valve_gatt();
    g_next_heartbeat_ms = 0;
    g_last_left_pulse_ms = 0;
    g_last_right_pulse_ms = 0;
}

void steam_controller_ble_on_hids_connected(uint32_t now_ms)
{
    g_next_heartbeat_ms = now_ms;
    enqueue_init_commands();
}

void steam_controller_ble_on_hid_report_written()
{
    (void)try_pending_valve_power_off();
}

void steam_controller_ble_schedule_heartbeat(uint32_t next_ms)
{
    g_next_heartbeat_ms = next_ms;
}

void steam_controller_ble_service(uint32_t now_ms)
{
    if (!steam_controller_is_active()) {
        return;
    }

    if (g_valve_power_off_pending) {
        (void)try_pending_valve_power_off();
    } else if (steam_controller_is_connected() &&
               g_valve_gatt_state == ValveGattState::Idle &&
               ble_controller_hub_write_ready() &&
               ble_controller_hub_tx_count() == 0u) {
        (void)start_valve_gatt_discovery();
    }

    if (steam_controller_is_connected() &&
        static_cast<int32_t>(now_ms - g_next_heartbeat_ms) >= 0) {
        enqueue_init_commands();
        g_next_heartbeat_ms = now_ms + kInitHeartbeatMs;
    }
}

bool steam_controller_ble_send_switch_feedback(const SwitchUsbFeedback &feedback)
{
    if (!feedback.has_rumble || !steam_controller_is_connected()) {
        return false;
    }

    bool queued = false;
    uint8_t report[10]{};
    const std::size_t rumble_len =
        build_steam_body_rumble_output(feedback.haptics, report,
                                       sizeof(report));
    if (rumble_len != 0u) {
        queued = ble_controller_hub_enqueue_hid_report(
                     report, rumble_len, HID_REPORT_TYPE_OUTPUT) ||
                 queued;
    }

    if (feedback.haptics.active) {
        const uint32_t now = now_ms();
        const uint8_t before = ble_controller_hub_tx_count();
        enqueue_pulse(kSteamTrackpadHapticLeft, feedback.haptics.left_pulse,
                      now, g_last_left_pulse_ms);
        enqueue_pulse(kSteamTrackpadHapticRight, feedback.haptics.right_pulse,
                      now, g_last_right_pulse_ms);
        queued = queued || ble_controller_hub_tx_count() != before;
    }

    ble_controller_hub_try_tx();
    return queued;
}

bool steam_controller_ble_pulse_trackpad_haptic(bool left, bool strong_click)
{
    const bool queued = enqueue_trackpad_local_haptic(left, strong_click);
    if (queued) {
        ble_controller_hub_try_tx();
    }
    return queued;
}

bool steam_controller_ble_power_off()
{
    if (!steam_link_open_for_gatt()) {
        return false;
    }

    ble_controller_hub_clear_tx_queue();
    g_valve_power_off_pending = true;
    const bool started = try_pending_valve_power_off();
    if (!started && g_valve_gatt_state == ValveGattState::Unavailable) {
        return false;
    }
    return started || g_valve_power_off_pending ||
           g_valve_gatt_state == ValveGattState::WritingPowerOff;
}

} // namespace switchbridge

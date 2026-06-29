#include "switch_bridge/steam_hog_bt.h"

#include "switch_bridge/steam_controller.h"
#include "switch_bridge/xbox_controller.h"

#include "ad_parser.h"
#include "ble/gatt-service/hids_client.h"
#include "ble/gatt_client.h"
#include "ble/sm.h"
#include "bluetooth.h"
#include "bluetooth_gatt.h"
#include "btstack_event.h"
#include "btstack_hid.h"
#include "btstack_util.h"
#include "gap.h"
#include "hci.h"
#include "pico/time.h"

#include <cstdio>
#include <cstring>

namespace switchbridge {

namespace {

constexpr uint32_t kReconnectDelayMs = 1000;
constexpr uint32_t kConnectTimeoutMs = 10000;
constexpr uint32_t kInitHeartbeatMs = 800;
constexpr uint32_t kPulseIntervalMs = 20;
constexpr uint16_t kHidsDescriptorStorageSize = 2048;
constexpr std::size_t kTxQueueDepth = 12;
constexpr std::size_t kTxPayloadMax = 64;
constexpr uint8_t kAdvEventDirectInd = 0x01;
constexpr uint16_t kValveCompanyId = 0x055d;
constexpr uint16_t kMicrosoftCompanyId = 0x0006;
constexpr uint8_t kValveGattServiceUuid[16] = {
    0x10, 0x0f, 0x6c, 0x32, 0x17, 0x35, 0x43, 0x13,
    0xb4, 0x02, 0x38, 0x56, 0x71, 0x31, 0xe5, 0xf3};
constexpr uint8_t kValveGattReportCharacteristicUuid[16] = {
    0x10, 0x0f, 0x6c, 0x34, 0x17, 0x35, 0x43, 0x13,
    0xb4, 0x02, 0x38, 0x56, 0x71, 0x31, 0xe5, 0xf3};

enum class SteamHogState : uint8_t {
    Idle,
    Scanning,
    Connecting,
    HidsConnecting,
    Connected,
    Paused,
};

enum class ValveGattState : uint8_t {
    Idle,
    DiscoveringService,
    DiscoveringCharacteristic,
    Ready,
    Unavailable,
    WritingPowerOff,
};

struct TxReport {
    uint8_t report_id = 0;
    hid_report_type_t report_type = HID_REPORT_TYPE_INPUT;
    uint8_t len = 0;
    uint8_t payload[kTxPayloadMax]{};
};

btstack_packet_callback_registration_t g_hci_callback{};
btstack_packet_callback_registration_t g_sm_callback{};
bd_addr_t g_remote_addr{};
bd_addr_type_t g_remote_addr_type = BD_ADDR_TYPE_LE_PUBLIC;
hci_con_handle_t g_con_handle = HCI_CON_HANDLE_INVALID;
uint16_t g_hids_cid = 0;
SteamHogBtDataCallback g_data_callback = nullptr;
SteamHogState g_state = SteamHogState::Idle;
uint8_t g_hid_descriptor_storage[kHidsDescriptorStorageSize]{};
TxReport g_tx_queue[kTxQueueDepth]{};
uint8_t g_tx_head = 0;
uint8_t g_tx_count = 0;
uint32_t g_state_ms = 0;
uint32_t g_next_scan_ms = 0;
uint32_t g_next_heartbeat_ms = 0;
uint32_t g_last_left_pulse_ms = 0;
uint32_t g_last_right_pulse_ms = 0;
bool g_stack_ready = false;
bool g_write_ready = false;
bool g_xbox_input_probe_pending = false;
bool g_scan_suppressed = false;
SteamHogBtControllerKind g_controller_kind = SteamHogBtControllerKind::Unknown;
SteamHogBtControllerKind g_last_seen_kind = SteamHogBtControllerKind::Unknown;
ValveGattState g_valve_gatt_state = ValveGattState::Idle;
bool g_valve_service_found = false;
bool g_valve_power_off_pending = false;
gatt_client_service_t g_valve_service{};
gatt_client_characteristic_t g_valve_report_characteristic{};
uint16_t g_valve_report_value_handle = 0;
uint16_t g_valve_report_properties = 0;
uint8_t g_valve_power_off_value[kSteamPowerOffGattValueSize]{};

void valve_gatt_packet_handler(uint8_t packet_type, uint16_t channel,
                               uint8_t *packet, uint16_t size);

uint32_t now_ms()
{
    return to_ms_since_boot(get_absolute_time());
}

bool state_is_connected()
{
    return g_state == SteamHogState::Connected && g_hids_cid != 0;
}

bool can_write_hid_report()
{
    return (g_state == SteamHogState::Connected ||
            g_state == SteamHogState::Paused) &&
           g_hids_cid != 0;
}

bool steam_link_open_for_gatt()
{
    return (g_state == SteamHogState::Connected ||
            g_state == SteamHogState::Paused) &&
           g_con_handle != HCI_CON_HANDLE_INVALID;
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
        valve_gatt_packet_handler, g_con_handle, kValveGattServiceUuid);
    if (status != ERROR_CODE_SUCCESS) {
        std::printf("[SC-BLE] Valve GATT service discovery failed status=0x%02x\n",
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

    uint8_t status = ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
    if ((g_valve_report_properties & ATT_PROPERTY_WRITE) != 0u) {
        g_valve_gatt_state = ValveGattState::WritingPowerOff;
        status = gatt_client_write_value_of_characteristic(
            valve_gatt_packet_handler, g_con_handle, g_valve_report_value_handle,
            static_cast<uint16_t>(len), g_valve_power_off_value);
        if (status == ERROR_CODE_SUCCESS) {
            g_valve_power_off_pending = false;
            std::printf("[SC-BLE] Valve GATT power-off write request queued\n");
            return true;
        }
        g_valve_gatt_state = ValveGattState::Ready;
    }

    if ((g_valve_report_properties & ATT_PROPERTY_WRITE_WITHOUT_RESPONSE) != 0u) {
        status = gatt_client_write_value_of_characteristic_without_response(
            g_con_handle, g_valve_report_value_handle, static_cast<uint16_t>(len),
            g_valve_power_off_value);
        if (status == ERROR_CODE_SUCCESS) {
            g_valve_power_off_pending = false;
            std::printf("[SC-BLE] Valve GATT power-off write command sent\n");
            return true;
        }
    }

    std::printf("[SC-BLE] Valve GATT power-off write failed status=0x%02x props=0x%04x\n",
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

bool adv_name_contains_xbox(const uint8_t *data, uint8_t len, bool &has_name)
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
            if (bytes_contain_ci(value, value_len, "xbox")) {
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

bool adv_is_steam_controller(const uint8_t *packet)
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

bool adv_is_xbox_controller(const uint8_t *packet)
{
    const uint8_t *ad_data = gap_event_advertising_report_get_data(packet);
    const uint8_t ad_len = gap_event_advertising_report_get_data_length(packet);
    const bool has_hid =
        ad_data_contains_uuid16(ad_len, ad_data,
                                ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE);
    if (!has_hid) {
        return false;
    }

    bool has_name = false;
    if (adv_name_contains_xbox(ad_data, ad_len, has_name)) {
        return true;
    }
    return ad_data_contains_company_id(ad_data, ad_len, kMicrosoftCompanyId);
}

SteamHogBtControllerKind kind_from_hog_report(const uint8_t *report,
                                              uint16_t report_len)
{
    if (report == nullptr || report_len == 0u) {
        return SteamHogBtControllerKind::Unknown;
    }
    if (report[0] == kSteamControllerReportState ||
        report[0] == kSteamControllerReportStateLegacy) {
        return SteamHogBtControllerKind::SteamController;
    }
    if (report_len == kXboxElite2InputPayloadSize) {
        return SteamHogBtControllerKind::Xbox;
    }
    if (report_len >= kXboxElite2InputReportSize &&
        (report[0] == kXboxInputReportId || report[0] == 0x00u)) {
        return SteamHogBtControllerKind::Xbox;
    }
    if (report_len == kXboxInputReportMinSize &&
        (report[0] == kXboxInputReportId || report[0] == 0x00u)) {
        return SteamHogBtControllerKind::Xbox;
    }
    return SteamHogBtControllerKind::Unknown;
}

bool adv_is_steam_reconnect_directed(const uint8_t *packet)
{
    return gap_event_advertising_report_get_advertising_event_type(packet) ==
           kAdvEventDirectInd;
}

void tx_pop_front()
{
    if (g_tx_count == 0) {
        return;
    }
    g_tx_head = static_cast<uint8_t>((g_tx_head + 1u) % kTxQueueDepth);
    --g_tx_count;
}

bool enqueue_write(uint8_t report_id, hid_report_type_t report_type,
                   const uint8_t *payload, uint8_t len)
{
    if (payload == nullptr && len != 0u) {
        return false;
    }
    if (len > kTxPayloadMax || g_tx_count >= kTxQueueDepth) {
        return false;
    }

    const uint8_t index =
        static_cast<uint8_t>((g_tx_head + g_tx_count) % kTxQueueDepth);
    TxReport &slot = g_tx_queue[index];
    slot.report_id = report_id;
    slot.report_type = report_type;
    slot.len = len;
    if (len != 0u) {
        std::memcpy(slot.payload, payload, len);
    }
    ++g_tx_count;
    return true;
}

bool enqueue_hid_report(const uint8_t *report, std::size_t report_len,
                        hid_report_type_t report_type)
{
    if (report == nullptr || report_len == 0u || report_len - 1u > kTxPayloadMax) {
        return false;
    }
    return enqueue_write(report[0], report_type, report + 1u,
                         static_cast<uint8_t>(report_len - 1u));
}

void try_tx()
{
    if (valve_gatt_busy()) {
        return;
    }
    while (can_write_hid_report() && g_write_ready && g_tx_count != 0u) {
        TxReport &front = g_tx_queue[g_tx_head];
        const uint8_t status = hids_client_send_write_report(
            g_hids_cid, front.report_id, front.report_type, front.payload,
            front.len);
        if (status == ERROR_CODE_SUCCESS) {
            tx_pop_front();
            g_write_ready = false;
            return;
        }
        if (status == ERROR_CODE_COMMAND_DISALLOWED) {
            return;
        }
        std::printf("[SC-BLE] Drop report 0x%02x type=%u status=0x%02x\n",
                    front.report_id, static_cast<unsigned>(front.report_type),
                    status);
        tx_pop_front();
    }
}

void try_xbox_input_probe()
{
    if (!g_xbox_input_probe_pending || !state_is_connected() ||
        g_controller_kind != SteamHogBtControllerKind::Xbox) {
        return;
    }

    uint8_t status = hids_client_send_get_report(
        g_hids_cid, kXboxInputReportId, HID_REPORT_TYPE_INPUT);
    if (status == ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE) {
        status = hids_client_send_get_report(g_hids_cid, 0,
                                             HID_REPORT_TYPE_INPUT);
    }
    if (status == ERROR_CODE_SUCCESS ||
        status == ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE ||
        status == ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER) {
        g_xbox_input_probe_pending = false;
    }
}

void enqueue_init_commands()
{
    uint8_t report[64]{};
    std::size_t len = build_steam_feature_command(
        kSteamCmdClearDigitalMappings, nullptr, 0, report, sizeof(report));
    if (len != 0u) {
        (void)enqueue_hid_report(report, len, HID_REPORT_TYPE_FEATURE);
    }

    len = build_steam_settings_feature_command(report, sizeof(report));
    if (len != 0u) {
        (void)enqueue_hid_report(report, len, HID_REPORT_TYPE_FEATURE);
    }
    try_tx();
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
        (void)enqueue_hid_report(report, len, HID_REPORT_TYPE_OUTPUT);
    }

    len = build_steam_trackpad_command_output(
        side, kSteamHapticCommandTick, pulse_tick_gain_db(pulse), report,
        sizeof(report));
    if (len != 0u) {
        (void)enqueue_hid_report(report, len, HID_REPORT_TYPE_OUTPUT);
    }
}

bool enqueue_trackpad_local_haptic(bool left, bool strong_click)
{
    if (!state_is_connected() ||
        g_controller_kind != SteamHogBtControllerKind::SteamController) {
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
        queued = enqueue_hid_report(report, len, HID_REPORT_TYPE_OUTPUT) ||
                 queued;
    }

    if (strong_click) {
        len = build_steam_trackpad_feature_pulse_command(
            side, kSteamTrackpadClickPulseUs, 0, 1,
            kSteamTrackpadClickPulseGainDb, report, sizeof(report));
        if (len != 0u) {
            queued = enqueue_hid_report(report, len, HID_REPORT_TYPE_FEATURE) ||
                     queued;
        }
    }
    return queued;
}

void clear_runtime_state(bool schedule_scan)
{
    reset_valve_gatt();
    g_controller_kind = SteamHogBtControllerKind::Unknown;
    g_con_handle = HCI_CON_HANDLE_INVALID;
    g_hids_cid = 0;
    g_write_ready = false;
    g_xbox_input_probe_pending = false;
    g_tx_head = 0;
    g_tx_count = 0;
    g_next_heartbeat_ms = 0;
    g_last_left_pulse_ms = 0;
    g_last_right_pulse_ms = 0;
    if (g_state != SteamHogState::Paused) {
        g_state = SteamHogState::Idle;
    }
    if (schedule_scan) {
        g_next_scan_ms = now_ms() + kReconnectDelayMs;
    }
}

void start_scan(uint32_t now_ms)
{
    if (g_scan_suppressed || !g_stack_ready || g_state != SteamHogState::Idle ||
        static_cast<int32_t>(now_ms - g_next_scan_ms) < 0) {
        return;
    }

    std::printf("[SC-BLE] Start LE HID scan\n");
    gap_set_scan_parameters(1, 48, 48);
    gap_start_scan();
    g_state = SteamHogState::Scanning;
    g_state_ms = now_ms;
}

void stop_scan()
{
    if (g_state == SteamHogState::Scanning) {
        gap_stop_scan();
    }
}

void connect_to_advertiser(uint8_t *packet, SteamHogBtControllerKind kind)
{
    gap_event_advertising_report_get_address(packet, g_remote_addr);
    g_remote_addr_type = static_cast<bd_addr_type_t>(
        gap_event_advertising_report_get_address_type(packet));
    g_controller_kind =
        kind == SteamHogBtControllerKind::Unknown ? g_last_seen_kind : kind;
    if (g_controller_kind != SteamHogBtControllerKind::Unknown) {
        g_last_seen_kind = g_controller_kind;
    }
    gap_stop_scan();
    g_state = SteamHogState::Connecting;
    g_state_ms = now_ms();
    std::printf("[SC-BLE] Connect %s type=%u kind=%u\n",
                bd_addr_to_str(g_remote_addr),
                static_cast<unsigned>(g_remote_addr_type),
                static_cast<unsigned>(g_controller_kind));
    const uint8_t status = gap_connect(g_remote_addr, g_remote_addr_type);
    if (status != ERROR_CODE_SUCCESS) {
        std::printf("[SC-BLE] gap_connect failed status=0x%02x\n", status);
        clear_runtime_state(true);
    }
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
                std::printf("[SC-BLE] Valve GATT service not found status=0x%02x\n",
                            status);
                g_valve_gatt_state = ValveGattState::Unavailable;
                g_valve_power_off_pending = false;
                break;
            }
            g_valve_gatt_state = ValveGattState::DiscoveringCharacteristic;
            const uint8_t discover_status =
                gatt_client_discover_characteristics_for_service_by_uuid128(
                    valve_gatt_packet_handler, g_con_handle, &g_valve_service,
                    kValveGattReportCharacteristicUuid);
            if (discover_status != ERROR_CODE_SUCCESS) {
                std::printf("[SC-BLE] Valve GATT characteristic discovery failed status=0x%02x\n",
                            discover_status);
                g_valve_gatt_state = ValveGattState::Idle;
            }
            break;
        }

        if (g_valve_gatt_state == ValveGattState::DiscoveringCharacteristic) {
            if (status == ATT_ERROR_SUCCESS && g_valve_report_value_handle != 0) {
                g_valve_gatt_state = ValveGattState::Ready;
                std::printf("[SC-BLE] Valve GATT report handle=0x%04x props=0x%04x\n",
                            g_valve_report_value_handle,
                            g_valve_report_properties);
                (void)try_pending_valve_power_off();
            } else {
                std::printf("[SC-BLE] Valve GATT report characteristic not found status=0x%02x\n",
                            status);
                g_valve_gatt_state = ValveGattState::Unavailable;
                g_valve_power_off_pending = false;
            }
            break;
        }

        if (g_valve_gatt_state == ValveGattState::WritingPowerOff) {
            g_valve_gatt_state = ValveGattState::Ready;
            std::printf("[SC-BLE] Valve GATT power-off write complete status=0x%02x\n",
                        status);
            try_tx();
            break;
        }
        break;
    }

    default:
        break;
    }
}

void hids_packet_handler(uint8_t packet_type, uint16_t channel,
                         uint8_t *packet, uint16_t size)
{
    (void)packet_type;
    (void)channel;
    (void)size;

    if (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META) {
        return;
    }

    switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
    case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
        const uint8_t status =
            gattservice_subevent_hid_service_connected_get_status(packet);
        if (status != ERROR_CODE_SUCCESS) {
            std::printf("[SC-BLE] HIDS connect failed status=0x%02x\n", status);
            if (g_con_handle != HCI_CON_HANDLE_INVALID) {
                (void)gap_disconnect(g_con_handle);
            }
            clear_runtime_state(true);
            return;
        }
        g_hids_cid = gattservice_subevent_hid_service_connected_get_hids_cid(packet);
        g_state = SteamHogState::Connected;
        g_write_ready = true;
        g_next_heartbeat_ms = now_ms();
        std::printf("[SC-BLE] HIDS connected cid=0x%04x services=%u\n",
                    g_hids_cid,
                    gattservice_subevent_hid_service_connected_get_num_instances(packet));
        if (g_controller_kind == SteamHogBtControllerKind::SteamController) {
            enqueue_init_commands();
        } else if (g_controller_kind == SteamHogBtControllerKind::Xbox) {
            g_xbox_input_probe_pending = true;
        }
        break;
    }

    case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED:
        std::printf("[SC-BLE] HIDS disconnected\n");
        clear_runtime_state(true);
        break;

    case GATTSERVICE_SUBEVENT_HID_REPORT: {
        if (!state_is_connected() ||
            gattservice_subevent_hid_report_get_hids_cid(packet) != g_hids_cid) {
            break;
        }
        const uint8_t *report =
            gattservice_subevent_hid_report_get_report(packet);
        const uint16_t report_len =
            gattservice_subevent_hid_report_get_report_len(packet);
        const SteamHogBtControllerKind report_kind =
            kind_from_hog_report(report, report_len);
        if (report_kind != SteamHogBtControllerKind::Unknown &&
            report_kind != g_controller_kind) {
            g_controller_kind = report_kind;
            g_last_seen_kind = report_kind;
        }
        if (g_data_callback != nullptr && report_len != 0u) {
            g_data_callback(g_controller_kind, report, report_len);
        }
        break;
    }

    case GATTSERVICE_SUBEVENT_HID_REPORT_WRITTEN:
        if (gattservice_subevent_hid_report_written_get_hids_cid(packet) ==
            g_hids_cid) {
            g_write_ready = true;
            (void)try_pending_valve_power_off();
            try_tx();
        }
        break;

    default:
        break;
    }
}

void sm_packet_handler(uint8_t packet_type, uint16_t channel,
                       uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
    case SM_EVENT_JUST_WORKS_REQUEST: {
        const hci_con_handle_t handle =
            sm_event_just_works_request_get_handle(packet);
        if (handle == g_con_handle) {
            std::printf("[SC-BLE] Accept Just Works pairing\n");
            sm_just_works_confirm(handle);
        }
        break;
    }

    case SM_EVENT_PAIRING_COMPLETE: {
        const hci_con_handle_t handle =
            sm_event_pairing_complete_get_handle(packet);
        if (handle == g_con_handle) {
            const uint8_t status = sm_event_pairing_complete_get_status(packet);
            std::printf("[SC-BLE] Pairing complete status=0x%02x reason=0x%02x\n",
                        status, sm_event_pairing_complete_get_reason(packet));
        }
        break;
    }

    default:
        break;
    }
}

void hci_packet_handler(uint8_t packet_type, uint16_t channel,
                        uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            g_stack_ready = true;
            g_next_scan_ms = now_ms();
            start_scan(g_next_scan_ms);
        }
        break;

    case GAP_EVENT_ADVERTISING_REPORT:
        if (g_state == SteamHogState::Scanning) {
            if (adv_is_xbox_controller(packet)) {
                connect_to_advertiser(packet,
                                      SteamHogBtControllerKind::Xbox);
            } else if (adv_is_steam_controller(packet)) {
                connect_to_advertiser(
                    packet, SteamHogBtControllerKind::SteamController);
            } else if (adv_is_steam_reconnect_directed(packet)) {
                connect_to_advertiser(
                    packet, SteamHogBtControllerKind::Unknown);
            }
        }
        break;

    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) ==
            GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
            if (g_state != SteamHogState::Connecting) {
                break;
            }
            const uint8_t status =
                gap_subevent_le_connection_complete_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                std::printf("[SC-BLE] LE connect failed status=0x%02x\n",
                            status);
                clear_runtime_state(true);
                break;
            }
            g_con_handle =
                gap_subevent_le_connection_complete_get_connection_handle(packet);
            reset_valve_gatt();
            g_state = SteamHogState::HidsConnecting;
            g_state_ms = now_ms();
            const uint8_t hids_status = hids_client_connect(
                g_con_handle, hids_packet_handler, HID_PROTOCOL_MODE_REPORT,
                &g_hids_cid);
            if (hids_status != ERROR_CODE_SUCCESS) {
                std::printf("[SC-BLE] hids_client_connect failed status=0x%02x\n",
                            hids_status);
                (void)gap_disconnect(g_con_handle);
                clear_runtime_state(true);
            }
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        const hci_con_handle_t handle =
            hci_event_disconnection_complete_get_connection_handle(packet);
        if (handle == g_con_handle) {
            std::printf("[SC-BLE] LE disconnected\n");
            clear_runtime_state(true);
        }
        break;
    }

    default:
        break;
    }
}

} // namespace

void steam_hog_bt_register_data_callback(SteamHogBtDataCallback callback)
{
    g_data_callback = callback;
}

void steam_hog_bt_stack_init()
{
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION |
                                       SM_AUTHREQ_BONDING);

    gatt_client_init();
    hids_client_init(g_hid_descriptor_storage,
                     sizeof(g_hid_descriptor_storage));

    g_hci_callback.callback = &hci_packet_handler;
    hci_add_event_handler(&g_hci_callback);

    g_sm_callback.callback = &sm_packet_handler;
    sm_add_event_handler(&g_sm_callback);
}

void steam_hog_bt_service(uint32_t now_ms)
{
    if (g_state == SteamHogState::Paused) {
        (void)try_pending_valve_power_off();
        return;
    }

    if (g_state == SteamHogState::Idle) {
        start_scan(now_ms);
    } else if (g_state == SteamHogState::Connecting ||
               g_state == SteamHogState::HidsConnecting) {
        if (now_ms - g_state_ms >= kConnectTimeoutMs) {
            std::printf("[SC-BLE] Connect timeout\n");
            (void)gap_connect_cancel();
            if (g_con_handle != HCI_CON_HANDLE_INVALID) {
                (void)gap_disconnect(g_con_handle);
            }
            clear_runtime_state(true);
        }
    }

    if (state_is_connected() &&
        g_controller_kind == SteamHogBtControllerKind::SteamController) {
        if (g_valve_power_off_pending) {
            (void)try_pending_valve_power_off();
        } else if (g_valve_gatt_state == ValveGattState::Idle &&
                   g_write_ready && g_tx_count == 0u) {
            (void)start_valve_gatt_discovery();
        }
    }

    if (state_is_connected() &&
        g_controller_kind == SteamHogBtControllerKind::SteamController &&
        static_cast<int32_t>(now_ms - g_next_heartbeat_ms) >= 0) {
        enqueue_init_commands();
        g_next_heartbeat_ms = now_ms + kInitHeartbeatMs;
    }
    try_xbox_input_probe();
    try_tx();
}

bool steam_hog_bt_is_connected()
{
    return state_is_connected();
}

SteamHogBtControllerKind steam_hog_bt_controller_kind()
{
    return state_is_connected() ? g_controller_kind
                                : SteamHogBtControllerKind::Unknown;
}

bool steam_hog_bt_send_switch_feedback(const SwitchUsbFeedback &feedback)
{
    if (!state_is_connected() || !feedback.has_rumble) {
        return false;
    }

    if (g_controller_kind == SteamHogBtControllerKind::Xbox) {
        uint8_t report[kXboxRumbleReportSize]{};
        const std::size_t len =
            build_xbox_rumble_output(feedback.haptics, report, sizeof(report));
        if (len == 0u) {
            return false;
        }
        const bool queued =
            enqueue_hid_report(report, len, HID_REPORT_TYPE_OUTPUT);
        try_tx();
        return queued;
    }

    if (g_controller_kind != SteamHogBtControllerKind::SteamController) {
        return false;
    }

    bool queued = false;
    uint8_t report[10]{};
    const std::size_t rumble_len =
        build_steam_body_rumble_output(feedback.haptics, report,
                                       sizeof(report));
    if (rumble_len != 0u) {
        queued = enqueue_hid_report(report, rumble_len, HID_REPORT_TYPE_OUTPUT) ||
                 queued;
    }

    if (feedback.haptics.active) {
        const uint32_t now = now_ms();
        const uint8_t before = g_tx_count;
        enqueue_pulse(kSteamTrackpadHapticLeft, feedback.haptics.left_pulse,
                      now, g_last_left_pulse_ms);
        enqueue_pulse(kSteamTrackpadHapticRight, feedback.haptics.right_pulse,
                      now, g_last_right_pulse_ms);
        queued = queued || g_tx_count != before;
    }

    try_tx();
    return queued;
}

bool steam_hog_bt_pulse_trackpad_haptic(bool left, bool strong_click)
{
    const bool queued = enqueue_trackpad_local_haptic(left, strong_click);
    if (queued) {
        try_tx();
    }
    return queued;
}

bool steam_hog_bt_disconnect()
{
    if (g_state == SteamHogState::Scanning) {
        gap_stop_scan();
        g_state = SteamHogState::Idle;
        return true;
    }
    if (g_state == SteamHogState::Connecting) {
        (void)gap_connect_cancel();
        clear_runtime_state(true);
        return true;
    }
    if (g_hids_cid != 0) {
        return hids_client_disconnect(g_hids_cid) == ERROR_CODE_SUCCESS;
    }
    if (g_con_handle != HCI_CON_HANDLE_INVALID) {
        return gap_disconnect(g_con_handle) == ERROR_CODE_SUCCESS;
    }
    return false;
}

bool steam_hog_bt_power_off()
{
    if (!steam_link_open_for_gatt() ||
        g_controller_kind != SteamHogBtControllerKind::SteamController) {
        return false;
    }

    g_tx_head = 0;
    g_tx_count = 0;
    g_valve_power_off_pending = true;
    const bool started = try_pending_valve_power_off();
    if (!started && g_valve_gatt_state == ValveGattState::Unavailable) {
        return false;
    }
    return started || g_valve_power_off_pending ||
           g_valve_gatt_state == ValveGattState::WritingPowerOff;
}

void steam_hog_bt_pause_for_switch2_wake()
{
    if (g_state == SteamHogState::Paused) {
        return;
    }

    std::printf("[SC-BLE] Pause for Switch 2 wake\n");
    if (g_state == SteamHogState::Scanning) {
        gap_stop_scan();
    } else if (g_state == SteamHogState::Connecting) {
        (void)gap_connect_cancel();
    }
    g_state = SteamHogState::Paused;
}

void steam_hog_bt_resume_after_switch2_wake()
{
    if (g_state != SteamHogState::Paused) {
        return;
    }

    std::printf("[SC-BLE] Resume after Switch 2 wake\n");
    if (g_hids_cid != 0) {
        g_state = SteamHogState::Connected;
        g_next_heartbeat_ms = now_ms();
        try_tx();
        return;
    }
    if (g_con_handle != HCI_CON_HANDLE_INVALID) {
        g_state = SteamHogState::HidsConnecting;
        g_state_ms = now_ms();
        return;
    }
    clear_runtime_state(false);
    g_state = SteamHogState::Idle;
    g_next_scan_ms = now_ms() + kReconnectDelayMs;
}

void steam_hog_bt_set_scan_suppressed(bool suppressed)
{
    if (g_scan_suppressed == suppressed) {
        return;
    }
    g_scan_suppressed = suppressed;
    if (suppressed) {
        if (g_state == SteamHogState::Scanning) {
            gap_stop_scan();
            g_state = SteamHogState::Idle;
        } else if (g_state == SteamHogState::Connecting) {
            (void)gap_connect_cancel();
            clear_runtime_state(false);
        }
        return;
    }
    if (g_state == SteamHogState::Idle) {
        g_next_scan_ms = now_ms() + kReconnectDelayMs;
    }
}

} // namespace switchbridge

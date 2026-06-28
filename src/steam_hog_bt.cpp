#include "ds5onswitch/steam_hog_bt.h"

#include "ds5onswitch/steam_controller.h"

#include "ad_parser.h"
#include "ble/gatt-service/hids_client.h"
#include "ble/gatt_client.h"
#include "ble/sm.h"
#include "bluetooth_gatt.h"
#include "btstack_event.h"
#include "btstack_hid.h"
#include "btstack_util.h"
#include "gap.h"
#include "hci.h"
#include "pico/time.h"

#include <cstdio>
#include <cstring>

namespace ds5onswitch {

namespace {

constexpr uint32_t kReconnectDelayMs = 1000;
constexpr uint32_t kConnectTimeoutMs = 10000;
constexpr uint32_t kInitHeartbeatMs = 800;
constexpr uint32_t kPulseIntervalMs = 20;
constexpr uint16_t kHidsDescriptorStorageSize = 1024;
constexpr std::size_t kTxQueueDepth = 12;
constexpr std::size_t kTxPayloadMax = 63;

enum class SteamHogState : uint8_t {
    Idle,
    Scanning,
    Connecting,
    HidsConnecting,
    Connected,
    Paused,
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

uint32_t now_ms()
{
    return to_ms_since_boot(get_absolute_time());
}

bool state_is_connected()
{
    return g_state == SteamHogState::Connected && g_hids_cid != 0;
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

bool adv_is_steam_controller(const uint8_t *packet)
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
    const bool name_is_steam = adv_name_contains_steam(ad_data, ad_len, has_name);
    return name_is_steam || !has_name;
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
    while (state_is_connected() && g_write_ready && g_tx_count != 0u) {
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

void clear_runtime_state(bool schedule_scan)
{
    g_con_handle = HCI_CON_HANDLE_INVALID;
    g_hids_cid = 0;
    g_write_ready = false;
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
    if (!g_stack_ready || g_state != SteamHogState::Idle ||
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

void connect_to_advertiser(uint8_t *packet)
{
    gap_event_advertising_report_get_address(packet, g_remote_addr);
    g_remote_addr_type = static_cast<bd_addr_type_t>(
        gap_event_advertising_report_get_address_type(packet));
    gap_stop_scan();
    g_state = SteamHogState::Connecting;
    g_state_ms = now_ms();
    std::printf("[SC-BLE] Connect %s type=%u\n", bd_addr_to_str(g_remote_addr),
                static_cast<unsigned>(g_remote_addr_type));
    const uint8_t status = gap_connect(g_remote_addr, g_remote_addr_type);
    if (status != ERROR_CODE_SUCCESS) {
        std::printf("[SC-BLE] gap_connect failed status=0x%02x\n", status);
        clear_runtime_state(true);
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
        enqueue_init_commands();
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
        if (g_data_callback != nullptr && report_len != 0u) {
            g_data_callback(report, report_len);
        }
        break;
    }

    case GATTSERVICE_SUBEVENT_HID_REPORT_WRITTEN:
        if (gattservice_subevent_hid_report_written_get_hids_cid(packet) ==
            g_hids_cid) {
            g_write_ready = true;
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
        if (g_state == SteamHogState::Scanning &&
            adv_is_steam_controller(packet)) {
            connect_to_advertiser(packet);
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
        static_cast<int32_t>(now_ms - g_next_heartbeat_ms) >= 0) {
        enqueue_init_commands();
        g_next_heartbeat_ms = now_ms + kInitHeartbeatMs;
    }
    try_tx();
}

bool steam_hog_bt_is_connected()
{
    return state_is_connected();
}

bool steam_hog_bt_send_switch_feedback(const SwitchUsbFeedback &feedback)
{
    if (!state_is_connected() || !feedback.has_rumble) {
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
    if (!state_is_connected()) {
        return false;
    }

    bool queued = false;
    uint8_t report[5]{};
    const std::size_t len = build_steam_power_off_output(report, sizeof(report));
    if (len == 0u) {
        return false;
    }
    for (uint8_t i = 0; i < 3u; ++i) {
        queued = enqueue_hid_report(report, len, HID_REPORT_TYPE_OUTPUT) ||
                 queued;
    }
    try_tx();
    return queued;
}

void steam_hog_bt_pause_for_switch2_wake()
{
    if (g_state == SteamHogState::Paused) {
        return;
    }

    std::printf("[SC-BLE] Pause for Switch 2 wake\n");
    if (g_state == SteamHogState::Scanning) {
        gap_stop_scan();
    }
    (void)steam_hog_bt_power_off();
    (void)steam_hog_bt_disconnect();
    g_state = SteamHogState::Paused;
}

void steam_hog_bt_resume_after_switch2_wake()
{
    if (g_state != SteamHogState::Paused) {
        return;
    }

    std::printf("[SC-BLE] Resume after Switch 2 wake\n");
    clear_runtime_state(false);
    g_state = SteamHogState::Idle;
    g_next_scan_ms = now_ms() + kReconnectDelayMs;
}

} // namespace ds5onswitch

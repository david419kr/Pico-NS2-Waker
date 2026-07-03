#include "switch_bridge/ble_controller_hub.h"

#include "ble_controller_hub_internal.h"
#include "steam_controller_ble_internal.h"
#include "switch_bridge/procon2_ble.h"
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
constexpr uint16_t kHidsDescriptorStorageSize = 2048;
constexpr std::size_t kTxQueueDepth = 12;
constexpr std::size_t kTxPayloadMax = 64;
constexpr uint8_t kAdvEventDirectInd = 0x01;
constexpr uint16_t kMicrosoftCompanyId = 0x0006;

enum class BleControllerHubState : uint8_t {
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
BleControllerDataCallback g_data_callback = nullptr;
BleControllerHubState g_state = BleControllerHubState::Idle;
uint8_t g_hid_descriptor_storage[kHidsDescriptorStorageSize]{};
TxReport g_tx_queue[kTxQueueDepth]{};
uint8_t g_tx_head = 0;
uint8_t g_tx_count = 0;
uint32_t g_state_ms = 0;
uint32_t g_next_scan_ms = 0;
bool g_stack_ready = false;
bool g_write_ready = false;
bool g_xbox_input_probe_pending = false;
bool g_scan_suppressed = false;
BleControllerKind g_ble_controller_kind = BleControllerKind::Unknown;
BleControllerKind g_last_seen_ble_kind = BleControllerKind::Unknown;

uint32_t now_ms()
{
    return to_ms_since_boot(get_absolute_time());
}

bool state_is_connected()
{
    return g_state == BleControllerHubState::Connected && g_hids_cid != 0;
}

bool can_write_hid_report()
{
    return (g_state == BleControllerHubState::Connected ||
            g_state == BleControllerHubState::Paused) &&
           g_hids_cid != 0;
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

BleControllerKind kind_from_hogp_report(const uint8_t *report,
                                              uint16_t report_len)
{
    if (report == nullptr || report_len == 0u) {
        return BleControllerKind::Unknown;
    }
    if (report[0] == kSteamControllerReportState ||
        report[0] == kSteamControllerReportStateLegacy) {
        return BleControllerKind::SteamController;
    }
    if (report_len == kXboxElite2InputPayloadSize) {
        return BleControllerKind::Xbox;
    }
    if (report_len >= kXboxElite2InputReportSize &&
        (report[0] == kXboxInputReportId || report[0] == 0x00u)) {
        return BleControllerKind::Xbox;
    }
    if (report_len == kXboxInputReportMinSize &&
        (report[0] == kXboxInputReportId || report[0] == 0x00u)) {
        return BleControllerKind::Xbox;
    }
    return BleControllerKind::Unknown;
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
    if (steam_controller_ble_gatt_busy()) {
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
        std::printf("[BLE-HUB] Drop report 0x%02x type=%u status=0x%02x\n",
                    front.report_id, static_cast<unsigned>(front.report_type),
                    status);
        tx_pop_front();
    }
}

void try_xbox_input_probe()
{
    if (!g_xbox_input_probe_pending || !state_is_connected() ||
        g_ble_controller_kind != BleControllerKind::Xbox) {
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

void clear_runtime_state(bool schedule_scan)
{
    steam_controller_ble_reset();
    g_ble_controller_kind = BleControllerKind::Unknown;
    g_con_handle = HCI_CON_HANDLE_INVALID;
    g_hids_cid = 0;
    g_write_ready = false;
    g_xbox_input_probe_pending = false;
    g_tx_head = 0;
    g_tx_count = 0;
    if (g_state != BleControllerHubState::Paused) {
        g_state = BleControllerHubState::Idle;
    }
    if (schedule_scan) {
        g_next_scan_ms = now_ms() + kReconnectDelayMs;
    }
}

void start_ble_scan(uint32_t now_ms)
{
    if (g_scan_suppressed || !g_stack_ready || g_state != BleControllerHubState::Idle ||
        static_cast<int32_t>(now_ms - g_next_scan_ms) < 0) {
        return;
    }

    std::printf("[BLE-HUB] Start LE HID scan\n");
    gap_set_scan_parameters(1, 48, 48);
    gap_start_scan();
    g_state = BleControllerHubState::Scanning;
    g_state_ms = now_ms;
}

void connect_to_ble_advertiser(uint8_t *packet, BleControllerKind kind)
{
    gap_event_advertising_report_get_address(packet, g_remote_addr);
    g_remote_addr_type = static_cast<bd_addr_type_t>(
        gap_event_advertising_report_get_address_type(packet));
    g_ble_controller_kind =
        kind == BleControllerKind::Unknown ? g_last_seen_ble_kind : kind;
    if (g_ble_controller_kind != BleControllerKind::Unknown) {
        g_last_seen_ble_kind = g_ble_controller_kind;
    }
    gap_stop_scan();
    g_state = BleControllerHubState::Connecting;
    g_state_ms = now_ms();
    std::printf("[BLE-HUB] Connect %s type=%u kind=%u\n",
                bd_addr_to_str(g_remote_addr),
                static_cast<unsigned>(g_remote_addr_type),
                static_cast<unsigned>(g_ble_controller_kind));
    const uint8_t status = gap_connect(g_remote_addr, g_remote_addr_type);
    if (status != ERROR_CODE_SUCCESS) {
        std::printf("[BLE-HUB] gap_connect failed status=0x%02x\n", status);
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
            std::printf("[BLE-HUB] HIDS connect failed status=0x%02x\n", status);
            if (g_con_handle != HCI_CON_HANDLE_INVALID) {
                (void)gap_disconnect(g_con_handle);
            }
            clear_runtime_state(true);
            return;
        }
        g_hids_cid = gattservice_subevent_hid_service_connected_get_hids_cid(packet);
        g_state = BleControllerHubState::Connected;
        g_write_ready = true;
        std::printf("[BLE-HUB] HIDS connected cid=0x%04x services=%u\n",
                    g_hids_cid,
                    gattservice_subevent_hid_service_connected_get_num_instances(packet));
        if (g_ble_controller_kind == BleControllerKind::SteamController) {
            steam_controller_ble_on_hids_connected(now_ms());
        } else if (g_ble_controller_kind == BleControllerKind::Xbox) {
            g_xbox_input_probe_pending = true;
        }
        break;
    }

    case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED:
        std::printf("[BLE-HUB] HIDS disconnected\n");
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
        const BleControllerKind report_kind =
            kind_from_hogp_report(report, report_len);
        if (report_kind != BleControllerKind::Unknown &&
            report_kind != g_ble_controller_kind) {
            g_ble_controller_kind = report_kind;
            g_last_seen_ble_kind = report_kind;
        }
        if (g_data_callback != nullptr && report_len != 0u) {
            g_data_callback(g_ble_controller_kind, report, report_len);
        }
        break;
    }

    case GATTSERVICE_SUBEVENT_HID_REPORT_WRITTEN:
        if (gattservice_subevent_hid_report_written_get_hids_cid(packet) ==
            g_hids_cid) {
            g_write_ready = true;
            steam_controller_ble_on_hid_report_written();
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
            std::printf("[BLE-HUB] Accept Just Works pairing\n");
            sm_just_works_confirm(handle);
        }
        break;
    }

    case SM_EVENT_PAIRING_COMPLETE: {
        const hci_con_handle_t handle =
            sm_event_pairing_complete_get_handle(packet);
        if (handle == g_con_handle) {
            const uint8_t status = sm_event_pairing_complete_get_status(packet);
            std::printf("[BLE-HUB] Pairing complete status=0x%02x reason=0x%02x\n",
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
            start_ble_scan(g_next_scan_ms);
        }
        break;

    case GAP_EVENT_ADVERTISING_REPORT:
        if (g_state == BleControllerHubState::Scanning) {
            if (procon2_ble_handle_advertising_report(packet)) {
                clear_runtime_state(false);
                g_scan_suppressed = true;
            } else if (adv_is_xbox_controller(packet)) {
                connect_to_ble_advertiser(packet,
                                      BleControllerKind::Xbox);
            } else if (steam_controller_ble_advertisement_matches(packet)) {
                connect_to_ble_advertiser(
                    packet, BleControllerKind::SteamController);
            } else if (adv_is_steam_reconnect_directed(packet)) {
                connect_to_ble_advertiser(
                    packet, BleControllerKind::Unknown);
            }
        }
        break;

    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) ==
            GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
            if (g_state != BleControllerHubState::Connecting) {
                break;
            }
            const uint8_t status =
                gap_subevent_le_connection_complete_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                std::printf("[BLE-HUB] LE connect failed status=0x%02x\n",
                            status);
                clear_runtime_state(true);
                break;
            }
            g_con_handle =
                gap_subevent_le_connection_complete_get_connection_handle(packet);
            steam_controller_ble_reset();
            g_state = BleControllerHubState::HidsConnecting;
            g_state_ms = now_ms();
            const uint8_t hids_status = hids_client_connect(
                g_con_handle, hids_packet_handler, HID_PROTOCOL_MODE_REPORT,
                &g_hids_cid);
            if (hids_status != ERROR_CODE_SUCCESS) {
                std::printf("[BLE-HUB] hids_client_connect failed status=0x%02x\n",
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
            std::printf("[BLE-HUB] LE disconnected\n");
            clear_runtime_state(true);
        }
        break;
    }

    default:
        break;
    }
}

} // namespace

void ble_controller_hub_register_data_callback(BleControllerDataCallback callback)
{
    g_data_callback = callback;
}

void ble_controller_hub_stack_init()
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

void ble_controller_hub_service(uint32_t now_ms)
{
    if (g_state == BleControllerHubState::Paused) {
        steam_controller_ble_service(now_ms);
        return;
    }

    if (g_state == BleControllerHubState::Idle) {
        start_ble_scan(now_ms);
    } else if (g_state == BleControllerHubState::Connecting ||
               g_state == BleControllerHubState::HidsConnecting) {
        if (now_ms - g_state_ms >= kConnectTimeoutMs) {
            std::printf("[BLE-HUB] Connect timeout\n");
            (void)gap_connect_cancel();
            if (g_con_handle != HCI_CON_HANDLE_INVALID) {
                (void)gap_disconnect(g_con_handle);
            }
            clear_runtime_state(true);
        }
    }

    steam_controller_ble_service(now_ms);
    try_xbox_input_probe();
    try_tx();
}

bool ble_controller_hub_is_connected()
{
    return state_is_connected();
}

BleControllerKind ble_controller_hub_controller_kind()
{
    return state_is_connected() ? g_ble_controller_kind
                                : BleControllerKind::Unknown;
}

bool ble_controller_hub_send_switch_feedback(const SwitchUsbFeedback &feedback)
{
    if (!state_is_connected() || !feedback.has_rumble) {
        return false;
    }

    if (g_ble_controller_kind == BleControllerKind::Xbox) {
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

    if (g_ble_controller_kind == BleControllerKind::SteamController) {
        return steam_controller_ble_send_switch_feedback(feedback);
    }
    return false;
}

bool ble_controller_hub_disconnect()
{
    if (g_state == BleControllerHubState::Scanning) {
        gap_stop_scan();
        g_state = BleControllerHubState::Idle;
        return true;
    }
    if (g_state == BleControllerHubState::Connecting) {
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

void ble_controller_hub_pause_for_switch2_wake()
{
    if (g_state == BleControllerHubState::Paused) {
        return;
    }

    std::printf("[BLE-HUB] Pause for Switch 2 wake\n");
    if (g_state == BleControllerHubState::Scanning) {
        gap_stop_scan();
    } else if (g_state == BleControllerHubState::Connecting) {
        (void)gap_connect_cancel();
    }
    g_state = BleControllerHubState::Paused;
}

void ble_controller_hub_resume_after_switch2_wake()
{
    if (g_state != BleControllerHubState::Paused) {
        return;
    }

    std::printf("[BLE-HUB] Resume after Switch 2 wake\n");
    if (g_hids_cid != 0) {
        g_state = BleControllerHubState::Connected;
        steam_controller_ble_schedule_heartbeat(now_ms());
        try_tx();
        return;
    }
    if (g_con_handle != HCI_CON_HANDLE_INVALID) {
        g_state = BleControllerHubState::HidsConnecting;
        g_state_ms = now_ms();
        return;
    }
    clear_runtime_state(false);
    g_state = BleControllerHubState::Idle;
    g_next_scan_ms = now_ms() + kReconnectDelayMs;
}

void ble_controller_hub_set_scan_suppressed(bool suppressed)
{
    if (g_scan_suppressed == suppressed) {
        return;
    }
    g_scan_suppressed = suppressed;
    if (suppressed) {
        if (g_state == BleControllerHubState::Scanning) {
            gap_stop_scan();
            g_state = BleControllerHubState::Idle;
        } else if (g_state == BleControllerHubState::Connecting) {
            (void)gap_connect_cancel();
            clear_runtime_state(false);
        }
        return;
    }
    if (g_state == BleControllerHubState::Idle) {
        g_next_scan_ms = now_ms() + kReconnectDelayMs;
    }
}

BleControllerKind ble_controller_hub_active_kind()
{
    return g_ble_controller_kind;
}

bool ble_controller_hub_state_is_connected()
{
    return state_is_connected();
}

bool ble_controller_hub_can_use_active_steam_link()
{
    return (g_state == BleControllerHubState::Connected ||
            g_state == BleControllerHubState::Paused) &&
           g_con_handle != HCI_CON_HANDLE_INVALID &&
           g_ble_controller_kind == BleControllerKind::SteamController;
}

hci_con_handle_t ble_controller_hub_connection_handle()
{
    return g_con_handle;
}

bool ble_controller_hub_enqueue_hid_report(const uint8_t *report,
                                           std::size_t report_len,
                                           hid_report_type_t report_type)
{
    return enqueue_hid_report(report, report_len, report_type);
}

void ble_controller_hub_try_tx()
{
    try_tx();
}

void ble_controller_hub_clear_tx_queue()
{
    g_tx_head = 0;
    g_tx_count = 0;
}

uint8_t ble_controller_hub_tx_count()
{
    return g_tx_count;
}

bool ble_controller_hub_write_ready()
{
    return g_write_ready;
}

} // namespace switchbridge

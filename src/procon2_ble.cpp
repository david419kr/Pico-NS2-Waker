#include "switch_bridge/procon2_ble.h"

#include "switch_bridge/procon2_report.h"

#include "ble/gatt_client.h"
#include "ble/sm.h"
#include "bluetooth.h"
#include "bluetooth_gatt.h"
#include "btstack_run_loop.h"
#include "btstack_event.h"
#include "btstack_util.h"
#include "gap.h"
#include "hci.h"

#include <cstdio>
#include <cstring>

namespace switchbridge {

namespace {

// Ported from OGX-Mini-2026 / Bluepad32's Switch 2 BLE parser for the Pro
// Controller 2 path. Bluepad32 owns the proven Pico/BTstack GATT sequence; this
// file only replaces its device-slot abstraction with DS5onSwitch globals.
constexpr uint16_t kHandleInputNotify = 0x000a;
constexpr uint16_t kHandleCmdWrite = 0x0014;
constexpr uint16_t kHandleVibrationPro = 0x0016;
constexpr uint16_t kHandleCmdNotify = 0x001a;
constexpr uint16_t kHandleBootstrapCccd = 0x0004;
constexpr uint16_t kHandleInputNotifyCccd = 0x000b;
constexpr uint16_t kHandleCmdNotifyCccd = 0x001b;

constexpr uint32_t kSetupStabilizeMs = 500;
constexpr uint32_t kCommandTimeoutMs = 2000;
constexpr uint32_t kConnectTimeoutMs = 10000;
constexpr uint32_t kKeepaliveMs = 8;
constexpr std::size_t kMaxServices = 4;

constexpr uint8_t kSw2InputReportUuid[16] = {
    0xd2, 0x7f, 0xdf, 0x09, 0x8f, 0x11, 0x8f, 0x82,
    0xad, 0x49, 0xfe, 0x89, 0xbe, 0xe9, 0x7d, 0xab};
constexpr uint8_t kSw2CmdWriteUuid[16] = {
    0x05, 0xf0, 0xe5, 0x4f, 0xa5, 0x1e, 0x44, 0xaf,
    0x6c, 0x4e, 0xb7, 0x8e, 0x64, 0x9d, 0x4a, 0xc9};
constexpr uint8_t kSw2CmdResponseUuid[16] = {
    0x6a, 0x83, 0x11, 0xb1, 0x15, 0x53, 0x0a, 0xa2,
    0x36, 0x4d, 0xd8, 0xd9, 0x61, 0xa9, 0x65, 0xc7};
constexpr uint8_t kSw2VibrationProUuid[16] = {
    0x05, 0x2b, 0xf7, 0x31, 0x0c, 0x63, 0x39, 0xa9,
    0x7d, 0x42, 0x58, 0x92, 0x51, 0x3f, 0x48, 0xcc};

constexpr uint8_t kPairLtk1[17] = {
    0x00, 0xea, 0xbd, 0x47, 0x13, 0x89, 0x35, 0x42, 0xc6,
    0x79, 0xee, 0x07, 0xf2, 0x53, 0x2c, 0x6c, 0x31};
constexpr uint8_t kPairLtk2[17] = {
    0x00, 0x40, 0xb0, 0x8a, 0x5f, 0xcd, 0x1f, 0x9b, 0x41,
    0x12, 0x5c, 0xac, 0xc6, 0x3f, 0x38, 0xa0, 0x73};

constexpr uint8_t kInitP03_0d[] = {
    0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr uint8_t kInitP15_03[] = {0x00};
constexpr uint8_t kInitP0c_02[] = {0x2f, 0x00, 0x00, 0x00};
constexpr uint8_t kInitP0a_08[] = {
    0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x35, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00};
constexpr uint8_t kInitP0c_04[] = {0x2f, 0x00, 0x00, 0x00};
constexpr uint8_t kInitP03_0a[] = {0x09, 0x00, 0x00, 0x00};
constexpr uint8_t kInitP01_01[] = {0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kInitP09_07[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

struct InitCommand {
    uint8_t cmd;
    uint8_t subcmd;
    const uint8_t *data;
    uint8_t data_len;
};

constexpr InitCommand kInitSequence[] = {
    {0x03, 0x0d, kInitP03_0d, sizeof(kInitP03_0d)},
    {0x07, 0x01, nullptr, 0},
    {0x16, 0x01, nullptr, 0},
    {0x15, 0x03, kInitP15_03, sizeof(kInitP15_03)},
    {0x0c, 0x02, kInitP0c_02, sizeof(kInitP0c_02)},
    {0x11, 0x03, nullptr, 0},
    {0x0a, 0x08, kInitP0a_08, sizeof(kInitP0a_08)},
    {0x0c, 0x04, kInitP0c_04, sizeof(kInitP0c_04)},
    {0x03, 0x0a, kInitP03_0a, sizeof(kInitP03_0a)},
    {0x10, 0x01, nullptr, 0},
    {0x01, 0x0c, nullptr, 0},
    {0x01, 0x01, kInitP01_01, sizeof(kInitP01_01)},
    {0x09, 0x07, kInitP09_07, sizeof(kInitP09_07)},
};

enum class State : uint8_t {
    Idle,
    Connecting,
    DiscoverService,
    DiscoverChars,
    Stabilize,
    Bootstrap,
    EnableCmdNotify,
    InitBurst,
    Pair,
    ReadCalibration,
    EnableInputNotify,
    Ready,
};

btstack_packet_callback_registration_t g_hci_callback{};
btstack_packet_callback_registration_t g_sm_callback{};
bd_addr_t g_remote_addr{};
bd_addr_type_t g_remote_addr_type = BD_ADDR_TYPE_LE_PUBLIC;
hci_con_handle_t g_con_handle = HCI_CON_HANDLE_INVALID;
State g_state = State::Idle;
uint32_t g_state_ms = 0;
uint32_t g_cmd_deadline_ms = 0;
uint32_t g_next_keepalive_ms = 0;
uint8_t g_init_step = 0;
uint8_t g_vibration_sequence = 0;
uint8_t g_expected_cmd = 0;
bool g_needs_pair = false;
bool g_cmd_pending = false;
bool g_waiting_encryption = false;
bool g_cccd_smp_tried = false;
bool g_notify_registered = false;
gatt_client_notification_t g_notify_listener{};
gatt_client_service_t g_services[kMaxServices]{};
uint8_t g_service_count = 0;
uint8_t g_service_index = 0;
uint16_t g_input_notify_handle = 0;
uint16_t g_cmd_write_handle = 0;
uint16_t g_cmd_notify_handle = 0;
uint16_t g_vibration_handle = 0;
Procon2Calibration g_calibration{};
Procon2BleDataCallback g_data_callback = nullptr;

void gatt_packet_handler(uint8_t packet_type, uint16_t channel,
                         uint8_t *packet, uint16_t size);

uint32_t elapsed(uint32_t now, uint32_t since)
{
    return now - since;
}

const char *state_name(State state)
{
    switch (state) {
    case State::Idle: return "idle";
    case State::Connecting: return "connecting";
    case State::DiscoverService: return "discover_service";
    case State::DiscoverChars: return "discover_chars";
    case State::Stabilize: return "stabilize";
    case State::Bootstrap: return "bootstrap";
    case State::EnableCmdNotify: return "enable_cmd_notify";
    case State::InitBurst: return "init_burst";
    case State::Pair: return "pair";
    case State::ReadCalibration: return "read_calibration";
    case State::EnableInputNotify: return "enable_input_notify";
    case State::Ready: return "ready";
    }
    return "?";
}

void set_state(State state)
{
    if (g_state != state) {
        std::printf("[PC2-BLE] state %s -> %s\n", state_name(g_state),
                    state_name(state));
    }
    g_state = state;
    g_state_ms = btstack_run_loop_get_time_ms();
}

void restore_default_sm_policy()
{
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION |
                                       SM_AUTHREQ_BONDING);
}

void reset_runtime()
{
    if (g_notify_registered) {
        gatt_client_stop_listening_for_characteristic_value_updates(
            &g_notify_listener);
        g_notify_registered = false;
    }
    g_con_handle = HCI_CON_HANDLE_INVALID;
    g_state = State::Idle;
    g_state_ms = 0;
    g_cmd_deadline_ms = 0;
    g_next_keepalive_ms = 0;
    g_init_step = 0;
    g_expected_cmd = 0;
    g_needs_pair = false;
    g_cmd_pending = false;
    g_waiting_encryption = false;
    g_cccd_smp_tried = false;
    g_service_count = 0;
    g_service_index = 0;
    g_input_notify_handle = 0;
    g_cmd_write_handle = 0;
    g_cmd_notify_handle = 0;
    g_vibration_handle = 0;
    g_calibration = {};
    restore_default_sm_policy();
}

bool uuid128_equal(const uint8_t *a, const uint8_t *b)
{
    return std::memcmp(a, b, 16u) == 0;
}

bool is_nintendo_service(const gatt_client_service_t &service)
{
    if (service.uuid16 != 0) {
        return false;
    }
    for (std::size_t i = 0; i <= 12u; ++i) {
        const uint8_t *u = &service.uuid128[i];
        if ((u[0] == 0xab && u[1] == 0x7d && u[2] == 0xe9 &&
             u[3] == 0xbe) ||
            (u[0] == 0xbe && u[1] == 0xe9 && u[2] == 0x7d &&
             u[3] == 0xab)) {
            return true;
        }
    }
    return false;
}

bool advertisement_contains_procon2(const uint8_t *data, uint8_t len,
                                    Procon2Advertisement &out)
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
        if (static_cast<uint16_t>(pos) + field_len > len || field_len < 1u) {
            break;
        }
        const uint8_t type = data[pos++];
        const uint8_t value_len = static_cast<uint8_t>(field_len - 1u);
        const uint8_t *value = &data[pos];
        if (type == 0xffu &&
            parse_procon2_manufacturer_data(value, value_len, out)) {
            return true;
        }
        pos = static_cast<uint8_t>(pos + value_len);
    }
    return false;
}

bool reconnect_target_is_local(const Procon2Advertisement &adv)
{
    if (adv.needs_pair) {
        return true;
    }
    bd_addr_t local{};
    gap_local_bd_addr(local);
    if (std::memcmp(adv.reconnect_mac.data(), local,
                    adv.reconnect_mac.size()) == 0) {
        return true;
    }
    for (std::size_t i = 0; i < adv.reconnect_mac.size(); ++i) {
        if (adv.reconnect_mac[i] !=
            local[adv.reconnect_mac.size() - 1u - i]) {
            return false;
        }
    }
    return true;
}

void bind_characteristic(const gatt_client_characteristic_t &ch)
{
    if ((ch.value_handle == kHandleInputNotify ||
         uuid128_equal(ch.uuid128, kSw2InputReportUuid)) &&
        (ch.properties & ATT_PROPERTY_NOTIFY) != 0u) {
        g_input_notify_handle = ch.value_handle;
    }
    if ((ch.value_handle == kHandleCmdNotify ||
         uuid128_equal(ch.uuid128, kSw2CmdResponseUuid)) &&
        (ch.properties & ATT_PROPERTY_NOTIFY) != 0u) {
        g_cmd_notify_handle = ch.value_handle;
    }
    if (ch.value_handle == kHandleCmdWrite ||
        uuid128_equal(ch.uuid128, kSw2CmdWriteUuid)) {
        g_cmd_write_handle = ch.value_handle;
    }
    if (ch.value_handle == kHandleVibrationPro ||
        uuid128_equal(ch.uuid128, kSw2VibrationProUuid)) {
        g_vibration_handle = ch.value_handle;
    }
}

void ensure_fixed_handles()
{
    if (g_input_notify_handle == 0u) {
        g_input_notify_handle = kHandleInputNotify;
    }
    if (g_cmd_write_handle == 0u) {
        g_cmd_write_handle = kHandleCmdWrite;
    }
    if (g_cmd_notify_handle == 0u) {
        g_cmd_notify_handle = kHandleCmdNotify;
    }
    if (g_vibration_handle == 0u) {
        g_vibration_handle = kHandleVibrationPro;
    }
}

void discover_next_service()
{
    const uint8_t status = gatt_client_discover_characteristics_for_service(
        gatt_packet_handler, g_con_handle, &g_services[g_service_index]);
    if (status != ERROR_CODE_SUCCESS) {
        std::printf("[PC2-BLE] characteristic discovery failed status=0x%02x\n",
                    status);
        (void)gap_disconnect(g_con_handle);
    }
}

void start_gatt_setup()
{
    ensure_fixed_handles();
    std::printf("[PC2-BLE] handles input=0x%04x cmd_w=0x%04x cmd_n=0x%04x vib=0x%04x\n",
                g_input_notify_handle, g_cmd_write_handle,
                g_cmd_notify_handle, g_vibration_handle);
    set_state(State::Stabilize);
}

void write_cccd_notify(uint16_t cccd_handle)
{
    static uint8_t enable_notify[2] = {0x01, 0x00};
    (void)gatt_client_write_characteristic_descriptor_using_descriptor_handle(
        gatt_packet_handler, g_con_handle, cccd_handle, sizeof(enable_notify),
        enable_notify);
}

void write_bootstrap_gate()
{
    static uint8_t enable_notify[2] = {0x01, 0x00};
    set_state(State::Bootstrap);
    (void)gatt_client_write_value_of_characteristic(
        gatt_packet_handler, g_con_handle, kHandleBootstrapCccd,
        sizeof(enable_notify), enable_notify);
}

void enable_cmd_notify()
{
    set_state(State::EnableCmdNotify);
    write_cccd_notify(kHandleCmdNotifyCccd);
}

void register_notify_listener()
{
    if (g_notify_registered) {
        return;
    }
    gatt_client_listen_for_characteristic_value_updates(
        &g_notify_listener, gatt_packet_handler, g_con_handle, nullptr);
    g_notify_registered = true;
}

void build_command(uint8_t *out, uint8_t &out_len, uint8_t cmd, uint8_t subcmd,
                   const uint8_t *data, uint8_t data_len)
{
    out[0] = cmd;
    out[1] = 0x91;
    out[2] = 0x01;
    out[3] = subcmd;
    out[4] = 0x00;
    out[5] = data_len;
    out[6] = 0x00;
    out[7] = 0x00;
    if (data != nullptr && data_len != 0u) {
        std::memcpy(&out[8], data, data_len);
    }
    out_len = static_cast<uint8_t>(8u + data_len);
}

void send_command(uint8_t cmd, uint8_t subcmd, const uint8_t *data,
                  uint8_t data_len)
{
    uint8_t buffer[64]{};
    uint8_t len = 0;
    build_command(buffer, len, cmd, subcmd, data, data_len);
    g_expected_cmd = cmd;
    g_cmd_pending = true;
    g_cmd_deadline_ms = btstack_run_loop_get_time_ms() + kCommandTimeoutMs;
    if (g_state != State::Ready) {
        std::printf("[PC2-BLE] cmd 0x%02x sub 0x%02x (%s step %u)\n",
                    cmd, subcmd, state_name(g_state), g_init_step);
    }
    const uint8_t status = gatt_client_write_value_of_characteristic_without_response(
        g_con_handle, g_cmd_write_handle, len, buffer);
    if (status != ERROR_CODE_SUCCESS) {
        std::printf("[PC2-BLE] cmd write failed status=0x%02x cmd=0x%02x\n",
                    status, cmd);
        (void)gap_disconnect(g_con_handle);
    }
}

void start_init_burst();
void subscribe_input();

void read_calibration()
{
    set_state(State::ReadCalibration);
    uint8_t req[8] = {0x0b, 0x7e, 0x00, 0x00,
                      0x42, 0xc0, 0x1f, 0x00};
    send_command(0x02, 0x04, req, sizeof(req));
}

void start_pairing()
{
    set_state(State::Pair);
    g_init_step = 0;
    bd_addr_t local{};
    gap_local_bd_addr(local);
    uint8_t payload[14]{};
    payload[0] = 0x00;
    payload[1] = 0x02;
    // Nintendo Switch 2 pairing command 0x15/0x01 commits the host address the
    // controller later uses to filter reconnect CONNECT_IND packets. Two wire
    // rules apply (matches WaveBird NS2PairingFrames.exchangeAddresses /
    // NS2Pairing.secondaryAddress):
    //   1. addresses are byte-reversed vs bd_addr_t natural order so they line
    //      up with the little-endian InitA the radio puts on air;
    //   2. the console always sends a secondary address equal to the primary
    //      with its least-significant byte decremented by one.
    // The old code copied bd_addr_t verbatim, so the controller stored the host
    // address reversed and rejected every reconnect CONNECT_IND (peripheral
    // stayed silent, central retried LL_VERSION_IND then dropped the link).
    bd_addr_t secondary_natural{};
    std::memcpy(secondary_natural, local, sizeof(secondary_natural));
    secondary_natural[5] = static_cast<uint8_t>(secondary_natural[5] - 1u);
    uint8_t primary_wire[6]{};
    uint8_t secondary_wire[6]{};
    reverse_bd_addr(local, primary_wire);
    reverse_bd_addr(secondary_natural, secondary_wire);
    std::memcpy(&payload[2], primary_wire, sizeof(primary_wire));
    std::memcpy(&payload[8], secondary_wire, sizeof(secondary_wire));
    send_command(0x15, 0x01, payload, sizeof(payload));
}

void finish_ready()
{
    g_cmd_pending = false;
    set_state(State::Ready);
    (void)gap_request_connection_parameter_update(g_con_handle, 6, 6, 0, 400);
    g_next_keepalive_ms = 0;
    uint8_t led[4] = {0x01, 0x00, 0x00, 0x00};
    send_command(0x09, 0x07, led, sizeof(led));
    std::printf("[PC2-BLE] ready\n");
}

void advance_after_command()
{
    g_cmd_pending = false;
    switch (g_state) {
    case State::InitBurst:
        ++g_init_step;
        if (g_init_step < sizeof(kInitSequence) / sizeof(kInitSequence[0])) {
            start_init_burst();
        } else {
            read_calibration();
        }
        break;
    case State::ReadCalibration:
        if (g_needs_pair) {
            start_pairing();
        } else {
            subscribe_input();
        }
        break;
    case State::Pair:
        ++g_init_step;
        if (g_init_step == 1u) {
            send_command(0x15, 0x04, kPairLtk1, sizeof(kPairLtk1));
        } else if (g_init_step == 2u) {
            send_command(0x15, 0x02, kPairLtk2, sizeof(kPairLtk2));
        } else if (g_init_step == 3u) {
            static const uint8_t finish[] = {0x00};
            send_command(0x15, 0x03, finish, sizeof(finish));
        } else {
            g_needs_pair = false;
            subscribe_input();
        }
        break;
    case State::Ready:
        break;
    default:
        break;
    }
}

void start_init_burst()
{
    set_state(State::InitBurst);
    const InitCommand &cmd = kInitSequence[g_init_step];
    send_command(cmd.cmd, cmd.subcmd, cmd.data, cmd.data_len);
}

void subscribe_input()
{
    set_state(State::EnableInputNotify);
    write_cccd_notify(kHandleInputNotifyCccd);
}

void handle_command_response(const uint8_t *data, uint16_t len)
{
    if (data == nullptr || len < 2u) {
        return;
    }
    if (data[0] == 0u && data[1] == 0u) {
        return;
    }
    if (data[1] != 0x01u) {
        return;
    }
    advance_after_command();
}

bool is_input_notification(uint16_t value_handle)
{
    return value_handle == g_input_notify_handle ||
           value_handle == kHandleInputNotify;
}

void handle_input_notification(uint16_t value_handle, const uint8_t *data,
                               uint16_t len)
{
    if (g_state != State::Ready || !is_input_notification(value_handle) ||
        data == nullptr || len < kProcon2Report05MinSize) {
        return;
    }
    SwitchProState state{};
    if (!parse_procon2_report05(data, len, g_calibration, state)) {
        return;
    }
    if (g_data_callback != nullptr) {
        g_data_callback(state);
    }
}

void retry_after_encryption()
{
    g_waiting_encryption = false;
    switch (g_state) {
    case State::EnableCmdNotify:
        write_cccd_notify(kHandleCmdNotifyCccd);
        break;
    case State::EnableInputNotify:
        write_cccd_notify(kHandleInputNotifyCccd);
        break;
    default:
        set_state(State::DiscoverService);
        (void)gatt_client_discover_primary_services(gatt_packet_handler,
                                                    g_con_handle);
        break;
    }
}

bool gatt_auth_retry_or_fail(uint8_t att_status)
{
    if ((att_status == ATT_ERROR_INSUFFICIENT_AUTHENTICATION ||
         att_status == ATT_ERROR_INSUFFICIENT_ENCRYPTION ||
         (att_status == ATT_ERROR_ATTRIBUTE_NOT_FOUND &&
          (g_state == State::EnableCmdNotify ||
           g_state == State::EnableInputNotify))) &&
        !g_cccd_smp_tried) {
        g_cccd_smp_tried = true;
        g_waiting_encryption = true;
        std::printf("[PC2-BLE] CCCD needs encryption att=0x%02x state=%s\n",
                    att_status, state_name(g_state));
        sm_request_pairing(g_con_handle);
        return true;
    }

    if (g_state == State::Bootstrap) {
        std::printf("[PC2-BLE] bootstrap skipped att=0x%02x\n", att_status);
        enable_cmd_notify();
        return true;
    }
    std::printf("[PC2-BLE] GATT failed state=%s att=0x%02x\n",
                state_name(g_state), att_status);
    (void)gap_disconnect(g_con_handle);
    return true;
}

void gatt_packet_handler(uint8_t packet_type, uint16_t channel,
                         uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
    case GATT_EVENT_SERVICE_QUERY_RESULT: {
        gatt_client_service_t service{};
        gatt_event_service_query_result_get_service(packet, &service);
        if (is_nintendo_service(service) && g_service_count < kMaxServices) {
            g_services[g_service_count++] = service;
            std::printf("[PC2-BLE] service handles=%u..%u\n",
                        service.start_group_handle, service.end_group_handle);
        }
        break;
    }
    case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
        gatt_client_characteristic_t ch{};
        gatt_event_characteristic_query_result_get_characteristic(packet, &ch);
        bind_characteristic(ch);
        break;
    }
    case GATT_EVENT_QUERY_COMPLETE: {
        const uint8_t att_status =
            gatt_event_query_complete_get_att_status(packet);
        if (att_status != ATT_ERROR_SUCCESS) {
            (void)gatt_auth_retry_or_fail(att_status);
            break;
        }
        switch (g_state) {
        case State::DiscoverService:
            if (g_service_count == 0u) {
                start_gatt_setup();
                break;
            }
            g_service_index = 0;
            set_state(State::DiscoverChars);
            discover_next_service();
            break;
        case State::DiscoverChars:
            ++g_service_index;
            if (g_service_index < g_service_count) {
                discover_next_service();
            } else {
                start_gatt_setup();
            }
            break;
        case State::Bootstrap:
            enable_cmd_notify();
            break;
        case State::EnableCmdNotify:
            register_notify_listener();
            g_init_step = 0;
            start_init_burst();
            break;
        case State::EnableInputNotify:
            finish_ready();
            break;
        default:
            break;
        }
        break;
    }
    case GATT_EVENT_NOTIFICATION: {
        const uint16_t value_handle =
            gatt_event_notification_get_value_handle(packet);
        const uint16_t value_len =
            gatt_event_notification_get_value_length(packet);
        const uint8_t *value = gatt_event_notification_get_value(packet);
        if (value_handle == g_cmd_notify_handle ||
            value_handle == kHandleCmdNotify) {
            handle_command_response(value, value_len);
        } else {
            handle_input_notification(value_handle, value, value_len);
        }
        break;
    }
    default:
        break;
    }
}

void setup_gatt()
{
    g_service_count = 0;
    g_service_index = 0;
    g_input_notify_handle = 0;
    g_cmd_write_handle = 0;
    g_cmd_notify_handle = 0;
    g_vibration_handle = 0;
    g_cccd_smp_tried = false;
    g_waiting_encryption = false;
    set_state(State::DiscoverService);
    const uint8_t status =
        gatt_client_discover_primary_services(gatt_packet_handler, g_con_handle);
    if (status != ERROR_CODE_SUCCESS) {
        std::printf("[PC2-BLE] service discovery failed status=0x%02x\n",
                    status);
        (void)gap_disconnect(g_con_handle);
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
    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) ==
            GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
            if (g_state != State::Connecting) {
                break;
            }
            const uint8_t status =
                gap_subevent_le_connection_complete_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                std::printf("[PC2-BLE] LE connect failed status=0x%02x\n",
                            status);
                reset_runtime();
                break;
            }
            g_con_handle =
                gap_subevent_le_connection_complete_get_connection_handle(packet);
            std::printf("[PC2-BLE] connected handle=0x%04x\n", g_con_handle);
            setup_gatt();
        }
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        const hci_con_handle_t handle =
            hci_event_disconnection_complete_get_connection_handle(packet);
        if (handle == g_con_handle) {
            std::printf("[PC2-BLE] disconnected reason=0x%02x\n",
                        hci_event_disconnection_complete_get_reason(packet));
            reset_runtime();
        }
        break;
    }
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
    case SM_EVENT_JUST_WORKS_REQUEST:
        if (sm_event_just_works_request_get_handle(packet) == g_con_handle) {
            sm_just_works_confirm(g_con_handle);
        }
        break;
    case SM_EVENT_PAIRING_COMPLETE:
        if (sm_event_pairing_complete_get_handle(packet) == g_con_handle) {
            std::printf("[PC2-BLE] pairing complete status=0x%02x reason=0x%02x\n",
                        sm_event_pairing_complete_get_status(packet),
                        sm_event_pairing_complete_get_reason(packet));
            if (g_waiting_encryption) {
                retry_after_encryption();
            }
        }
        break;
    default:
        break;
    }
}

bool write_vibration_packet(const SwitchRumblePacket &left,
                            const SwitchRumblePacket &right)
{
    if (g_state != State::Ready || g_vibration_handle == 0u) {
        return false;
    }
    uint8_t payload[kProcon2Vibration33Size]{};
    const std::size_t len = build_procon2_vibration33(
        left, right, g_vibration_sequence++, payload, sizeof(payload));
    if (len == 0u) {
        return false;
    }
    const uint8_t status = gatt_client_write_value_of_characteristic_without_response(
        g_con_handle, g_vibration_handle, static_cast<uint16_t>(len), payload);
    return status == ERROR_CODE_SUCCESS;
}

} // namespace

void procon2_ble_register_data_callback(Procon2BleDataCallback callback)
{
    g_data_callback = callback;
}

void procon2_ble_stack_init()
{
    g_hci_callback.callback = &hci_packet_handler;
    hci_add_event_handler(&g_hci_callback);
    g_sm_callback.callback = &sm_packet_handler;
    sm_add_event_handler(&g_sm_callback);
}

void procon2_ble_service(uint32_t now_ms)
{
    if (g_state == State::Connecting &&
        elapsed(now_ms, g_state_ms) >= kConnectTimeoutMs) {
        std::printf("[PC2-BLE] connect timeout\n");
        (void)gap_connect_cancel();
        reset_runtime();
        return;
    }
    if (g_state == State::Stabilize &&
        elapsed(now_ms, g_state_ms) >= kSetupStabilizeMs) {
        write_bootstrap_gate();
        return;
    }
    if (g_state == State::Ready &&
        static_cast<int32_t>(now_ms - g_next_keepalive_ms) >= 0) {
        static const SwitchRumblePacket silent{0x00, 0x01, 0x40, 0x40};
        (void)write_vibration_packet(silent, silent);
        g_next_keepalive_ms = now_ms + kKeepaliveMs;
    }
    if (g_cmd_pending && static_cast<int32_t>(now_ms - g_cmd_deadline_ms) >= 0) {
        std::printf("[PC2-BLE] cmd 0x%02x timeout state=%s step=%u\n",
                    g_expected_cmd, state_name(g_state), g_init_step);
        advance_after_command();
        return;
    }
}

bool procon2_ble_handle_advertising_report(uint8_t *packet)
{
    if (packet == nullptr || g_state != State::Idle) {
        return false;
    }

    const uint8_t *ad_data = gap_event_advertising_report_get_data(packet);
    const uint8_t ad_len = gap_event_advertising_report_get_data_length(packet);
    Procon2Advertisement adv{};
    if (!advertisement_contains_procon2(ad_data, ad_len, adv)) {
        return false;
    }
    if (!reconnect_target_is_local(adv)) {
        return false;
    }

    gap_event_advertising_report_get_address(packet, g_remote_addr);
    g_remote_addr_type = static_cast<bd_addr_type_t>(
        gap_event_advertising_report_get_address_type(packet));
    g_needs_pair = adv.needs_pair;

    std::printf("[PC2-BLE] found addr=%s type=%u %s\n",
                bd_addr_to_str(g_remote_addr),
                static_cast<unsigned>(g_remote_addr_type),
                g_needs_pair ? "(pair)" : "(reconnect)");

    if (g_needs_pair) {
        gap_delete_bonding(g_remote_addr_type, g_remote_addr);
    }
    sm_set_authentication_requirements(0);
    gap_stop_scan();
    set_state(State::Connecting);
    const uint8_t status = gap_connect(g_remote_addr, g_remote_addr_type);
    if (status != ERROR_CODE_SUCCESS) {
        std::printf("[PC2-BLE] gap_connect failed status=0x%02x\n", status);
        reset_runtime();
    }
    return true;
}

bool procon2_ble_is_active()
{
    return g_state != State::Idle;
}

bool procon2_ble_is_ready()
{
    return g_state == State::Ready;
}

bool procon2_ble_send_switch_feedback(const SwitchUsbFeedback &feedback)
{
    if (!feedback.has_rumble) {
        return false;
    }
    return write_vibration_packet(feedback.left_rumble, feedback.right_rumble);
}

bool procon2_ble_disconnect()
{
    if (g_state == State::Connecting) {
        (void)gap_connect_cancel();
        reset_runtime();
        return true;
    }
    if (g_con_handle != HCI_CON_HANDLE_INVALID) {
        return gap_disconnect(g_con_handle) == ERROR_CODE_SUCCESS;
    }
    return false;
}

} // namespace switchbridge

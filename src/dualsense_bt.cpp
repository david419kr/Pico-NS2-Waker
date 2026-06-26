#include "ds5onswitch/dualsense_bt.h"

#include "btstack_event.h"
#include "classic/sdp_server.h"
#include "gap.h"
#include "hci.h"
#include "hci_cmd.h"
#include "l2cap.h"
#include "pico/cyw43_arch.h"
#include "pico/util/queue.h"

#include <cstdio>
#include <cstring>

namespace ds5onswitch {

namespace {

constexpr uint16_t kMtuControl = 672;
constexpr uint16_t kMtuInterrupt = 672;
constexpr std::size_t kSendQueueDepth = 8;
constexpr std::size_t kSendPacketSize = kDualSenseBtOutputReportSize + 1u;

struct SendElement {
    uint8_t data[kSendPacketSize];
    uint16_t len;
};

btstack_packet_callback_registration_t g_hci_callback{};
btstack_packet_callback_registration_t g_l2cap_callback{};
bd_addr_t g_current_addr{};
hci_con_handle_t g_acl_handle = HCI_CON_HANDLE_INVALID;
uint16_t g_hid_control_cid = 0;
uint16_t g_hid_interrupt_cid = 0;
bool g_device_found = false;
uint8_t g_output_sequence = 0;
DualSenseBtDataCallback g_data_callback = nullptr;
queue_t g_send_fifo;

void hci_packet_handler(uint8_t packet_type, uint16_t channel,
                        uint8_t *packet, uint16_t size);
void l2cap_packet_handler(uint8_t packet_type, uint16_t channel,
                          uint8_t *packet, uint16_t size);

bool is_gamepad_class(uint32_t class_of_device)
{
    return (class_of_device & 0x000f00u) == 0x000500u;
}

void reset_connection_state()
{
    g_acl_handle = HCI_CON_HANDLE_INVALID;
    g_hid_control_cid = 0;
    g_hid_interrupt_cid = 0;
    g_device_found = false;
}

void open_hid_control()
{
    if (g_hid_control_cid != 0) {
        return;
    }

    l2cap_create_channel(l2cap_packet_handler, g_current_addr, PSM_HID_CONTROL,
                         kMtuControl, &g_hid_control_cid);
}

void open_hid_interrupt()
{
    if (g_hid_interrupt_cid != 0) {
        return;
    }

    l2cap_create_channel(l2cap_packet_handler, g_current_addr, PSM_HID_INTERRUPT,
                         kMtuInterrupt, &g_hid_interrupt_cid);
}

void request_feature(uint8_t report_id)
{
    if (g_hid_control_cid == 0) {
        return;
    }

    uint8_t request[] = {0x43, report_id};
    l2cap_send(g_hid_control_cid, request, sizeof(request));
}

void request_initial_features()
{
    request_feature(0x09);
    request_feature(0x20);
    request_feature(0x22);
    request_feature(0x05);
}

void send_initial_output()
{
    DualSenseOutputState state{};
    state.reset();
    const DualSenseBtOutputReport report = state.build_report(g_output_sequence++);
    dualsense_bt_send_output_report(report);
}

void maybe_start_inquiry()
{
    gap_connectable_control(1);
    gap_discoverable_control(1);
    gap_inquiry_start(30);
}

void hci_packet_handler(uint8_t packet_type, uint16_t channel,
                        uint8_t *packet, uint16_t size)
{
    (void)packet_type;
    (void)channel;
    (void)size;

    const uint8_t event_type = hci_event_packet_get_type(packet);
    switch (event_type) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            std::printf("[BT] Stack ready, start inquiry\n");
            maybe_start_inquiry();
        }
        break;

    case HCI_EVENT_INQUIRY_RESULT:
    case HCI_EVENT_INQUIRY_RESULT_WITH_RSSI:
    case HCI_EVENT_EXTENDED_INQUIRY_RESPONSE: {
        bd_addr_t addr{};
        uint32_t cod = 0;
        if (event_type == HCI_EVENT_INQUIRY_RESULT) {
            cod = hci_event_inquiry_result_get_class_of_device(packet);
            hci_event_inquiry_result_get_bd_addr(packet, addr);
        } else if (event_type == HCI_EVENT_INQUIRY_RESULT_WITH_RSSI) {
            cod = hci_event_inquiry_result_with_rssi_get_class_of_device(packet);
            hci_event_inquiry_result_with_rssi_get_bd_addr(packet, addr);
        } else {
            cod = hci_event_extended_inquiry_response_get_class_of_device(packet);
            hci_event_extended_inquiry_response_get_bd_addr(packet, addr);
        }

        if (is_gamepad_class(cod)) {
            std::printf("[BT] Gamepad found: %s cod=0x%06x\n",
                        bd_addr_to_str(addr), static_cast<unsigned>(cod));
            bd_addr_copy(g_current_addr, addr);
            g_device_found = true;
            gap_inquiry_stop();
        }
        break;
    }

    case GAP_EVENT_INQUIRY_COMPLETE:
    case HCI_EVENT_INQUIRY_COMPLETE:
        if (g_device_found) {
            std::printf("[BT] Connecting to %s\n", bd_addr_to_str(g_current_addr));
            hci_send_cmd(&hci_create_connection, g_current_addr,
                         hci_usable_acl_packet_types(), 0, 0, 0, 1);
        } else if (event_type == HCI_EVENT_INQUIRY_COMPLETE) {
            maybe_start_inquiry();
        }
        break;

    case HCI_EVENT_CONNECTION_REQUEST: {
        bd_addr_t addr{};
        hci_event_connection_request_get_bd_addr(packet, addr);
        const uint32_t cod =
            hci_event_connection_request_get_class_of_device(packet);
        if (is_gamepad_class(cod)) {
            bd_addr_copy(g_current_addr, addr);
            gap_inquiry_stop();
            hci_send_cmd(&hci_accept_connection_request, addr, 0x01);
        }
        break;
    }

    case HCI_EVENT_CONNECTION_COMPLETE: {
        const uint8_t status = hci_event_connection_complete_get_status(packet);
        if (status == ERROR_CODE_SUCCESS) {
            g_acl_handle =
                hci_event_connection_complete_get_connection_handle(packet);
            hci_event_connection_complete_get_bd_addr(packet, g_current_addr);
            std::printf("[BT] ACL connected handle=0x%04x\n", g_acl_handle);
            hci_send_cmd(&hci_authentication_requested, g_acl_handle);
        } else {
            std::printf("[BT] ACL connect failed status=0x%02x\n", status);
            reset_connection_state();
            maybe_start_inquiry();
        }
        break;
    }

    case HCI_EVENT_LINK_KEY_REQUEST: {
        bd_addr_t addr{};
        link_key_t link_key{};
        link_key_type_t link_key_type{};
        hci_event_link_key_request_get_bd_addr(packet, addr);
        if (gap_get_link_key_for_bd_addr(addr, link_key, &link_key_type)) {
            hci_send_cmd(&hci_link_key_request_reply, addr, link_key);
        } else {
            hci_send_cmd(&hci_link_key_request_negative_reply, addr);
        }
        break;
    }

    case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
        bd_addr_t addr{};
        hci_event_user_confirmation_request_get_bd_addr(packet, addr);
        hci_send_cmd(&hci_user_confirmation_request_reply, addr);
        break;
    }

    case HCI_EVENT_PIN_CODE_REQUEST: {
        bd_addr_t addr{};
        hci_event_pin_code_request_get_bd_addr(packet, addr);
        gap_pin_code_response(addr, "0000");
        break;
    }

    case HCI_EVENT_AUTHENTICATION_COMPLETE:
        if (hci_event_authentication_complete_get_status(packet) ==
            ERROR_CODE_SUCCESS) {
            hci_send_cmd(&hci_set_connection_encryption,
                         hci_event_authentication_complete_get_connection_handle(packet),
                         1);
        } else {
            gap_drop_link_key_for_bd_addr(g_current_addr);
            dualsense_bt_disconnect();
        }
        break;

    case HCI_EVENT_ENCRYPTION_CHANGE:
        if (hci_event_encryption_change_get_status(packet) == ERROR_CODE_SUCCESS &&
            hci_event_encryption_change_get_encryption_enabled(packet) != 0) {
            open_hid_control();
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        const uint8_t reason =
            hci_event_disconnection_complete_get_reason(packet);
        std::printf("[BT] Disconnected reason=0x%02x\n", reason);
        reset_connection_state();
        while (!queue_is_empty(&g_send_fifo)) {
            SendElement discarded{};
            queue_try_remove(&g_send_fifo, &discarded);
        }
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        maybe_start_inquiry();
        break;
    }

    default:
        break;
    }
}

void l2cap_packet_handler(uint8_t packet_type, uint16_t channel,
                          uint8_t *packet, uint16_t size)
{
    if (packet_type == L2CAP_DATA_PACKET) {
        if (channel == g_hid_interrupt_cid) {
            if (g_data_callback != nullptr) {
                g_data_callback(DualSenseBtChannel::Interrupt, packet, size);
            }
        } else if (channel == g_hid_control_cid) {
            if (g_data_callback != nullptr) {
                g_data_callback(DualSenseBtChannel::Control, packet, size);
            }
        }
        return;
    }

    const uint8_t event_type = hci_event_packet_get_type(packet);
    switch (event_type) {
    case L2CAP_EVENT_CHANNEL_OPENED: {
        const uint8_t status = l2cap_event_channel_opened_get_status(packet);
        const uint16_t psm = l2cap_event_channel_opened_get_psm(packet);
        const uint16_t local_cid =
            l2cap_event_channel_opened_get_local_cid(packet);
        if (status != ERROR_CODE_SUCCESS) {
            std::printf("[BT] L2CAP open failed psm=0x%04x status=0x%02x\n",
                        psm, status);
            dualsense_bt_disconnect();
            break;
        }

        if (psm == PSM_HID_CONTROL) {
            g_hid_control_cid = local_cid;
            std::printf("[BT] HID control open cid=0x%04x\n", local_cid);
            request_initial_features();
            open_hid_interrupt();
        } else if (psm == PSM_HID_INTERRUPT) {
            g_hid_interrupt_cid = local_cid;
            std::printf("[BT] HID interrupt open cid=0x%04x\n", local_cid);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
            gap_connectable_control(false);
            gap_discoverable_control(false);
            send_initial_output();
        }
        break;
    }

    case L2CAP_EVENT_INCOMING_CONNECTION: {
        const uint16_t local_cid =
            l2cap_event_incoming_connection_get_local_cid(packet);
        l2cap_accept_connection(local_cid);
        break;
    }

    case L2CAP_EVENT_CHANNEL_CLOSED: {
        const uint16_t local_cid =
            l2cap_event_channel_closed_get_local_cid(packet);
        if (local_cid == g_hid_control_cid) {
            g_hid_control_cid = 0;
        }
        if (local_cid == g_hid_interrupt_cid) {
            g_hid_interrupt_cid = 0;
        }
        if (g_hid_control_cid == 0 && g_hid_interrupt_cid == 0) {
            dualsense_bt_disconnect();
        }
        break;
    }

    case L2CAP_EVENT_CAN_SEND_NOW: {
        SendElement element{};
        if (queue_try_remove(&g_send_fifo, &element)) {
            const uint8_t status =
                l2cap_send(g_hid_interrupt_cid, element.data, element.len);
            if (status != ERROR_CODE_SUCCESS) {
                std::printf("[BT] L2CAP send failed status=0x%02x\n", status);
            }
        }
        if (!queue_is_empty(&g_send_fifo) && g_hid_interrupt_cid != 0) {
            l2cap_request_can_send_now_event(g_hid_interrupt_cid);
        }
        break;
    }

    default:
        break;
    }
}

} // namespace

void dualsense_bt_register_data_callback(DualSenseBtDataCallback callback)
{
    g_data_callback = callback;
}

bool dualsense_bt_init()
{
    queue_init(&g_send_fifo, sizeof(SendElement), kSendQueueDepth);

    l2cap_init();
    sdp_init();
    l2cap_register_service(l2cap_packet_handler, PSM_HID_CONTROL, kMtuControl,
                           LEVEL_2);
    l2cap_register_service(l2cap_packet_handler, PSM_HID_INTERRUPT,
                           kMtuInterrupt, LEVEL_2);

    gap_ssp_set_enable(true);
    gap_secure_connections_enable(true);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_DISPLAY_YES_NO);
    gap_ssp_set_authentication_requirement(
        SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING);
    gap_connectable_control(1);
    gap_discoverable_control(1);

    g_l2cap_callback.callback = &l2cap_packet_handler;
    l2cap_add_event_handler(&g_l2cap_callback);
    g_hci_callback.callback = &hci_packet_handler;
    hci_add_event_handler(&g_hci_callback);

    return hci_power_control(HCI_POWER_ON) == 0;
}

bool dualsense_bt_is_connected()
{
    return g_hid_interrupt_cid != 0;
}

bool dualsense_bt_send_output_report(const DualSenseBtOutputReport &report)
{
    if (g_hid_interrupt_cid == 0) {
        return false;
    }

    SendElement element{};
    element.len = static_cast<uint16_t>(report.size() + 1u);
    element.data[0] = 0xa2;
    std::memcpy(element.data + 1, report.data(), report.size());

    if (!queue_try_add(&g_send_fifo, &element)) {
        std::printf("[BT] output queue full\n");
        return false;
    }

    if (queue_get_level(&g_send_fifo) == 1) {
        l2cap_request_can_send_now_event(g_hid_interrupt_cid);
    }

    return true;
}

bool dualsense_bt_disconnect()
{
    if (g_acl_handle == HCI_CON_HANDLE_INVALID) {
        return false;
    }
    hci_send_cmd(&hci_disconnect, g_acl_handle, 0x13);
    return true;
}

} // namespace ds5onswitch

#include "ds5onswitch/switch2_wake_runtime.h"

#include "ds5onswitch/dualsense_bt.h"
#include "ds5onswitch/steam_hog_bt.h"
#include "ds5onswitch/switch2_wake.h"

#include "bsp/board_api.h"
#include "btstack_event.h"
#include "btstack_tlv.h"
#include "gap.h"
#include "hci.h"

#include <cstdio>
#include <cstring>

namespace ds5onswitch {

namespace {

constexpr uint32_t kSwitch2WakeTlvTag = 0x5332574bu; // ASCII 'S2WK'
constexpr uint32_t kAdvertiseDurationMs = 20000;
constexpr uint32_t kCooldownMs = 30000;
constexpr uint32_t kControllerPowerOffLeadMs = 500;

enum class WakeState : uint8_t {
    Idle,
    PoweringOffController,
    Advertising,
    Cooldown,
};

btstack_packet_callback_registration_t g_hci_callback{};
Switch2WakeConsoleAddrWire g_target_addr_wire{};
Switch2WakeAdvData g_adv_data{};
WakeState g_state = WakeState::Idle;
hci_con_handle_t g_le_handle = HCI_CON_HANDLE_INVALID;
uint32_t g_state_ms = 0;
uint32_t g_cooldown_until_ms = 0;
bool g_has_target = false;
bool g_advertising = false;

void print_addr_wire(const char *prefix,
                     const uint8_t addr[kSwitch2WakeConsoleAddrWireSize])
{
    std::printf("%s%02x:%02x:%02x:%02x:%02x:%02x\n", prefix,
                addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

void persist_target()
{
    const btstack_tlv_t *tlv = nullptr;
    void *tlv_context = nullptr;
    btstack_tlv_get_instance(&tlv, &tlv_context);
    if (tlv == nullptr) {
        std::printf("[S2WK] No TLV instance for wake target persistence\n");
        return;
    }

    const int rc = tlv->store_tag(tlv_context, kSwitch2WakeTlvTag,
                                  g_target_addr_wire.data(),
                                  g_target_addr_wire.size());
    std::printf("[S2WK] Persisted wake target rc=%d\n", rc);
}

void load_target()
{
    const btstack_tlv_t *tlv = nullptr;
    void *tlv_context = nullptr;
    btstack_tlv_get_instance(&tlv, &tlv_context);
    if (tlv == nullptr) {
        g_has_target = false;
        return;
    }

    Switch2WakeConsoleAddrWire loaded{};
    const int len = tlv->get_tag(tlv_context, kSwitch2WakeTlvTag,
                                 loaded.data(), loaded.size());
    if (len != static_cast<int>(loaded.size()) ||
        !switch2_wake_addr_valid(loaded.data())) {
        g_has_target = false;
        return;
    }

    g_target_addr_wire = loaded;
    g_has_target = true;
    print_addr_wire("[S2WK] Loaded wake target ", g_target_addr_wire.data());
}

void start_advertising()
{
    g_adv_data = switch2_wake_build_adv_data(g_target_addr_wire.data());

    bd_addr_t direct_addr{};
    gap_random_address_set_mode(GAP_RANDOM_ADDRESS_TYPE_OFF);
    gap_advertisements_set_params(
        0x20, 0x30,
        0x00, // ADV_IND/connectable undirected
        0x00, // public direct address type, unused for ADV_IND
        direct_addr,
        0x07, // channels 37, 38, 39
        0x00);
    gap_advertisements_set_data(static_cast<uint8_t>(g_adv_data.size()),
                                g_adv_data.data());
    gap_advertisements_enable(1);

    g_advertising = true;
    g_state = WakeState::Advertising;
    g_state_ms = board_millis();
    print_addr_wire("[S2WK] Advertising wake target ",
                    g_target_addr_wire.data());
}

void stop_advertising()
{
    if (g_advertising) {
        gap_advertisements_enable(0);
        g_advertising = false;
    }
    if (g_le_handle != HCI_CON_HANDLE_INVALID) {
        (void)gap_disconnect(g_le_handle);
    }
}

bool disconnect_classic_controller_for_wake()
{
    std::printf("[S2WK] Wake requested, power off/disconnect controller "
                "before advertise\n");
    (void)dualsense_bt_power_off();
    const bool steam_power_off_queued = steam_hog_bt_power_off();
    (void)dualsense_bt_disconnect();
    return steam_power_off_queued;
}

void start_wake_advertising_after_controller_pause()
{
    steam_hog_bt_pause_for_switch2_wake();
    start_advertising();
}

void finish_attempt(bool apply_cooldown)
{
    stop_advertising();
    dualsense_bt_resume_after_switch2_wake();
    steam_hog_bt_resume_after_switch2_wake();
    const uint32_t now = board_millis();
    if (apply_cooldown) {
        g_state = WakeState::Cooldown;
        g_cooldown_until_ms = now + kCooldownMs;
    } else {
        g_state = WakeState::Idle;
        g_cooldown_until_ms = 0;
    }
    g_state_ms = now;
}

void hci_packet_handler(uint8_t packet_type, uint16_t channel,
                        uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    const uint8_t event_type = hci_event_packet_get_type(packet);
    switch (event_type) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            load_target();
        }
        break;

    case HCI_EVENT_LE_META:
        if (hci_event_le_meta_get_subevent_code(packet) ==
            HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
            const uint8_t status =
                hci_subevent_le_connection_complete_get_status(packet);
            if (status == ERROR_CODE_SUCCESS) {
                g_le_handle =
                    hci_subevent_le_connection_complete_get_connection_handle(packet);
                std::printf("[S2WK] LE connected handle=0x%04x\n", g_le_handle);
            }
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        const hci_con_handle_t handle =
            hci_event_disconnection_complete_get_connection_handle(packet);
        if (handle == g_le_handle) {
            g_le_handle = HCI_CON_HANDLE_INVALID;
            std::printf("[S2WK] LE disconnected\n");
        }
        break;
    }

    default:
        break;
    }
}

} // namespace

void switch2_wake_init()
{
    g_hci_callback.callback = &hci_packet_handler;
    hci_add_event_handler(&g_hci_callback);
}

void switch2_wake_set_target_from_usb_pairing(const uint8_t addr_wire[6])
{
    if (!switch2_wake_addr_valid(addr_wire)) {
        return;
    }

    if (g_has_target &&
        std::memcmp(g_target_addr_wire.data(), addr_wire,
                    g_target_addr_wire.size()) == 0) {
        return;
    }

    std::memcpy(g_target_addr_wire.data(), addr_wire,
                g_target_addr_wire.size());
    g_has_target = true;
    print_addr_wire("[S2WK] Learned wake target ",
                    g_target_addr_wire.data());
    persist_target();
}

bool switch2_wake_has_target()
{
    return g_has_target && switch2_wake_addr_valid(g_target_addr_wire.data());
}

bool switch2_wake_request()
{
    if (!switch2_wake_has_target() || g_state != WakeState::Idle) {
        return false;
    }

    const uint32_t now = board_millis();
    if (g_cooldown_until_ms != 0 &&
        static_cast<int32_t>(now - g_cooldown_until_ms) < 0) {
        return false;
    }

    std::printf("[S2WK] Wake requested\n");
    dualsense_bt_pause_for_switch2_wake();
    const bool steam_power_off_queued = disconnect_classic_controller_for_wake();
    if (steam_power_off_queued) {
        g_state = WakeState::PoweringOffController;
        g_state_ms = now;
    } else {
        start_wake_advertising_after_controller_pause();
    }
    return true;
}

void switch2_wake_service(bool usb_mounted)
{
    const uint32_t now = board_millis();
    if (usb_mounted) {
        if (g_state != WakeState::Idle || g_advertising) {
            std::printf("[S2WK] USB mounted, cancel/re-arm wake\n");
            finish_attempt(false);
        }
        g_cooldown_until_ms = 0;
        return;
    }

    switch (g_state) {
    case WakeState::Idle:
        break;

    case WakeState::PoweringOffController:
        if (now - g_state_ms >= kControllerPowerOffLeadMs) {
            start_wake_advertising_after_controller_pause();
        }
        break;

    case WakeState::Advertising:
        if (now - g_state_ms >= kAdvertiseDurationMs) {
            std::printf("[S2WK] Wake advertisement window done\n");
            finish_attempt(true);
        }
        break;

    case WakeState::Cooldown:
        if (static_cast<int32_t>(now - g_cooldown_until_ms) >= 0) {
            g_state = WakeState::Idle;
            g_cooldown_until_ms = 0;
        }
        break;
    }
}

} // namespace ds5onswitch

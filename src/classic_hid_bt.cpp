#include "switch_bridge/classic_hid_bt.h"

#include "switch_bridge/bt_debug_log.h"
#include "switch_bridge/dualshock4_output.h"
#include "switch_bridge/dualshock4_report.h"
#include "switch_bridge/dualsense_report.h"
#include "switch_bridge/steam_hog_bt.h"
#include "switch_bridge/switch_identity.h"
#include "switch_bridge/switch_usb.h"
#include "switch_bridge/xbox_controller.h"

#include "btstack_event.h"
#include "btstack_tlv.h"
#include "btstack_util.h"
#include "bluetooth_sdp.h"
#include "classic/sdp_client.h"
#include "classic/sdp_server.h"
#include "gap.h"
#include "hci.h"
#include "hci_cmd.h"
#include "l2cap.h"
#include "pico.h"
#include "pico/time.h"
#include "pico/util/queue.h"

#include <cstdio>
#include <cstring>

namespace switchbridge {

namespace {

constexpr uint16_t kMtuControl = 672;
constexpr uint16_t kMtuInterrupt = 672;
constexpr std::size_t kSendQueueDepth = 10;
constexpr std::size_t kSendPacketSize = kDualSenseBtAudioReportSize;
constexpr std::size_t kManualHidPacketSize = kSendPacketSize + 1u;
constexpr uint32_t kBlacklistTlvTag = 0x424c434bu; // ASCII 'BLCK'
constexpr uint32_t kBlacklistPersistDelayMs = 5000;
constexpr std::size_t kSdpAttributeBufferSize = 256;
constexpr uint16_t kNintendoVendorId = 0x057e;
constexpr uint16_t kSonyVendorId = 0x054c;
constexpr uint16_t kMicrosoftVendorId = kXboxMicrosoftVid;
constexpr uint16_t kSonyDualShock4Pid1 = 0x05c4;
constexpr uint16_t kSonyDualShock4Pid2 = 0x09cc;
constexpr uint16_t kSonyDualSensePid1 = 0x0ce6;
constexpr uint16_t kSonyDualSensePid2 = 0x0df2;
constexpr uint16_t kSdpPnpProductIdAttribute = 0x0202;
constexpr uint8_t kHidSetReportFeature = 0x53;
constexpr uint8_t kDualSenseFeatureReportBluetoothControl = 0x08;
constexpr std::size_t kDualSenseBluetoothControlPayloadSize = 47;
constexpr uint8_t kDualSenseBluetoothControlOff = 0x02;
constexpr uint8_t kDualSenseFeatureCrcSeed = 0x53;
constexpr uint8_t kHciDisconnectRemoteUserTerminated = 0x13;

enum class SdpProbe {
    None,
    Pnp,
    Hid,
};

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
bool g_hid_control_opening = false;
bool g_hid_interrupt_opening = false;
bool g_device_found = false;
bool g_local_acl_initiated = false;
uint8_t g_output_sequence = 0;
ClassicHidDataCallback g_data_callback = nullptr;
queue_t g_send_fifo;
bd_addr_t g_blacklisted_addrs[NVM_NUM_LINK_KEYS]{};
int g_blacklisted_addr_count = 0;
bool g_blacklist_dirty = false;
uint32_t g_blacklist_dirty_ms = 0;
bool g_paused_for_switch2_wake = false;
BtControllerKind g_controller_kind = BtControllerKind::Unknown;
BtControllerKind g_controller_kind_hint = BtControllerKind::Unknown;
bool g_dualsense_init_sent = false;
bool g_dualsense_features_requested = false;
bool g_dualshock4_init_sent = false;
bool g_dualshock4_features_requested = false;
bool g_switch_pro_init_sent = false;
bool g_unknown_interrupt_report_logged = false;
bool g_logged_first_control_data = false;
bool g_logged_first_interrupt_data = false;
SdpProbe g_sdp_probe = SdpProbe::None;
BtControllerKind g_sdp_kind_hint = BtControllerKind::Unknown;
uint8_t g_sdp_attribute[kSdpAttributeBufferSize]{};
uint16_t g_sdp_attribute_id = 0;
uint16_t g_sdp_attribute_len = 0;
uint8_t g_sdp_previous_attribute_byte = 0;
uint16_t g_sdp_vendor_id = 0;
uint16_t g_sdp_product_id = 0;

void __not_in_flash_func(hci_packet_handler)(uint8_t packet_type,
                                             uint16_t channel,
                                             uint8_t *packet, uint16_t size);
void __not_in_flash_func(l2cap_packet_handler)(uint8_t packet_type,
                                               uint16_t channel,
                                               uint8_t *packet, uint16_t size);
void sdp_packet_handler(uint8_t packet_type, uint16_t channel,
                        uint8_t *packet, uint16_t size);

bool is_gamepad_class(uint32_t class_of_device)
{
    return (class_of_device & 0x000f00u) == 0x000500u;
}

uint32_t now_ms()
{
    return to_ms_since_boot(get_absolute_time());
}

bool is_controller_candidate(uint32_t class_of_device, BtControllerKind hint)
{
    if (hint != BtControllerKind::Unknown) {
        return true;
    }
    // Genuine controllers usually report major class Peripheral (0x05), but
    // third-party Switch Pro clones are not consistent. Accept unknown CODs
    // during pairing/discovery; HID report parsing still decides the final
    // controller kind.
    return is_gamepad_class(class_of_device) || class_of_device == 0;
}

const char *controller_kind_name(BtControllerKind kind)
{
    switch (kind) {
    case BtControllerKind::DualSense:
        return "DualSense";
    case BtControllerKind::DualShock4:
        return "DualShock 4";
    case BtControllerKind::SwitchPro:
        return "Switch Pro";
    case BtControllerKind::Xbox:
        return "Xbox";
    case BtControllerKind::Unknown:
    default:
        return "unknown";
    }
}

uint8_t controller_kind_code(BtControllerKind kind)
{
    return static_cast<uint8_t>(kind);
}

uint16_t sdp_uint16_value(const uint8_t *data, uint16_t len)
{
    if (data == nullptr || len < 2u) {
        return 0;
    }
    if (len >= 3u && data[0] == 0x09u) {
        return static_cast<uint16_t>((data[1] << 8u) | data[2]);
    }
    return static_cast<uint16_t>((data[len - 2u] << 8u) | data[len - 1u]);
}

bool contains_ascii(const char *haystack, std::size_t haystack_len,
                    const char *needle)
{
    const std::size_t needle_len = std::strlen(needle);
    if (needle_len == 0 || haystack_len < needle_len) {
        return false;
    }
    for (std::size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (std::memcmp(haystack + i, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

BtControllerKind kind_from_eir(const uint8_t *eir, std::size_t size)
{
    if (eir == nullptr) {
        return BtControllerKind::Unknown;
    }

    std::size_t offset = 0;
    while (offset < size) {
        const uint8_t field_len = eir[offset];
        if (field_len == 0) {
            break;
        }
        if (offset + field_len >= size) {
            break;
        }

        const uint8_t type = eir[offset + 1u];
        const uint8_t *data = eir + offset + 2u;
        const std::size_t data_len = field_len - 1u;
        if (type == 0x08u || type == 0x09u) {
            const char *name = reinterpret_cast<const char *>(data);
            if (contains_ascii(name, data_len, "Pro Controller")) {
                return BtControllerKind::SwitchPro;
            }
            if (contains_ascii(name, data_len, "DualSense")) {
                return BtControllerKind::DualSense;
            }
            if (contains_ascii(name, data_len, "Xbox")) {
                return BtControllerKind::Xbox;
            }
        }
        offset += static_cast<std::size_t>(field_len) + 1u;
    }

    return BtControllerKind::Unknown;
}

BtControllerKind kind_from_hid_report(const uint8_t *packet, std::size_t size)
{
    if (packet == nullptr || size == 0) {
        return BtControllerKind::Unknown;
    }

    const uint8_t *report = packet;
    std::size_t report_size = size;
    if (packet[0] == 0xa1u) {
        if (size < 2u) {
            return BtControllerKind::Unknown;
        }
        report = packet + 1u;
        report_size = size - 1u;
    }

    switch (report[0]) {
    case kSwitchProFullInputReportId:
    case kSwitchProSubcommandReplyReportId:
    case 0x3fu:
        return BtControllerKind::SwitchPro;
    case 0x31u:
        if (report_size >= kDualSenseBt31PayloadSize + 1u) {
            return BtControllerKind::DualSense;
        }
        break;
    case 0x11u:
        if (report_size >= 2u) {
            return BtControllerKind::DualShock4;
        }
        break;
    case 0x01u:
        if (report_size >= kXboxInputReportMinSize) {
            return BtControllerKind::Xbox;
        }
        // Some DS4-compatible pads start with the 10-byte minimal report or a
        // USB-shaped 0x01 report before full 0x11 reports are enabled.
        if (report_size >= kDualShock4BtMinimalReportSize &&
            report_size <= 64u) {
            return BtControllerKind::DualShock4;
        }
        break;
    default:
        break;
    }

    return BtControllerKind::Unknown;
}

void reset_connection_state()
{
    g_acl_handle = HCI_CON_HANDLE_INVALID;
    g_hid_control_cid = 0;
    g_hid_interrupt_cid = 0;
    g_hid_control_opening = false;
    g_hid_interrupt_opening = false;
    g_device_found = false;
    g_local_acl_initiated = false;
    g_controller_kind = BtControllerKind::Unknown;
    g_controller_kind_hint = BtControllerKind::Unknown;
    g_dualsense_init_sent = false;
    g_dualsense_features_requested = false;
    g_dualshock4_init_sent = false;
    g_dualshock4_features_requested = false;
    g_switch_pro_init_sent = false;
    g_unknown_interrupt_report_logged = false;
    g_logged_first_control_data = false;
    g_logged_first_interrupt_data = false;
    g_sdp_probe = SdpProbe::None;
    g_sdp_kind_hint = BtControllerKind::Unknown;
    g_sdp_attribute_id = 0;
    g_sdp_attribute_len = 0;
    g_sdp_previous_attribute_byte = 0;
    g_sdp_vendor_id = 0;
    g_sdp_product_id = 0;
}

void open_hid_control()
{
    if (g_hid_control_cid != 0 || g_hid_control_opening) {
        return;
    }

    const uint8_t status =
        l2cap_create_channel(l2cap_packet_handler, g_current_addr,
                             PSM_HID_CONTROL, kMtuControl,
                             &g_hid_control_cid);
    if (status == ERROR_CODE_SUCCESS) {
        g_hid_control_opening = true;
    } else {
        std::printf("[BT] HID control open request failed status=0x%02x\n",
                    status);
    }
}

void open_hid_interrupt()
{
    if (g_hid_interrupt_cid != 0 || g_hid_interrupt_opening) {
        return;
    }

    const uint8_t status =
        l2cap_create_channel(l2cap_packet_handler, g_current_addr,
                             PSM_HID_INTERRUPT, kMtuInterrupt,
                             &g_hid_interrupt_cid);
    if (status == ERROR_CODE_SUCCESS) {
        g_hid_interrupt_opening = true;
    } else {
        std::printf("[BT] HID interrupt open request failed status=0x%02x\n",
                    status);
    }
}

void open_next_outgoing_hid_channel()
{
    if (g_hid_control_cid == 0) {
        open_hid_control();
    } else if (g_hid_interrupt_cid == 0) {
        open_hid_interrupt();
    }
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
    if (g_hid_control_cid == 0) {
        return;
    }
    if (g_dualsense_features_requested) {
        return;
    }
    g_dualsense_features_requested = true;
    request_feature(0x09);
    request_feature(0x20);
    request_feature(0x22);
    request_feature(0x05);
}

void request_dualshock4_features()
{
    if (g_hid_control_cid == 0) {
        return;
    }
    if (g_dualshock4_features_requested) {
        return;
    }
    g_dualshock4_features_requested = true;
    request_feature(0x05);
}

void send_initial_output()
{
    if (g_hid_interrupt_cid == 0) {
        return;
    }
    if (g_dualsense_init_sent) {
        return;
    }
    g_dualsense_init_sent = true;
    DualSenseOutputState state{};
    state.reset();
    const DualSenseBtOutputReport report = state.build_report(g_output_sequence++);
    classic_hid_send_dualsense_output_report(report);
}

void send_dualshock4_initial_output()
{
    if (g_hid_interrupt_cid == 0) {
        return;
    }
    if (g_dualshock4_init_sent) {
        return;
    }
    g_dualshock4_init_sent = true;
    DualShock4OutputState state{};
    state.reset();
    const DualShock4BtOutputReport report = state.build_report();
    (void)classic_hid_send_raw_output_report(report.data(), report.size());
}

void send_switch_pro_subcommand(uint8_t subcommand, uint8_t arg)
{
    uint8_t report[12] = {
        kSwitchOutputSubcommand,
        g_output_sequence++,
        0x00, 0x01, 0x40, 0x40,
        0x00, 0x01, 0x40, 0x40,
        subcommand,
        arg,
    };
    (void)classic_hid_send_raw_output_report(report, sizeof(report));
}

void send_switch_pro_usb_command(uint8_t command)
{
    const uint8_t report[2] = {
        kSwitchOutputUsb,
        command,
    };
    (void)classic_hid_send_raw_output_report(report, sizeof(report));
}

void send_switch_pro_init()
{
    if (g_switch_pro_init_sent || g_hid_interrupt_cid == 0) {
        return;
    }
    g_switch_pro_init_sent = true;
    send_switch_pro_usb_command(kSwitchUsbCmdNoTimeout);
    send_switch_pro_subcommand(kSwitchSubcmdEnableVibration, 0x01u);
    send_switch_pro_subcommand(kSwitchSubcmdEnableImu, 0x01u);
    send_switch_pro_subcommand(kSwitchSubcmdSetReportMode,
                               kSwitchProFullInputReportId);
}

void set_controller_kind(BtControllerKind kind)
{
    if (kind == BtControllerKind::Unknown || g_controller_kind == kind) {
        return;
    }

    g_controller_kind = kind;
    std::printf("[BT] Controller kind: %s\n", controller_kind_name(kind));
    bt_debug_log_event(BtDebugEvent::ControllerKind,
                       controller_kind_code(kind));
    if (kind == BtControllerKind::DualSense) {
        request_initial_features();
        send_initial_output();
    } else if (kind == BtControllerKind::DualShock4) {
        request_dualshock4_features();
        send_dualshock4_initial_output();
    } else if (kind == BtControllerKind::SwitchPro) {
        send_switch_pro_init();
    }
}

void continue_hid_open_after_probe()
{
    if (g_sdp_kind_hint != BtControllerKind::Unknown) {
        g_controller_kind_hint = g_sdp_kind_hint;
    }

    if (g_controller_kind_hint == BtControllerKind::SwitchPro ||
        g_controller_kind_hint == BtControllerKind::Xbox) {
        open_next_outgoing_hid_channel();
        return;
    }
    if (g_local_acl_initiated) {
        open_next_outgoing_hid_channel();
    } else {
        std::printf("[BT] Encrypted incoming ACL, waiting for HID channels\n");
    }
}

bool start_sdp_probe(SdpProbe probe)
{
    if (g_sdp_probe != SdpProbe::None) {
        return true;
    }

    g_sdp_probe = probe;
    g_sdp_attribute_id = 0;
    g_sdp_attribute_len = 0;
    g_sdp_previous_attribute_byte = 0;
    std::memset(g_sdp_attribute, 0, sizeof(g_sdp_attribute));

    const uint16_t service =
        probe == SdpProbe::Pnp
            ? BLUETOOTH_SERVICE_CLASS_PNP_INFORMATION
            : BLUETOOTH_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE_SERVICE;
    const uint8_t status =
        sdp_client_query_uuid16(sdp_packet_handler, g_current_addr, service);
    if (status != ERROR_CODE_SUCCESS) {
        g_sdp_probe = SdpProbe::None;
        return false;
    }
    return true;
}

void classify_sdp_pnp_ids(uint16_t attribute_id)
{
    if (g_sdp_vendor_id == kNintendoVendorId) {
        g_sdp_kind_hint = BtControllerKind::SwitchPro;
    } else if (g_sdp_vendor_id == kSonyVendorId) {
        // Keep the pre-DS4 behavior as the fallback: Sony HID is DualSense
        // unless the PnP product ID proves it is a DS4. Some DualSense SDP
        // replies do not expose the product ID early enough, and leaving the
        // kind Unknown prevents initial feature/output reports, breaking audio
        // and rumble.
        g_sdp_kind_hint = BtControllerKind::DualSense;
        if (g_sdp_product_id == kSonyDualShock4Pid1 ||
            g_sdp_product_id == kSonyDualShock4Pid2) {
            g_sdp_kind_hint = BtControllerKind::DualShock4;
        } else if (g_sdp_product_id == kSonyDualSensePid1 ||
                   g_sdp_product_id == kSonyDualSensePid2) {
            g_sdp_kind_hint = BtControllerKind::DualSense;
        }
    } else if (g_sdp_vendor_id == kMicrosoftVendorId) {
        g_sdp_kind_hint = BtControllerKind::Xbox;
    }

    if (g_sdp_kind_hint != BtControllerKind::Unknown) {
        bt_debug_log_event(BtDebugEvent::SdpComplete,
                           controller_kind_code(g_sdp_kind_hint),
                           attribute_id,
                           (static_cast<uint32_t>(g_sdp_vendor_id) << 16u) |
                               g_sdp_product_id);
    }
}

bool start_controller_kind_probe()
{
    if (g_controller_kind_hint != BtControllerKind::Unknown) {
        return false;
    }
    g_sdp_kind_hint = BtControllerKind::Unknown;
    g_sdp_vendor_id = 0;
    g_sdp_product_id = 0;
    return start_sdp_probe(SdpProbe::Pnp);
}

void process_sdp_attribute(uint16_t attribute_id, const uint8_t *data,
                           uint16_t len)
{
    if (attribute_id == BLUETOOTH_ATTRIBUTE_VENDOR_ID) {
        g_sdp_vendor_id = sdp_uint16_value(data, len);
        classify_sdp_pnp_ids(attribute_id);
    } else if (attribute_id == kSdpPnpProductIdAttribute) {
        g_sdp_product_id = sdp_uint16_value(data, len);
        classify_sdp_pnp_ids(attribute_id);
    }
}

void process_sdp_attribute_byte(uint8_t *packet)
{
    const uint16_t attribute_id =
        sdp_event_query_attribute_byte_get_attribute_id(packet);
    const uint16_t attribute_len =
        sdp_event_query_attribute_byte_get_attribute_length(packet);
    const uint16_t offset =
        sdp_event_query_attribute_byte_get_data_offset(packet);
    const uint8_t data = sdp_event_query_attribute_byte_get_data(packet);

    if (offset == 0) {
        g_sdp_attribute_id = attribute_id;
        g_sdp_attribute_len = attribute_len;
        g_sdp_previous_attribute_byte = 0;
        if (attribute_len <= sizeof(g_sdp_attribute)) {
            std::memset(g_sdp_attribute, 0, attribute_len);
        }
    }

    if (attribute_id == BLUETOOTH_ATTRIBUTE_HID_DESCRIPTOR_LIST &&
        g_sdp_previous_attribute_byte == 0x85u &&
        (data == kSwitchProFullInputReportId ||
         data == kSwitchProSubcommandReplyReportId || data == 0x3fu)) {
        g_sdp_kind_hint = BtControllerKind::SwitchPro;
        bt_debug_log_event(BtDebugEvent::SdpComplete,
                           controller_kind_code(g_sdp_kind_hint),
                           BLUETOOTH_ATTRIBUTE_HID_DESCRIPTOR_LIST, data);
    }
    g_sdp_previous_attribute_byte = data;

    if (attribute_id != g_sdp_attribute_id ||
        g_sdp_attribute_len > sizeof(g_sdp_attribute) ||
        offset >= sizeof(g_sdp_attribute)) {
        return;
    }

    g_sdp_attribute[offset] = data;
    if (offset + 1u == g_sdp_attribute_len) {
        process_sdp_attribute(attribute_id, g_sdp_attribute,
                              g_sdp_attribute_len);
    }
}

void sdp_packet_handler(uint8_t packet_type, uint16_t channel,
                        uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
    case SDP_EVENT_QUERY_ATTRIBUTE_VALUE:
        process_sdp_attribute_byte(packet);
        break;

    case SDP_EVENT_QUERY_COMPLETE: {
        bt_debug_log_event(BtDebugEvent::SdpComplete,
                           controller_kind_code(g_sdp_kind_hint),
                           g_sdp_probe == SdpProbe::Pnp ? 1u : 2u,
                           sdp_event_query_complete_get_status(packet));
        const SdpProbe completed_probe = g_sdp_probe;
        g_sdp_probe = SdpProbe::None;
        g_sdp_attribute_id = 0;
        g_sdp_attribute_len = 0;
        g_sdp_previous_attribute_byte = 0;

        if (g_acl_handle == HCI_CON_HANDLE_INVALID) {
            break;
        }
        if (completed_probe == SdpProbe::Pnp &&
            g_sdp_kind_hint == BtControllerKind::Unknown &&
            start_sdp_probe(SdpProbe::Hid)) {
            break;
        }
        continue_hid_open_after_probe();
        break;
    }

    default:
        break;
    }
}

void maybe_start_inquiry()
{
    if (g_paused_for_switch2_wake) {
        return;
    }

    gap_connectable_control(1);
    gap_discoverable_control(1);
    gap_inquiry_start(30);
}

bool blacklist_contains(const bd_addr_t addr)
{
    for (int i = 0; i < g_blacklisted_addr_count; ++i) {
        if (bd_addr_cmp(addr, g_blacklisted_addrs[i]) == 0) {
            return true;
        }
    }
    return false;
}

void blacklist_add_unique(const bd_addr_t addr)
{
    if (blacklist_contains(addr) ||
        g_blacklisted_addr_count >= NVM_NUM_LINK_KEYS) {
        return;
    }

    bd_addr_copy(g_blacklisted_addrs[g_blacklisted_addr_count++], addr);
    std::printf("[BT] Blacklisted %s\n", bd_addr_to_str(addr));
}

void blacklist_persist()
{
    const btstack_tlv_t *tlv = nullptr;
    void *tlv_context = nullptr;
    btstack_tlv_get_instance(&tlv, &tlv_context);
    if (tlv == nullptr) {
        std::printf("[BT] No TLV instance for blacklist persistence\n");
        return;
    }

    if (g_blacklisted_addr_count == 0) {
        tlv->delete_tag(tlv_context, kBlacklistTlvTag);
        return;
    }

    const uint32_t bytes =
        static_cast<uint32_t>(g_blacklisted_addr_count * sizeof(bd_addr_t));
    const int rc = tlv->store_tag(
        tlv_context, kBlacklistTlvTag,
        reinterpret_cast<const uint8_t *>(g_blacklisted_addrs), bytes);
    std::printf("[BT] Persisted %d blacklisted MAC(s), rc=%d\n",
                g_blacklisted_addr_count, rc);
}

void blacklist_load()
{
    const btstack_tlv_t *tlv = nullptr;
    void *tlv_context = nullptr;
    btstack_tlv_get_instance(&tlv, &tlv_context);
    if (tlv == nullptr) {
        g_blacklisted_addr_count = 0;
        return;
    }

    const int len = tlv->get_tag(
        tlv_context, kBlacklistTlvTag,
        reinterpret_cast<uint8_t *>(g_blacklisted_addrs),
        sizeof(g_blacklisted_addrs));
    if (len <= 0 || len % static_cast<int>(sizeof(bd_addr_t)) != 0) {
        g_blacklisted_addr_count = 0;
        return;
    }

    g_blacklisted_addr_count = len / static_cast<int>(sizeof(bd_addr_t));
    if (g_blacklisted_addr_count > NVM_NUM_LINK_KEYS) {
        g_blacklisted_addr_count = NVM_NUM_LINK_KEYS;
    }
    std::printf("[BT] Loaded %d blacklisted MAC(s)\n", g_blacklisted_addr_count);
}

void blacklist_remove(const bd_addr_t addr)
{
    for (int i = 0; i < g_blacklisted_addr_count; ++i) {
        if (bd_addr_cmp(addr, g_blacklisted_addrs[i]) != 0) {
            continue;
        }

        for (int j = i; j < g_blacklisted_addr_count - 1; ++j) {
            bd_addr_copy(g_blacklisted_addrs[j], g_blacklisted_addrs[j + 1]);
        }
        --g_blacklisted_addr_count;
        g_blacklist_dirty = true;
        g_blacklist_dirty_ms = now_ms();
        std::printf("[BT] Removed %s from blacklist, persistence deferred\n",
                    bd_addr_to_str(addr));
        return;
    }
}

void __not_in_flash_func(hci_packet_handler)(uint8_t packet_type,
                                             uint16_t channel,
                                             uint8_t *packet, uint16_t size)
{
    (void)packet_type;
    (void)channel;

    const uint8_t event_type = hci_event_packet_get_type(packet);
    switch (event_type) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            bd_addr_t local_addr{};
            gap_local_bd_addr(local_addr);
            SwitchControllerBdAddr identity{};
            std::memcpy(identity.data(), local_addr, identity.size());
            switch_identity_set_bd_addr(identity);
            std::printf("[BT] Local controller addr %s\n",
                        bd_addr_to_str(local_addr));
            bt_debug_log_event(BtDebugEvent::StackReady);
            blacklist_load();
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

        BtControllerKind hint = BtControllerKind::Unknown;
        if (event_type == HCI_EVENT_EXTENDED_INQUIRY_RESPONSE &&
            size > 17u) {
            hint = kind_from_eir(packet + 17u, size - 17u);
        }

        if (is_controller_candidate(cod, hint)) {
            std::printf("[BT] Gamepad found: %s cod=0x%06x hint=%s\n",
                        bd_addr_to_str(addr), static_cast<unsigned>(cod),
                        controller_kind_name(hint));
            bt_debug_log_event(BtDebugEvent::InquiryResult,
                               controller_kind_code(hint), 0,
                               static_cast<uint32_t>(cod));
            bd_addr_copy(g_current_addr, addr);
            g_controller_kind_hint = hint;
            g_device_found = true;
            gap_inquiry_stop();
        }
        break;
    }

    case GAP_EVENT_INQUIRY_COMPLETE:
    case HCI_EVENT_INQUIRY_COMPLETE:
        if (g_paused_for_switch2_wake) {
            g_device_found = false;
            break;
        }
        if (g_device_found) {
            std::printf("[BT] Connecting to %s\n", bd_addr_to_str(g_current_addr));
            g_local_acl_initiated = true;
            bt_debug_log_event(BtDebugEvent::CreateConnection,
                               controller_kind_code(g_controller_kind_hint));
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
        if (g_paused_for_switch2_wake) {
            hci_send_cmd(&hci_reject_connection_request, addr, 0x0f);
            break;
        }
        if (blacklist_contains(addr)) {
            std::printf("[BT] Rejecting blacklisted controller %s\n",
                        bd_addr_to_str(addr));
            hci_send_cmd(&hci_reject_connection_request, addr, 0x0f);
            break;
        }
        if (!is_controller_candidate(cod, BtControllerKind::Unknown)) {
            std::printf("[BT] Accepting incoming unknown COD from %s cod=0x%06x\n",
                        bd_addr_to_str(addr), static_cast<unsigned>(cod));
        }
        bt_debug_log_event(BtDebugEvent::ConnectionRequest, 0, 0,
                           static_cast<uint32_t>(cod));
        bd_addr_copy(g_current_addr, addr);
        g_controller_kind_hint = BtControllerKind::Unknown;
        g_local_acl_initiated = false;
        gap_inquiry_stop();
        hci_send_cmd(&hci_accept_connection_request, addr, 0x01);
        break;
    }

    case HCI_EVENT_CONNECTION_COMPLETE: {
        const uint8_t status = hci_event_connection_complete_get_status(packet);
        bt_debug_log_event(BtDebugEvent::ConnectionComplete,
                           controller_kind_code(g_controller_kind_hint),
                           status,
                           status == ERROR_CODE_SUCCESS
                               ? hci_event_connection_complete_get_connection_handle(packet)
                               : 0);
        if (status == ERROR_CODE_SUCCESS) {
            g_controller_kind = BtControllerKind::Unknown;
            g_dualsense_init_sent = false;
            g_dualsense_features_requested = false;
            g_dualshock4_init_sent = false;
            g_dualshock4_features_requested = false;
            g_switch_pro_init_sent = false;
            g_acl_handle =
                hci_event_connection_complete_get_connection_handle(packet);
            hci_event_connection_complete_get_bd_addr(packet, g_current_addr);
            std::printf("[BT] ACL connected handle=0x%04x %s\n",
                        g_acl_handle,
                        g_local_acl_initiated ? "local" : "incoming");
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
        bt_debug_log_event(
            BtDebugEvent::AuthComplete,
            controller_kind_code(g_controller_kind_hint),
            hci_event_authentication_complete_get_status(packet),
            hci_event_authentication_complete_get_connection_handle(packet));
        if (hci_event_authentication_complete_get_status(packet) ==
            ERROR_CODE_SUCCESS) {
            hci_send_cmd(&hci_set_connection_encryption,
                         hci_event_authentication_complete_get_connection_handle(packet),
                         1);
        } else {
            gap_drop_link_key_for_bd_addr(g_current_addr);
            classic_hid_disconnect();
        }
        break;

    case HCI_EVENT_ENCRYPTION_CHANGE:
        bt_debug_log_event(
            BtDebugEvent::EncryptionChange,
            controller_kind_code(g_controller_kind_hint),
            static_cast<uint16_t>(
                (hci_event_encryption_change_get_status(packet) << 8u) |
                hci_event_encryption_change_get_encryption_enabled(packet)),
            hci_event_encryption_change_get_connection_handle(packet));
        if (hci_event_encryption_change_get_status(packet) == ERROR_CODE_SUCCESS &&
            hci_event_encryption_change_get_encryption_enabled(packet) != 0) {
            if (start_controller_kind_probe()) {
                break;
            }
            continue_hid_open_after_probe();
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        const uint8_t reason =
            hci_event_disconnection_complete_get_reason(packet);
        std::printf("[BT] Disconnected reason=0x%02x\n", reason);
        bt_debug_log_event(BtDebugEvent::Disconnect,
                           controller_kind_code(g_controller_kind), reason);
        reset_connection_state();
        while (!queue_is_empty(&g_send_fifo)) {
            SendElement discarded{};
            queue_try_remove(&g_send_fifo, &discarded);
        }
        maybe_start_inquiry();
        break;
    }

    default:
        break;
    }
}

void __not_in_flash_func(l2cap_packet_handler)(uint8_t packet_type,
                                               uint16_t channel,
                                               uint8_t *packet, uint16_t size)
{
    if (packet_type == L2CAP_DATA_PACKET) {
        if (channel == g_hid_interrupt_cid) {
            if (!g_logged_first_interrupt_data) {
                g_logged_first_interrupt_data = true;
                const uint32_t first_bytes =
                    (size > 0 ? packet[0] : 0) |
                    ((size > 1 ? packet[1] : 0) << 8u);
                bt_debug_log_event(BtDebugEvent::HidData,
                                   controller_kind_code(g_controller_kind),
                                   size, first_bytes);
            }
            const BtControllerKind report_kind =
                kind_from_hid_report(packet, size);
            if (report_kind == BtControllerKind::Unknown &&
                !g_unknown_interrupt_report_logged) {
                g_unknown_interrupt_report_logged = true;
                std::printf("[BT] Unknown HID interrupt report len=%u", size);
                if (size > 0) {
                    std::printf(" first=0x%02x", packet[0]);
                }
                if (size > 1) {
                    std::printf(" second=0x%02x", packet[1]);
                }
                std::printf("\n");
            }
            set_controller_kind(report_kind);
            if (g_data_callback != nullptr) {
                g_data_callback(ClassicHidChannel::Interrupt, packet, size);
            }
        } else if (channel == g_hid_control_cid) {
            if (!g_logged_first_control_data) {
                g_logged_first_control_data = true;
                const uint32_t first_bytes =
                    (size > 0 ? packet[0] : 0) |
                    ((size > 1 ? packet[1] : 0) << 8u);
                bt_debug_log_event(BtDebugEvent::HidData,
                                   controller_kind_code(g_controller_kind),
                                   size, first_bytes | 0x80000000u);
            }
            set_controller_kind(kind_from_hid_report(packet, size));
            if (g_data_callback != nullptr) {
                g_data_callback(ClassicHidChannel::Control, packet, size);
            }
        } else {
            std::printf("[BT] L2CAP data on unknown cid=0x%04x len=%u\n",
                        channel, size);
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
        bt_debug_log_event(
            BtDebugEvent::L2capOpen,
            controller_kind_code(g_controller_kind_hint),
            static_cast<uint16_t>((status << 8u) | (psm & 0xffu)),
            (static_cast<uint32_t>(psm) << 16u) | local_cid);
        if (psm == PSM_HID_CONTROL) {
            g_hid_control_opening = false;
        } else if (psm == PSM_HID_INTERRUPT) {
            g_hid_interrupt_opening = false;
        }
        if (status != ERROR_CODE_SUCCESS) {
            std::printf("[BT] L2CAP open failed psm=0x%04x status=0x%02x\n",
                        psm, status);
            if (psm == PSM_HID_CONTROL) {
                g_hid_control_cid = 0;
            } else if (psm == PSM_HID_INTERRUPT) {
                g_hid_interrupt_cid = 0;
            }
            (void)classic_hid_disconnect();
            break;
        }

        if (psm == PSM_HID_CONTROL) {
            g_hid_control_cid = local_cid;
            std::printf("[BT] HID control open cid=0x%04x\n", local_cid);
            if (g_controller_kind_hint == BtControllerKind::SwitchPro) {
                set_controller_kind(BtControllerKind::SwitchPro);
            } else if (g_controller_kind_hint == BtControllerKind::DualSense) {
                set_controller_kind(BtControllerKind::DualSense);
            } else if (g_controller_kind_hint == BtControllerKind::DualShock4) {
                set_controller_kind(BtControllerKind::DualShock4);
            } else if (g_controller_kind_hint == BtControllerKind::Xbox) {
                set_controller_kind(BtControllerKind::Xbox);
            } else {
                request_initial_features();
            }
            if (g_controller_kind == BtControllerKind::DualShock4) {
                request_dualshock4_features();
            }
            if (g_local_acl_initiated) {
                open_next_outgoing_hid_channel();
            } else {
                std::printf("[BT] Incoming HID control, waiting for interrupt\n");
            }
        } else if (psm == PSM_HID_INTERRUPT) {
            g_hid_interrupt_cid = local_cid;
            std::printf("[BT] HID interrupt open cid=0x%04x\n", local_cid);
            blacklist_remove(g_current_addr);
            gap_connectable_control(false);
            gap_discoverable_control(false);
            if (g_controller_kind_hint != BtControllerKind::Unknown) {
                set_controller_kind(g_controller_kind_hint);
            } else {
                set_controller_kind(BtControllerKind::DualSense);
            }
            if (g_controller_kind == BtControllerKind::DualSense) {
                send_initial_output();
            } else if (g_controller_kind == BtControllerKind::DualShock4) {
                send_dualshock4_initial_output();
            } else if (g_controller_kind == BtControllerKind::SwitchPro) {
                send_switch_pro_init();
            }
        }
        break;
    }

    case L2CAP_EVENT_INCOMING_CONNECTION: {
        const uint16_t local_cid =
            l2cap_event_incoming_connection_get_local_cid(packet);
        const uint16_t psm = l2cap_event_incoming_connection_get_psm(packet);
        std::printf("[BT] Incoming L2CAP psm=0x%04x cid=0x%04x\n",
                    psm, local_cid);
        bt_debug_log_event(BtDebugEvent::L2capIncoming,
                           controller_kind_code(g_controller_kind_hint), psm,
                           local_cid);
        l2cap_accept_connection(local_cid);
        break;
    }

    case L2CAP_EVENT_CHANNEL_CLOSED: {
        const uint16_t local_cid =
            l2cap_event_channel_closed_get_local_cid(packet);
        bt_debug_log_event(BtDebugEvent::L2capClosed,
                           controller_kind_code(g_controller_kind), local_cid);
        if (local_cid == g_hid_control_cid) {
            g_hid_control_cid = 0;
            g_hid_control_opening = false;
        }
        if (local_cid == g_hid_interrupt_cid) {
            g_hid_interrupt_cid = 0;
            g_hid_interrupt_opening = false;
        }
        if (g_hid_control_cid == 0 && g_hid_interrupt_cid == 0) {
            classic_hid_disconnect();
        }
        break;
    }

    case L2CAP_EVENT_CAN_SEND_NOW: {
        SendElement element{};
        if (queue_try_remove(&g_send_fifo, &element)) {
            uint8_t packet[kManualHidPacketSize]{};
            packet[0] = 0xa2u;
            std::memcpy(packet + 1u, element.data, element.len);
            const uint16_t packet_len =
                static_cast<uint16_t>(element.len + 1u);
            const uint8_t status =
                l2cap_send(g_hid_interrupt_cid, packet, packet_len);
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

void classic_hid_register_data_callback(ClassicHidDataCallback callback)
{
    g_data_callback = callback;
}

bool classic_hid_init()
{
    queue_init(&g_send_fifo, sizeof(SendElement), kSendQueueDepth);

    l2cap_init();
    steam_hog_bt_stack_init();
    sdp_init();
    sdp_client_init();
    l2cap_register_service(l2cap_packet_handler, PSM_HID_CONTROL, kMtuControl,
                           LEVEL_2);
    l2cap_register_service(l2cap_packet_handler, PSM_HID_INTERRUPT,
                           kMtuInterrupt, LEVEL_2);

    gap_ssp_set_enable(true);
    gap_secure_connections_enable(true);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_DISPLAY_YES_NO);
    gap_ssp_set_authentication_requirement(
        SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE |
                                         LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
    hci_set_master_slave_policy(HCI_ROLE_MASTER);
    gap_set_page_scan_activity(0x0012, 0x0012);
    gap_set_page_scan_type(PAGE_SCAN_MODE_INTERLACED);
    gap_connectable_control(1);
    gap_discoverable_control(1);

    g_l2cap_callback.callback = &l2cap_packet_handler;
    l2cap_add_event_handler(&g_l2cap_callback);
    g_hci_callback.callback = &hci_packet_handler;
    hci_add_event_handler(&g_hci_callback);

    // Do not override the controller's Bluetooth address here. The Pico keeps
    // its own factory-derived public address (CYW43 Wi-Fi MAC + 1, set by the
    // SDK CYW43 BTstack transport). The USB Switch Pro persona reports this same
    // address (see configure_switch_identity_from_radio() in main.cpp), so the
    // identity advertised to the Switch 2 and the real radio address match.
    return hci_power_control(HCI_POWER_ON) == 0;
}

bool __not_in_flash_func(classic_hid_is_connected)()
{
    return g_hid_interrupt_cid != 0;
}

BtControllerKind __not_in_flash_func(classic_hid_controller_kind)()
{
    return g_controller_kind;
}

bool __not_in_flash_func(classic_hid_send_dualsense_output_report)(
    const DualSenseBtOutputReport &report)
{
    return classic_hid_send_raw_output_report(report.data(), report.size());
}

bool __not_in_flash_func(classic_hid_send_raw_output_report)(
    const uint8_t *report, std::size_t size)
{
    if (g_hid_interrupt_cid == 0) {
        return false;
    }
    if (report == nullptr || size == 0 || size > kSendPacketSize) {
        return false;
    }

    SendElement element{};
    element.len = static_cast<uint16_t>(size);
    std::memcpy(element.data, report, size);

    if (!queue_try_add(&g_send_fifo, &element)) {
        return false;
    }

    if (queue_get_level(&g_send_fifo) == 1) {
        l2cap_request_can_send_now_event(g_hid_interrupt_cid);
    }

    return true;
}

bool __not_in_flash_func(classic_hid_power_off_active_controller)()
{
    if (g_controller_kind != BtControllerKind::DualSense ||
        g_hid_control_cid == 0) {
        return false;
    }

    uint8_t packet[2u + kDualSenseBluetoothControlPayloadSize]{};
    packet[0] = kHidSetReportFeature;
    packet[1] = kDualSenseFeatureReportBluetoothControl;
    packet[2] = kDualSenseBluetoothControlOff;

    const uint32_t crc = dualsense_crc32_seeded(
        kDualSenseFeatureCrcSeed, packet + 1u, sizeof(packet) - 1u - 4u);
    packet[sizeof(packet) - 4u] = static_cast<uint8_t>(crc);
    packet[sizeof(packet) - 3u] = static_cast<uint8_t>(crc >> 8u);
    packet[sizeof(packet) - 2u] = static_cast<uint8_t>(crc >> 16u);
    packet[sizeof(packet) - 1u] = static_cast<uint8_t>(crc >> 24u);

    const uint8_t status =
        l2cap_send(g_hid_control_cid, packet, sizeof(packet));
    if (status != ERROR_CODE_SUCCESS) {
        std::printf("[BT] DualSense power-off failed status=0x%02x\n", status);
        return false;
    }

    std::printf("[BT] DualSense power-off requested\n");
    return true;
}

bool classic_hid_disconnect()
{
    if (g_acl_handle == HCI_CON_HANDLE_INVALID) {
        return false;
    }

    hci_send_cmd(&hci_disconnect, g_acl_handle,
                 kHciDisconnectRemoteUserTerminated);
    return true;
}

void classic_hid_pause_for_switch2_wake()
{
    g_paused_for_switch2_wake = true;
    g_device_found = false;
    gap_inquiry_stop();
    gap_connectable_control(false);
    gap_discoverable_control(false);
}

void classic_hid_resume_after_switch2_wake()
{
    g_paused_for_switch2_wake = false;
    if (g_acl_handle != HCI_CON_HANDLE_INVALID || classic_hid_is_connected()) {
        return;
    }
    maybe_start_inquiry();
}

void classic_hid_bootsel_click_action()
{
    if (g_acl_handle != HCI_CON_HANDLE_INVALID) {
        std::printf("[BT] BOOTSEL click - disconnect current ACL\n");
        (void)classic_hid_disconnect();
    } else {
        std::printf("[BT] BOOTSEL click - fresh inquiry\n");
        reset_connection_state();
        maybe_start_inquiry();
    }
}

void classic_hid_forget_pairings()
{
    std::printf("[BT] BOOTSEL hold - forget all paired controllers\n");

    btstack_link_key_iterator_t it{};
    if (gap_link_key_iterator_init(&it)) {
        bd_addr_t addr{};
        link_key_t key{};
        link_key_type_t type{};
        while (gap_link_key_iterator_get_next(&it, addr, key, &type)) {
            blacklist_add_unique(addr);
        }
        gap_link_key_iterator_done(&it);
    }

    if (g_acl_handle != HCI_CON_HANDLE_INVALID) {
        blacklist_add_unique(g_current_addr);
        (void)classic_hid_disconnect();
    }

    gap_delete_all_link_keys();
    blacklist_persist();
    if (g_acl_handle == HCI_CON_HANDLE_INVALID) {
        maybe_start_inquiry();
    }
}

void classic_hid_persist_blacklist_if_dirty()
{
    if (!g_blacklist_dirty ||
        now_ms() - g_blacklist_dirty_ms < kBlacklistPersistDelayMs) {
        return;
    }

    g_blacklist_dirty = false;
    blacklist_persist();
}

} // namespace switchbridge

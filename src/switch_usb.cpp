#include "ds5onswitch/switch_usb.h"

#include "ds5onswitch/switch_identity.h"
#include "ds5onswitch/switch_pro.h"

#include <algorithm>
#include <cstring>

namespace ds5onswitch {

namespace {

constexpr uint8_t kNintendoSwitchName[] = {
    'N', 'i', 'n', 't', 'e', 'n', 'd', 'o', ' ',
    'S', 'w', 'i', 't', 'c', 'h',
};

void put_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xffu);
    out[1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
}

void put_i16_le(uint8_t *out, int16_t value)
{
    put_u16_le(out, static_cast<uint16_t>(value));
}

void put_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xffu);
    out[1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
    out[2] = static_cast<uint8_t>((value >> 16u) & 0xffu);
    out[3] = static_cast<uint8_t>((value >> 24u) & 0xffu);
}

uint32_t get_u32_le(const uint8_t *in)
{
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8u) |
           (static_cast<uint32_t>(in[2]) << 16u) |
           (static_cast<uint32_t>(in[3]) << 24u);
}

bool is_valid_addr_wire(const uint8_t addr[6])
{
    bool any_nonzero = false;
    bool any_not_ff = false;
    for (std::size_t i = 0; i < 6; ++i) {
        any_nonzero = any_nonzero || addr[i] != 0x00u;
        any_not_ff = any_not_ff || addr[i] != 0xffu;
    }
    return any_nonzero && any_not_ff;
}

bool contains_nintendo_switch_name(const uint8_t *data, std::size_t size)
{
    if (data == nullptr || size < sizeof(kNintendoSwitchName)) {
        return false;
    }

    for (std::size_t i = 0; i <= size - sizeof(kNintendoSwitchName); ++i) {
        if (std::memcmp(data + i, kNintendoSwitchName,
                        sizeof(kNintendoSwitchName)) == 0) {
            return true;
        }
    }
    return false;
}

bool extract_switch2_console_addr_from_manual_pairing(const uint8_t *args,
                                                      std::size_t args_size,
                                                      uint8_t out_addr_wire[6])
{
    if (args == nullptr || out_addr_wire == nullptr || args_size < 7) {
        return false;
    }

    const uint8_t phase = args[0];
    if (phase != 0x01u && phase != 0x04u) {
        return false;
    }

    if (!is_valid_addr_wire(&args[1])) {
        return false;
    }

    // Third-party Switch Pro 1 wired-pairing captures include the console name
    // after the address. Keep this as an anti-false-positive guard so ordinary
    // 0x01 phases from non-Switch hosts do not overwrite the wake target.
    if (!contains_nintendo_switch_name(args, args_size)) {
        return false;
    }

    std::memcpy(out_addr_wire, &args[1], 6);
    return true;
}

void copy_window(uint8_t *out, std::size_t out_len, uint32_t address,
                 uint32_t base, const uint8_t *src, std::size_t src_len)
{
    const uint64_t req_start = address;
    const uint64_t req_end = req_start + out_len;
    const uint64_t src_start = base;
    const uint64_t src_end = src_start + src_len;

    if (out_len == 0 || src_len == 0 || req_end <= src_start || src_end <= req_start) {
        return;
    }

    const uint64_t overlap_start = std::max(req_start, src_start);
    const uint64_t overlap_end = std::min(req_end, src_end);
    std::memcpy(&out[overlap_start - req_start],
                &src[overlap_start - src_start],
                static_cast<std::size_t>(overlap_end - overlap_start));
}

void encode_stick_calibration(uint8_t out[9], uint16_t neutral_x,
                              uint16_t neutral_y, uint16_t max_x,
                              uint16_t max_y, uint16_t min_x,
                              uint16_t min_y)
{
    pack_switch_stick12(&out[0], neutral_x, neutral_y);
    pack_switch_stick12(&out[3], max_x, max_y);
    pack_switch_stick12(&out[6], min_x, min_y);
}

void fill_imu_calibration(uint8_t *block, std::size_t block_len, uint32_t address)
{
    uint8_t cal[24]{};
    put_i16_le(&cal[6], 0x4000);
    put_i16_le(&cal[8], 0x4000);
    put_i16_le(&cal[10], 0x4000);
    put_i16_le(&cal[18], 0x343b);
    put_i16_le(&cal[20], 0x343b);
    put_i16_le(&cal[22], 0x343b);
    copy_window(block, block_len, address, 0x6020u, cal, sizeof(cal));
}

void fill_stick_calibration(uint8_t *block, std::size_t block_len,
                            uint32_t address, uint32_t base, bool left)
{
    uint8_t cal[9]{};
    if (left) {
        encode_stick_calibration(cal, 2047u, 2047u, 0x0800u, 0x0800u,
                                 2048u, 2048u);
    } else {
        encode_stick_calibration(cal, 0x0800u, 0x0800u, 2048u, 2048u,
                                 2047u, 2047u);
    }
    copy_window(block, block_len, address, base, cal, sizeof(cal));
}

void fill_factory_metadata(uint8_t *block, std::size_t block_len, uint32_t address)
{
    static const uint8_t serial[16] = {
        '0', '0', '0', '0', '0', '0', '0', '0',
        '0', '0', '0', '1', 0xff, 0xff, 0xff, 0xff,
    };
    static const uint8_t type[] = {0x03, 0xa0};
    static const uint8_t color_valid[] = {0x01};
    static const uint8_t colors[] = {
        0x32, 0x32, 0x32, 0xff, 0xff, 0xff,
        0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
    };

    copy_window(block, block_len, address, 0x6000u, serial, sizeof(serial));
    copy_window(block, block_len, address, 0x6012u, type, sizeof(type));
    copy_window(block, block_len, address, 0x601bu, color_valid, sizeof(color_valid));
    copy_window(block, block_len, address, 0x6050u, colors, sizeof(colors));
}

void encode_pair12(uint8_t out[3], uint16_t a, uint16_t b)
{
    pack_switch_stick12(out, a, b);
}

void fill_factory_parameters(uint8_t *block, std::size_t block_len,
                             uint32_t address)
{
    uint8_t horizontal[6]{};
    uint8_t stick[18]{};
    static const uint16_t values[12] = {
        0x0019u, 0x04cdu, 0x00aeu, 0x0e14u,
        0x02eeu, 0x02eeu, 0x02eeu, 0x02eeu,
        0x0ab4u, 0x0ab4u, 0x0496u, 0x0496u,
    };

    put_i16_le(&horizontal[0], -688);
    put_i16_le(&horizontal[2], 0);
    put_i16_le(&horizontal[4], 4038);
    copy_window(block, block_len, address, 0x6080u, horizontal, sizeof(horizontal));

    for (std::size_t i = 0; i < 6; ++i) {
        encode_pair12(&stick[i * 3u], values[i * 2u], values[i * 2u + 1u]);
    }
    copy_window(block, block_len, address, 0x6086u, stick, sizeof(stick));
    copy_window(block, block_len, address, 0x6098u, stick, sizeof(stick));
}

std::size_t manual_pairing_payload(const uint8_t *args, std::size_t args_size,
                                   uint8_t *out, std::size_t out_size)
{
    constexpr std::size_t kPairingPayloadLen = 49;
    static const uint8_t kPairingMaterial[16] = {
        0xeb, 0xe0, 0xf9, 0xf6, 0xcc, 0xda, 0xd0, 0x2e,
        0x25, 0x30, 0x0f, 0x1f, 0x6f, 0x7f, 0x4c, 0x5d,
    };
    static const uint8_t kControllerName[] = {
        'P', 'r', 'o', ' ', 'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r',
    };

    if (out_size < kPairingPayloadLen) {
        return 0;
    }

    const uint8_t request = args != nullptr && args_size >= 1 ? args[0] : 0;
    std::memset(out, 0, out_size);
    switch (request) {
    case 0x01:
        out[0] = 0x01;
        for (std::size_t i = 0; i < kSwitchProControllerBdAddr.size(); ++i) {
            out[1u + i] =
                kSwitchProControllerBdAddr[kSwitchProControllerBdAddr.size() - 1u - i];
        }
        out[8] = 0x25;
        out[9] = 0x08;
        std::memcpy(&out[10], kControllerName, sizeof(kControllerName));
        return kPairingPayloadLen;
    case 0x02:
        out[0] = 0x02;
        std::memcpy(&out[1], kPairingMaterial, sizeof(kPairingMaterial));
        return kPairingPayloadLen;
    case 0x03:
    case 0x04:
        out[0] = 0x03;
        return kPairingPayloadLen;
    default:
        out[0] = request;
        return 1;
    }
}

std::size_t spi_read_payload(const uint8_t *args, std::size_t args_size,
                             uint8_t *out, std::size_t out_size)
{
    if (args == nullptr || args_size < 5) {
        if (out_size < 5) {
            return 0;
        }
        std::memset(out, 0, 5);
        return 5;
    }

    const uint32_t address = get_u32_le(args);
    uint8_t length = args[4];
    if (length > 0x30u) {
        length = 0x30u;
    }
    if (out_size < static_cast<std::size_t>(length) + 5u) {
        return 0;
    }

    std::memset(out, 0, static_cast<std::size_t>(length) + 5u);
    put_u32_le(&out[0], address);
    out[4] = length;
    std::memset(&out[5], 0xff, length);
    fill_factory_metadata(&out[5], length, address);
    fill_imu_calibration(&out[5], length, address);
    fill_stick_calibration(&out[5], length, address, 0x603du, true);
    fill_stick_calibration(&out[5], length, address, 0x6046u, false);
    fill_factory_parameters(&out[5], length, address);
    return static_cast<std::size_t>(length) + 5u;
}

uint8_t subcommand_ack(uint8_t subcommand)
{
    switch (subcommand) {
    case kSwitchSubcmdSpiRead:
        return 0x90u;
    case kSwitchSubcmdBtManualPairing:
        return 0x81u;
    case kSwitchSubcmdRequestDeviceInfo:
        return 0x82u;
    case kSwitchSubcmdGetPlayerLights:
        return 0xb0u;
    case kSwitchSubcmdReadImuReg:
        return 0xc0u;
    case kSwitchSubcmdGetRegulatedVoltage:
        return 0xd0u;
    default:
        return 0x80u;
    }
}

} // namespace

void SwitchUsbEmulator::set_state(const SwitchProState &state)
{
    state_ = state;
}

const SwitchProState &SwitchUsbEmulator::state() const
{
    return state_;
}

SwitchReport SwitchUsbEmulator::build_input_report()
{
    return build_switch_pro_input_report(state_, counter_++, vibration_enabled_);
}

bool SwitchUsbEmulator::build_usb_response(uint8_t command, SwitchReport &out) const
{
    if (command == kSwitchUsbCmdNoTimeout) {
        return false;
    }

    out.fill(0);
    out[0] = kSwitchProUsbResponseReportId;
    out[1] = command;
    out[3] = 0x03u;
    if (command == kSwitchUsbCmdConnStatus) {
        // Real Switch Pro USB connection-status replies carry the controller
        // Bluetooth address in HID wire order (little-endian BD_ADDR), while
        // the device-info subcommand carries it in display/public order.
        for (std::size_t i = 0; i < kSwitchProControllerBdAddr.size(); ++i) {
            out[4u + i] =
                kSwitchProControllerBdAddr[kSwitchProControllerBdAddr.size() - 1u - i];
        }
    }
    return true;
}

bool SwitchUsbEmulator::build_subcommand_reply(uint8_t subcommand,
                                               const uint8_t *args,
                                               std::size_t args_size,
                                               SwitchReport &out)
{
    uint8_t payload[54]{};
    std::size_t payload_len = 0;

    switch (subcommand) {
    case kSwitchSubcmdBtManualPairing:
        payload_len = manual_pairing_payload(args, args_size, payload, sizeof(payload));
        break;
    case kSwitchSubcmdRequestDeviceInfo:
    {
        static const uint8_t prefix[] = {0x03, 0x48, 0x03, 0x02};
        std::memcpy(payload, prefix, sizeof(prefix));
        std::memcpy(payload + sizeof(prefix), kSwitchProControllerBdAddr.data(),
                    kSwitchProControllerBdAddr.size());
        payload[10] = 0x01;
        payload[11] = 0x03;
        payload_len = 12;
        break;
    }
    case kSwitchSubcmdSpiRead:
        payload_len = spi_read_payload(args, args_size, payload, sizeof(payload));
        break;
    case kSwitchSubcmdSpiWrite:
    case kSwitchSubcmdSpiErase:
        payload[0] = 0x00u;
        payload_len = 1;
        break;
    case kSwitchSubcmdGetPlayerLights:
        payload[0] = player_led_mask_;
        payload_len = 1;
        break;
    case kSwitchSubcmdReadImuReg:
    {
        const uint8_t reg = args != nullptr && args_size >= 1 ? args[0] : 0;
        uint8_t count = args != nullptr && args_size >= 2 ? args[1] : 1;
        if (count == 0) {
            count = 1;
        }
        if (count > 0x20u) {
            count = 0x20u;
        }
        payload[0] = reg;
        payload[1] = count;
        std::memset(&payload[2], 0, count);
        payload_len = static_cast<std::size_t>(count) + 2u;
        break;
    }
    case kSwitchSubcmdGetRegulatedVoltage:
        put_u16_le(payload, 0x0640u);
        payload_len = 2;
        break;
    case kSwitchSubcmdStateOnly:
        payload[0] = 0x03u;
        payload_len = 1;
        break;
    default:
        payload_len = 0;
        break;
    }

    out = build_switch_pro_input_report(state_, counter_++, vibration_enabled_);
    out[0] = kSwitchProSubcommandReplyReportId;
    out[13] = subcommand_ack(subcommand);
    out[14] = subcommand;
    if (payload_len > 0) {
        const std::size_t n = std::min<std::size_t>(payload_len, 49);
        std::memcpy(&out[15], payload, n);
    }
    return true;
}

SwitchUsbOutputResult SwitchUsbEmulator::handle_output_report(const uint8_t *data,
                                                              std::size_t size)
{
    SwitchUsbOutputResult result{};
    if (data == nullptr || size == 0) {
        return result;
    }

    switch (data[0]) {
    case kSwitchOutputUsb:
        if (size >= 2 && build_usb_response(data[1], result.reply)) {
            result.has_reply = true;
            if (data[1] == kSwitchUsbCmdReset) {
                active_report_id_ = 0;
            }
        }
        break;
    case kSwitchOutputSubcommand:
        if (size >= 10) {
            std::memcpy(result.feedback.left_rumble.data(), &data[2], 4);
            std::memcpy(result.feedback.right_rumble.data(), &data[6], 4);
            result.feedback.has_rumble = true;
            result.feedback.haptics =
                rumble_translator_.translate(result.feedback.left_rumble,
                                             result.feedback.right_rumble);
        }
        if (size >= 11) {
            const uint8_t subcommand = data[10];
            const uint8_t *args = size > 11 ? &data[11] : nullptr;
            const std::size_t args_size = size > 11 ? size - 11 : 0;
            if (subcommand == kSwitchSubcmdBtManualPairing &&
                extract_switch2_console_addr_from_manual_pairing(
                    args, args_size, result.switch2_console_addr_wire)) {
                result.has_switch2_console_addr = true;
            }
            handle_subcommand_state(subcommand, args, args_size, result.feedback);
            if (build_subcommand_reply(subcommand, args, args_size, result.reply)) {
                result.has_reply = true;
                result.queue_full_report =
                    subcommand == kSwitchSubcmdSetReportMode &&
                    args != nullptr && args_size >= 1 &&
                    args[0] == kSwitchProFullInputReportId;
            }
        }
        break;
    case kSwitchOutputRumbleOnly:
        if (size >= 10) {
            std::memcpy(result.feedback.left_rumble.data(), &data[2], 4);
            std::memcpy(result.feedback.right_rumble.data(), &data[6], 4);
            result.feedback.has_rumble = true;
            result.feedback.haptics =
                rumble_translator_.translate(result.feedback.left_rumble,
                                             result.feedback.right_rumble);
        }
        break;
    default:
        break;
    }

    return result;
}

bool SwitchUsbEmulator::imu_enabled() const
{
    return imu_enabled_;
}

bool SwitchUsbEmulator::vibration_enabled() const
{
    return vibration_enabled_;
}

uint8_t SwitchUsbEmulator::player_led_mask() const
{
    return player_led_mask_;
}

uint8_t SwitchUsbEmulator::active_report_id() const
{
    return active_report_id_;
}

void SwitchUsbEmulator::handle_subcommand_state(uint8_t subcommand,
                                                const uint8_t *args,
                                                std::size_t args_size,
                                                SwitchUsbFeedback &feedback)
{
    switch (subcommand) {
    case kSwitchSubcmdSetReportMode:
        if (args != nullptr && args_size >= 1) {
            active_report_id_ = args[0];
        }
        break;
    case kSwitchSubcmdSetPlayerLights:
        if (args != nullptr && args_size >= 1) {
            player_led_mask_ = args[0];
            feedback.player_led_changed = true;
            feedback.player_led_mask = player_led_mask_;
        }
        break;
    case kSwitchSubcmdEnableImu:
        if (args != nullptr && args_size >= 1) {
            imu_enabled_ = args[0] != 0;
        }
        break;
    case kSwitchSubcmdEnableVibration:
        if (args != nullptr && args_size >= 1) {
            vibration_enabled_ = args[0] != 0;
        }
        break;
    default:
        break;
    }
}

} // namespace ds5onswitch

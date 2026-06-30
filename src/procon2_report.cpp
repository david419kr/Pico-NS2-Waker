#include "switch_bridge/procon2_report.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace switchbridge {

namespace {

constexpr uint32_t kQ15One = 32768u;
constexpr uint16_t kCenteredFullScale = 2047u;
constexpr uint16_t kNeutral12 = 2048u;

uint16_t read_u16_le(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8u);
}

uint32_t read_u32_le(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

int16_t read_i16_le(const uint8_t *p)
{
    return static_cast<int16_t>(read_u16_le(p));
}

void write_u32_le(uint8_t *p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8u);
    p[2] = static_cast<uint8_t>(value >> 16u);
    p[3] = static_cast<uint8_t>(value >> 24u);
}

int16_t neg_sat(int16_t value)
{
    return value == std::numeric_limits<int16_t>::min()
               ? std::numeric_limits<int16_t>::max()
               : static_cast<int16_t>(-value);
}

bool mac_all_zero(const std::array<uint8_t, 6> &mac)
{
    for (uint8_t b : mac) {
        if (b != 0u) {
            return false;
        }
    }
    return true;
}

void add_button(uint32_t bits, uint8_t bit, SwitchButton button,
                uint32_t &buttons)
{
    if ((bits & (1u << bit)) != 0u) {
        buttons |= static_cast<uint32_t>(button);
    }
}

uint32_t procon2_button_bits_to_switch(uint32_t bits)
{
    uint32_t buttons = 0;
    add_button(bits, 0, kNsY, buttons);
    add_button(bits, 1, kNsX, buttons);
    add_button(bits, 2, kNsB, buttons);
    add_button(bits, 3, kNsA, buttons);
    add_button(bits, 6, kNsR, buttons);
    add_button(bits, 7, kNsZR, buttons);
    add_button(bits, 8, kNsMinus, buttons);
    add_button(bits, 9, kNsPlus, buttons);
    add_button(bits, 10, kNsRightStick, buttons);
    add_button(bits, 11, kNsLeftStick, buttons);
    add_button(bits, 12, kNsHome, buttons);
    add_button(bits, 13, kNsCapture, buttons);
    add_button(bits, 16, kNsDown, buttons);
    add_button(bits, 17, kNsUp, buttons);
    add_button(bits, 18, kNsRight, buttons);
    add_button(bits, 19, kNsLeft, buttons);
    add_button(bits, 22, kNsL, buttons);
    add_button(bits, 23, kNsZL, buttons);
    return buttons;
}

int16_t calibrate_axis(uint16_t raw, const Procon2StickAxisCalibration &cal,
                       bool invert)
{
    int scaled = 0;
    if (cal.neutral != 0u && cal.max_above != 0u && cal.max_below != 0u) {
        const int delta = static_cast<int>(raw) - static_cast<int>(cal.neutral);
        if (delta >= 0) {
            scaled = (delta * static_cast<int>(kCenteredFullScale)) /
                     static_cast<int>(cal.max_above);
        } else {
            scaled = (delta * static_cast<int>(kCenteredFullScale)) /
                     static_cast<int>(cal.max_below);
        }
    } else {
        scaled = static_cast<int>(raw) - static_cast<int>(kNeutral12);
    }

    if (invert) {
        scaled = -scaled;
    }
    scaled = std::clamp(scaled, -static_cast<int>(kCenteredFullScale),
                        static_cast<int>(kCenteredFullScale));
    return static_cast<int16_t>(scaled);
}

uint16_t q15_to_lra_amp10(uint32_t q15)
{
    q15 = std::min(q15, kQ15One);
    // Keep ProCon2 body rumble conservative. Strong sustained rumble is tiring
    // on real Switch games; pulse/texture detail can still come through.
    constexpr uint32_t kMaxPercent = 70u;
    return static_cast<uint16_t>(
        (q15 * 1023u * kMaxPercent + (kQ15One * 100u / 2u)) /
        (kQ15One * 100u));
}

uint8_t amp10_to_u8(uint16_t amp)
{
    amp = std::min<uint16_t>(amp, 1023u);
    return static_cast<uint8_t>((static_cast<uint32_t>(amp) * 255u + 511u) /
                                1023u);
}

uint16_t texture_to_freq(uint32_t texture, uint16_t base, uint16_t span)
{
    texture = std::min(texture, kQ15One);
    return static_cast<uint16_t>(
        std::min<uint32_t>(0x1ffu,
                           base + ((texture * span + kQ15One / 2u) /
                                   kQ15One)));
}

void pack_wavebird_lra_op(uint8_t out[5], const SwitchRumblePacket &packet)
{
    const uint32_t amp_q15 = switch_rumble_amplitude_q15(packet);
    const uint16_t amp10 = q15_to_lra_amp10(amp_q15);
    const bool active = amp10 != 0u;
    const uint32_t texture = switch_rumble_texture_q15(packet);
    const uint16_t lf_freq = texture_to_freq(texture, 0x0e1u, 0x030u);
    const uint16_t hf_freq = texture_to_freq(texture, 0x180u, 0x050u);
    const uint32_t value =
        (static_cast<uint32_t>(lf_freq) & 0x1ffu) |
        ((static_cast<uint32_t>(amp10) & 0x3ffu) << 10u) |
        ((static_cast<uint32_t>(hf_freq) & 0x1ffu) << 20u) |
        (active ? (1u << 31u) : 0u);

    write_u32_le(out, value);
    out[4] = active ? amp10_to_u8(amp10) : 0u;
}

void pack_bluepad32_motor(uint8_t out[5], const SwitchRumblePacket &packet)
{
    const uint16_t amp10 = q15_to_lra_amp10(switch_rumble_amplitude_q15(packet));
    uint64_t value = 0;
    value |= 0x0e1u;
    value |= (static_cast<uint64_t>(amp10 & 0x3ffu) << 10u);
    value |= (static_cast<uint64_t>(0x1e1u & 0x1ffu) << 20u);
    value |= (static_cast<uint64_t>(amp10 & 0x3ffu) << 30u);
    write_u32_le(out, static_cast<uint32_t>(value));
    out[4] = static_cast<uint8_t>(value >> 32u);
}

void build_bluepad32_block(uint8_t out[16], uint8_t sequence,
                           const uint8_t motor[5])
{
    out[0] = static_cast<uint8_t>(0x50u + (sequence & 0x0fu));
    std::memcpy(&out[1], motor, 5u);
    std::memcpy(&out[6], motor, 5u);
    std::memcpy(&out[11], motor, 5u);
}

} // namespace

bool procon2_is_supported_product(uint16_t product_id)
{
    return product_id == kProcon2ProductId;
}

bool parse_procon2_manufacturer_data(const uint8_t *data, std::size_t size,
                                     Procon2Advertisement &out)
{
    out = {};
    if (data == nullptr || size < 18u) {
        return false;
    }
    if (read_u16_le(&data[0]) != kSwitch2MfgCompanyId) {
        return false;
    }

    const uint16_t vendor_id = read_u16_le(&data[5]);
    const uint16_t product_id = read_u16_le(&data[7]);
    if (vendor_id != kNintendoVendorId ||
        !procon2_is_supported_product(product_id)) {
        return false;
    }

    out.valid = true;
    out.vendor_id = vendor_id;
    out.product_id = product_id;
    std::memcpy(out.reconnect_mac.data(), &data[12], out.reconnect_mac.size());
    out.needs_pair = mac_all_zero(out.reconnect_mac);
    return true;
}

void unpack_procon2_stick12(const uint8_t in[3], uint16_t &x, uint16_t &y)
{
    x = static_cast<uint16_t>(
        static_cast<uint16_t>(in[0]) |
        ((static_cast<uint16_t>(in[1]) & 0x0fu) << 8u));
    y = static_cast<uint16_t>(
        ((static_cast<uint16_t>(in[1]) >> 4u) |
         (static_cast<uint16_t>(in[2]) << 4u)) &
        0x0fffu);
}

uint16_t procon2_centered_to_switch_axis(int16_t centered, bool y_axis)
{
    const int value = y_axis ? (static_cast<int>(kNeutral12) - centered)
                             : (static_cast<int>(kNeutral12) + centered);
    return static_cast<uint16_t>(std::clamp(value, 0, 0x0fff));
}

bool parse_procon2_report05(const uint8_t *report, std::size_t size,
                            const Procon2Calibration &calibration,
                            SwitchProState &out)
{
    if (report == nullptr || size < kProcon2Report05MinSize ||
        report[0] != kProcon2InputReport05) {
        return false;
    }

    SwitchProState parsed{};
    parsed.buttons = procon2_button_bits_to_switch(read_u32_le(&report[4]));

    uint16_t lx = 0;
    uint16_t ly = 0;
    uint16_t rx = 0;
    uint16_t ry = 0;
    unpack_procon2_stick12(&report[10], lx, ly);
    unpack_procon2_stick12(&report[13], rx, ry);

    const int16_t left_x = calibrate_axis(lx, calibration.left.x, false);
    const int16_t left_y = calibrate_axis(ly, calibration.left.y, true);
    const int16_t right_x = calibrate_axis(rx, calibration.right.x, false);
    const int16_t right_y = calibrate_axis(ry, calibration.right.y, true);
    parsed.left_x = procon2_centered_to_switch_axis(left_x, false);
    parsed.left_y = procon2_centered_to_switch_axis(left_y, true);
    parsed.right_x = procon2_centered_to_switch_axis(right_x, false);
    parsed.right_y = procon2_centered_to_switch_axis(right_y, true);

    if (size >= 60u) {
        const int16_t ax = read_i16_le(&report[48]);
        const int16_t ay = read_i16_le(&report[50]);
        const int16_t az = read_i16_le(&report[52]);
        const int16_t gx = read_i16_le(&report[54]);
        const int16_t gy = read_i16_le(&report[56]);
        const int16_t gz = read_i16_le(&report[58]);
        if (ax != 0 || ay != 0 || az != 0 || gx != 0 || gy != 0 || gz != 0) {
            parsed.has_imu = true;
            parsed.accel_x = ay;
            parsed.accel_y = neg_sat(ax);
            parsed.accel_z = az;
            parsed.gyro_x = gy;
            parsed.gyro_y = neg_sat(gx);
            parsed.gyro_z = neg_sat(gz);
        }
    }

    out = parsed;
    return true;
}

std::size_t build_procon2_vibration33(const SwitchRumblePacket &left,
                                      const SwitchRumblePacket &right,
                                      uint8_t sequence,
                                      uint8_t *out,
                                      std::size_t out_size)
{
    if (out == nullptr || out_size < kProcon2Vibration33Size) {
        return 0;
    }

    uint8_t left_motor[5]{};
    uint8_t right_motor[5]{};
    pack_bluepad32_motor(left_motor, left);
    pack_bluepad32_motor(right_motor, right);

    std::memset(out, 0, kProcon2Vibration33Size);
    out[0] = 0x00u;
    // Bluepad32's Pro 2 packet is 0x00 + right block + left block.
    build_bluepad32_block(&out[1], sequence, right_motor);
    build_bluepad32_block(&out[17], sequence, left_motor);
    return kProcon2Vibration33Size;
}

std::size_t build_procon2_vibration42(const SwitchRumblePacket &left,
                                      const SwitchRumblePacket &right,
                                      uint8_t sequence,
                                      uint8_t *out,
                                      std::size_t out_size)
{
    if (out == nullptr || out_size < kProcon2Vibration42Size) {
        return 0;
    }

    uint8_t left_op[5]{};
    uint8_t right_op[5]{};
    pack_wavebird_lra_op(left_op, left);
    pack_wavebird_lra_op(right_op, right);
    const bool left_active = !switch_rumble_packet_is_silent(left);
    const bool right_active = !switch_rumble_packet_is_silent(right);
    const uint8_t tid = static_cast<uint8_t>(sequence & 0x0fu);

    std::memset(out, 0, kProcon2Vibration42Size);
    out[0] = 0x00u;
    out[1] = static_cast<uint8_t>((left_active ? 0x50u : 0x10u) | tid);
    std::memcpy(&out[2], left_op, sizeof(left_op));
    out[17] = static_cast<uint8_t>((right_active ? 0x50u : 0x10u) | tid);
    std::memcpy(&out[18], right_op, sizeof(right_op));
    return kProcon2Vibration42Size;
}

} // namespace switchbridge

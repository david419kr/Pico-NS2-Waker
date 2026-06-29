#include "switch_bridge/switch_pro.h"

#include <algorithm>

namespace switchbridge {

namespace {

void put_i16_le(uint8_t *out, int16_t value)
{
    const auto u = static_cast<uint16_t>(value);
    out[0] = static_cast<uint8_t>(u & 0xffu);
    out[1] = static_cast<uint8_t>((u >> 8) & 0xffu);
}

int16_t read_i16_le(const uint8_t *in)
{
    return static_cast<int16_t>(static_cast<uint16_t>(in[0]) |
                                (static_cast<uint16_t>(in[1]) << 8u));
}

uint16_t unpack_switch_stick12(const uint8_t in[3], bool x_axis)
{
    if (x_axis) {
        return static_cast<uint16_t>(
            static_cast<uint16_t>(in[0]) |
            ((static_cast<uint16_t>(in[1]) & 0x0fu) << 8u));
    }
    return static_cast<uint16_t>(
        ((static_cast<uint16_t>(in[1]) >> 4u) |
         (static_cast<uint16_t>(in[2]) << 4u)) &
        0x0fffu);
}

void add_button(uint32_t &buttons, uint8_t byte, uint8_t mask,
                SwitchButton button)
{
    if ((byte & mask) != 0) {
        buttons |= static_cast<uint32_t>(button);
    }
}

uint32_t parse_switch_pro_button_bytes(const uint8_t in[3])
{
    uint32_t buttons = 0;

    add_button(buttons, in[0], 0x01u, kNsY);
    add_button(buttons, in[0], 0x02u, kNsX);
    add_button(buttons, in[0], 0x04u, kNsB);
    add_button(buttons, in[0], 0x08u, kNsA);
    add_button(buttons, in[0], 0x40u, kNsR);
    add_button(buttons, in[0], 0x80u, kNsZR);

    add_button(buttons, in[1], 0x01u, kNsMinus);
    add_button(buttons, in[1], 0x02u, kNsPlus);
    add_button(buttons, in[1], 0x04u, kNsRightStick);
    add_button(buttons, in[1], 0x08u, kNsLeftStick);
    add_button(buttons, in[1], 0x10u, kNsHome);
    add_button(buttons, in[1], 0x20u, kNsCapture);

    add_button(buttons, in[2], 0x01u, kNsDown);
    add_button(buttons, in[2], 0x02u, kNsUp);
    add_button(buttons, in[2], 0x04u, kNsRight);
    add_button(buttons, in[2], 0x08u, kNsLeft);
    add_button(buttons, in[2], 0x40u, kNsL);
    add_button(buttons, in[2], 0x80u, kNsZL);

    return buttons;
}

} // namespace

void pack_switch_stick12(uint8_t out[3], uint16_t x, uint16_t y)
{
    x = std::min<uint16_t>(x, 0x0fffu);
    y = std::min<uint16_t>(y, 0x0fffu);
    out[0] = static_cast<uint8_t>(x & 0xffu);
    out[1] = static_cast<uint8_t>(((x >> 8) & 0x0fu) | ((y & 0x0fu) << 4));
    out[2] = static_cast<uint8_t>((y >> 4) & 0xffu);
}

void switch_pro_button_bytes(uint32_t buttons, uint8_t out[3])
{
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;

    if ((buttons & kNsY) != 0) {
        out[0] |= 0x01u;
    }
    if ((buttons & kNsX) != 0) {
        out[0] |= 0x02u;
    }
    if ((buttons & kNsB) != 0) {
        out[0] |= 0x04u;
    }
    if ((buttons & kNsA) != 0) {
        out[0] |= 0x08u;
    }
    if ((buttons & kNsR) != 0) {
        out[0] |= 0x40u;
    }
    if ((buttons & kNsZR) != 0) {
        out[0] |= 0x80u;
    }
    if ((buttons & kNsMinus) != 0) {
        out[1] |= 0x01u;
    }
    if ((buttons & kNsPlus) != 0) {
        out[1] |= 0x02u;
    }
    if ((buttons & kNsRightStick) != 0) {
        out[1] |= 0x04u;
    }
    if ((buttons & kNsLeftStick) != 0) {
        out[1] |= 0x08u;
    }
    if ((buttons & kNsHome) != 0) {
        out[1] |= 0x10u;
    }
    if ((buttons & kNsCapture) != 0) {
        out[1] |= 0x20u;
    }
    if ((buttons & kNsDown) != 0) {
        out[2] |= 0x01u;
    }
    if ((buttons & kNsUp) != 0) {
        out[2] |= 0x02u;
    }
    if ((buttons & kNsRight) != 0) {
        out[2] |= 0x04u;
    }
    if ((buttons & kNsLeft) != 0) {
        out[2] |= 0x08u;
    }
    if ((buttons & kNsL) != 0) {
        out[2] |= 0x40u;
    }
    if ((buttons & kNsZL) != 0) {
        out[2] |= 0x80u;
    }
}

void switch2_pro_button_bytes(uint32_t buttons, uint8_t out[3])
{
    // Switch 2 Pro (report 0x09) carries the Nintendo button bitfield verbatim.
    // The kNs* enum is laid out to match the on-wire ordering, so a straight
    // bit copy is correct (mirrors nRF5340 switch2_pro_button_bytes()).
    out[0] = static_cast<uint8_t>(buttons & 0xffu);
    out[1] = static_cast<uint8_t>((buttons >> 8) & 0xffu);
    out[2] = static_cast<uint8_t>((buttons >> 16) & 0x1fu);
}

SwitchProInputReport build_switch_pro_input_report(const SwitchProState &state,
                                                   uint8_t packet_timer,
                                                   bool vibration_enabled)
{
    SwitchProInputReport out{};
    out.fill(0);

    out[0] = kSwitchProFullInputReportId;
    out[1] = packet_timer;
    out[2] = 0x81u;

    uint8_t buttons[3]{};
    switch_pro_button_bytes(state.buttons, buttons);
    out[3] = buttons[0];
    out[4] = buttons[1];
    out[5] = buttons[2];

    pack_switch_stick12(&out[6], state.left_x, state.left_y);
    pack_switch_stick12(&out[9], state.right_x, state.right_y);
    out[12] = vibration_enabled ? 0x38u : 0x30u;

    if (state.has_imu) {
        for (std::size_t offset : {13u, 25u, 37u}) {
            put_i16_le(&out[offset + 0], state.accel_x);
            put_i16_le(&out[offset + 2], state.accel_y);
            put_i16_le(&out[offset + 4], state.accel_z);
            put_i16_le(&out[offset + 6], state.gyro_x);
            put_i16_le(&out[offset + 8], state.gyro_y);
            put_i16_le(&out[offset + 10], state.gyro_z);
        }
    }

    return out;
}

void write_switch_pro_report_buttons(SwitchReport &report, uint32_t buttons)
{
    if (report[0] != kSwitchProFullInputReportId &&
        report[0] != kSwitchProSubcommandReplyReportId) {
        return;
    }

    uint8_t button_bytes[3]{};
    switch_pro_button_bytes(buttons, button_bytes);
    report[3] = button_bytes[0];
    report[4] = button_bytes[1];
    report[5] = button_bytes[2];
}

bool parse_switch_pro_input_report(const uint8_t *report, std::size_t size,
                                   SwitchProState &out)
{
    if (report == nullptr || size < 13u) {
        return false;
    }
    if (report[0] != kSwitchProFullInputReportId &&
        report[0] != kSwitchProSubcommandReplyReportId) {
        return false;
    }

    SwitchProState parsed{};
    parsed.buttons = parse_switch_pro_button_bytes(&report[3]);
    parsed.left_x = unpack_switch_stick12(&report[6], true);
    parsed.left_y = unpack_switch_stick12(&report[6], false);
    parsed.right_x = unpack_switch_stick12(&report[9], true);
    parsed.right_y = unpack_switch_stick12(&report[9], false);

    if (size >= 49u) {
        parsed.has_imu = true;
        parsed.accel_x = read_i16_le(&report[13]);
        parsed.accel_y = read_i16_le(&report[15]);
        parsed.accel_z = read_i16_le(&report[17]);
        parsed.gyro_x = read_i16_le(&report[19]);
        parsed.gyro_y = read_i16_le(&report[21]);
        parsed.gyro_z = read_i16_le(&report[23]);
    }

    out = parsed;
    return true;
}

bool parse_switch_pro_bt_report(const uint8_t *report, std::size_t size,
                                SwitchProState &out)
{
    if (report == nullptr || size == 0) {
        return false;
    }
    if (report[0] == 0xa1u) {
        return parse_switch_pro_input_report(report + 1, size - 1u, out);
    }
    return parse_switch_pro_input_report(report, size, out);
}

} // namespace switchbridge

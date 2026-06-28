#include "ds5onswitch/switch_pro.h"

#include <algorithm>

namespace ds5onswitch {

namespace {

void put_i16_le(uint8_t *out, int16_t value)
{
    const auto u = static_cast<uint16_t>(value);
    out[0] = static_cast<uint8_t>(u & 0xffu);
    out[1] = static_cast<uint8_t>((u >> 8) & 0xffu);
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

} // namespace ds5onswitch

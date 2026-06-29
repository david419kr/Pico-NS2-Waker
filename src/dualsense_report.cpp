#include "switch_bridge/dualsense_report.h"

namespace switchbridge {

namespace {

int16_t read_le_i16(const uint8_t *data)
{
    return static_cast<int16_t>(static_cast<uint16_t>(data[0]) |
                                (static_cast<uint16_t>(data[1]) << 8u));
}

void add_button(uint32_t &buttons, uint8_t byte, uint8_t mask,
                PlayStationButton button)
{
    if ((byte & mask) != 0) {
        buttons |= static_cast<uint32_t>(button);
    }
}

PlayStationDpad parse_dpad(uint8_t value)
{
    value &= 0x0fu;
    if (value <= static_cast<uint8_t>(PlayStationDpad::Neutral)) {
        return static_cast<PlayStationDpad>(value);
    }
    return PlayStationDpad::Neutral;
}

} // namespace

bool parse_dualsense_bt31_payload(const uint8_t *payload, std::size_t size,
                                  PlayStationInput &out)
{
    if (payload == nullptr || size < kDualSenseBt31PayloadSize) {
        return false;
    }

    PlayStationInput parsed{};
    parsed.left_x = payload[0];
    parsed.left_y = payload[1];
    parsed.right_x = payload[2];
    parsed.right_y = payload[3];
    parsed.l2 = payload[4];
    parsed.r2 = payload[5];
    parsed.dpad = parse_dpad(payload[7]);

    add_button(parsed.buttons, payload[7], 0x10, kPsSquare);
    add_button(parsed.buttons, payload[7], 0x20, kPsCross);
    add_button(parsed.buttons, payload[7], 0x40, kPsCircle);
    add_button(parsed.buttons, payload[7], 0x80, kPsTriangle);

    add_button(parsed.buttons, payload[8], 0x01, kPsL1);
    add_button(parsed.buttons, payload[8], 0x02, kPsR1);
    add_button(parsed.buttons, payload[8], 0x04, kPsL2Button);
    add_button(parsed.buttons, payload[8], 0x08, kPsR2Button);
    add_button(parsed.buttons, payload[8], 0x10, kPsCreate);
    add_button(parsed.buttons, payload[8], 0x20, kPsOptions);
    add_button(parsed.buttons, payload[8], 0x40, kPsL3);
    add_button(parsed.buttons, payload[8], 0x80, kPsR3);

    add_button(parsed.buttons, payload[9], 0x01, kPsButton);
    add_button(parsed.buttons, payload[9], 0x02, kPsTouchpadClick);
    add_button(parsed.buttons, payload[9], 0x04, kPsMute);

    parsed.gyro_x = read_le_i16(payload + 15);
    parsed.gyro_z = read_le_i16(payload + 17);
    parsed.gyro_y = read_le_i16(payload + 19);
    parsed.accel_x = read_le_i16(payload + 21);
    parsed.accel_y = read_le_i16(payload + 23);
    parsed.accel_z = read_le_i16(payload + 25);
    parsed.has_imu = true;

    parsed.headphones_connected = (payload[53] & 0x01u) != 0;
    parsed.mic_connected = (payload[53] & 0x02u) != 0;
    parsed.mic_muted = (payload[53] & 0x04u) != 0;
    parsed.external_mic_active = (payload[54] & 0x01u) != 0;

    out = parsed;
    return true;
}

bool parse_dualsense_bt_report(const uint8_t *report, std::size_t size,
                               PlayStationInput &out)
{
    if (report == nullptr) {
        return false;
    }

    if (size >= kDualSenseBt31PayloadSize + 3u && report[0] == 0xa1 &&
        report[1] == 0x31) {
        return parse_dualsense_bt31_payload(report + 3, size - 3u, out);
    }

    if (size >= kDualSenseBt31PayloadSize + 1u && report[0] == 0x31) {
        return parse_dualsense_bt31_payload(report + 1, size - 1u, out);
    }

    return false;
}

} // namespace switchbridge

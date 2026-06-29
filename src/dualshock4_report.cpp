#include "switch_bridge/dualshock4_report.h"

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

void parse_common_buttons(PlayStationInput &parsed, const uint8_t *buttons)
{
    parsed.dpad = parse_dpad(buttons[0]);

    add_button(parsed.buttons, buttons[0], 0x10, kPsSquare);
    add_button(parsed.buttons, buttons[0], 0x20, kPsCross);
    add_button(parsed.buttons, buttons[0], 0x40, kPsCircle);
    add_button(parsed.buttons, buttons[0], 0x80, kPsTriangle);

    add_button(parsed.buttons, buttons[1], 0x01, kPsL1);
    add_button(parsed.buttons, buttons[1], 0x02, kPsR1);
    add_button(parsed.buttons, buttons[1], 0x04, kPsL2Button);
    add_button(parsed.buttons, buttons[1], 0x08, kPsR2Button);
    add_button(parsed.buttons, buttons[1], 0x10, kPsCreate);
    add_button(parsed.buttons, buttons[1], 0x20, kPsOptions);
    add_button(parsed.buttons, buttons[1], 0x40, kPsL3);
    add_button(parsed.buttons, buttons[1], 0x80, kPsR3);

    add_button(parsed.buttons, buttons[2], 0x01, kPsButton);
    add_button(parsed.buttons, buttons[2], 0x02, kPsTouchpadClick);
}

bool parse_full_report(const uint8_t *wire, std::size_t size,
                       PlayStationInput &out)
{
    if (size < kDualShock4Bt11ReportSize || wire[0] != 0x11u) {
        return false;
    }

    PlayStationInput parsed{};
    parsed.left_x = wire[3];
    parsed.left_y = wire[4];
    parsed.right_x = wire[5];
    parsed.right_y = wire[6];
    parse_common_buttons(parsed, wire + 7);
    parsed.l2 = wire[10];
    parsed.r2 = wire[11];

    parsed.gyro_x = read_le_i16(wire + 15);
    parsed.gyro_y = read_le_i16(wire + 17);
    parsed.gyro_z = read_le_i16(wire + 19);
    parsed.accel_x = read_le_i16(wire + 21);
    parsed.accel_y = read_le_i16(wire + 23);
    parsed.accel_z = read_le_i16(wire + 25);
    parsed.has_imu = true;

    out = parsed;
    return true;
}

bool parse_minimal_report(const uint8_t *wire, std::size_t size,
                           PlayStationInput &out)
{
    if (size < kDualShock4BtMinimalReportSize || wire[0] != 0x01u) {
        return false;
    }

    PlayStationInput parsed{};
    parsed.left_x = wire[1];
    parsed.left_y = wire[2];
    parsed.right_x = wire[3];
    parsed.right_y = wire[4];
    parse_common_buttons(parsed, wire + 5);
    parsed.l2 = wire[8];
    parsed.r2 = wire[9];
    parsed.has_imu = false;

    out = parsed;
    return true;
}

bool parse_usb_shaped_report(const uint8_t *wire, std::size_t size,
                             PlayStationInput &out)
{
    if (size < 25u || wire[0] != 0x01u) {
        return false;
    }

    PlayStationInput parsed{};
    parsed.left_x = wire[1];
    parsed.left_y = wire[2];
    parsed.right_x = wire[3];
    parsed.right_y = wire[4];
    parse_common_buttons(parsed, wire + 5);
    parsed.l2 = wire[8];
    parsed.r2 = wire[9];

    parsed.gyro_x = read_le_i16(wire + 13);
    parsed.gyro_y = read_le_i16(wire + 15);
    parsed.gyro_z = read_le_i16(wire + 17);
    parsed.accel_x = read_le_i16(wire + 19);
    parsed.accel_y = read_le_i16(wire + 21);
    parsed.accel_z = read_le_i16(wire + 23);
    parsed.has_imu = true;

    out = parsed;
    return true;
}

} // namespace

bool parse_dualshock4_bt_report(const uint8_t *report, std::size_t size,
                                PlayStationInput &out)
{
    if (report == nullptr || size == 0) {
        return false;
    }

    const uint8_t *wire = report;
    std::size_t wire_size = size;
    if (report[0] == 0xa1u) {
        if (size < 2u) {
            return false;
        }
        wire = report + 1u;
        wire_size = size - 1u;
    }

    if (parse_full_report(wire, wire_size, out)) {
        return true;
    }
    if (parse_usb_shaped_report(wire, wire_size, out)) {
        return true;
    }
    return parse_minimal_report(wire, wire_size, out);
}

} // namespace switchbridge

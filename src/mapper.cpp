#include "switch_bridge/mapper.h"

#include <algorithm>
#include <cstdint>

namespace switchbridge {

namespace {

constexpr int32_t kSwitchAccelCountsPerG = 4096;
constexpr int32_t kPlayStationAccelCountsPerG = 8192;
constexpr int32_t kSwitchGyroCountsPerDpsMilli = 14247;
constexpr int32_t kPlayStationGyroCountsPerDps = 1024;
constexpr uint32_t kSwitchStickAxisMax = 0x0fffu;

constexpr int16_t clamp_i16(int32_t value)
{
    return static_cast<int16_t>(
        std::clamp(value, static_cast<int32_t>(INT16_MIN),
                   static_cast<int32_t>(INT16_MAX)));
}

constexpr bool pressed(uint32_t buttons, PlayStationButton button)
{
    return (buttons & static_cast<uint32_t>(button)) != 0;
}

constexpr int32_t div_round_nearest(int32_t numerator, int32_t denominator)
{
    return numerator >= 0 ? (numerator + denominator / 2) / denominator
                          : (numerator - denominator / 2) / denominator;
}

void add_dpad(uint32_t &buttons, PlayStationDpad dpad)
{
    switch (dpad) {
    case PlayStationDpad::Up:
        buttons |= kNsUp;
        break;
    case PlayStationDpad::UpRight:
        buttons |= kNsUp | kNsRight;
        break;
    case PlayStationDpad::Right:
        buttons |= kNsRight;
        break;
    case PlayStationDpad::DownRight:
        buttons |= kNsDown | kNsRight;
        break;
    case PlayStationDpad::Down:
        buttons |= kNsDown;
        break;
    case PlayStationDpad::DownLeft:
        buttons |= kNsDown | kNsLeft;
        break;
    case PlayStationDpad::Left:
        buttons |= kNsLeft;
        break;
    case PlayStationDpad::UpLeft:
        buttons |= kNsUp | kNsLeft;
        break;
    case PlayStationDpad::Neutral:
        break;
    }
}

} // namespace

uint16_t axis8_to_switch12(uint8_t value)
{
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(value) * kSwitchStickAxisMax + 127u) / 255u);
}

int16_t playstation_accel_to_switch(int16_t value)
{
    const int32_t scaled =
        (static_cast<int32_t>(value) * kSwitchAccelCountsPerG) /
        kPlayStationAccelCountsPerG;
    return clamp_i16(scaled);
}

int16_t playstation_gyro_to_switch(int16_t value)
{
    const int32_t scaled = div_round_nearest(
        static_cast<int32_t>(value) * kSwitchGyroCountsPerDpsMilli,
        kPlayStationGyroCountsPerDps * 1000);
    return clamp_i16(scaled);
}

SwitchProState map_playstation_to_switch(const PlayStationInput &input,
                                        MapperConfig config)
{
    SwitchProState out{};

    out.left_x = axis8_to_switch12(input.left_x);
    out.left_y = static_cast<uint16_t>(
        kSwitchStickAxisMax - axis8_to_switch12(input.left_y));
    out.right_x = axis8_to_switch12(input.right_x);
    out.right_y = static_cast<uint16_t>(
        kSwitchStickAxisMax - axis8_to_switch12(input.right_y));

    if (pressed(input.buttons, kPsCross)) {
        out.buttons |= kNsB;
    }
    if (pressed(input.buttons, kPsCircle)) {
        out.buttons |= kNsA;
    }
    if (pressed(input.buttons, kPsSquare)) {
        out.buttons |= kNsY;
    }
    if (pressed(input.buttons, kPsTriangle)) {
        out.buttons |= kNsX;
    }
    if (pressed(input.buttons, kPsL1)) {
        out.buttons |= kNsL;
    }
    if (pressed(input.buttons, kPsR1)) {
        out.buttons |= kNsR;
    }
    if (pressed(input.buttons, kPsL2Button) ||
        input.l2 >= config.trigger_button_threshold) {
        out.buttons |= kNsZL;
    }
    if (pressed(input.buttons, kPsR2Button) ||
        input.r2 >= config.trigger_button_threshold) {
        out.buttons |= kNsZR;
    }
    if (pressed(input.buttons, kPsCreate)) {
        out.buttons |= kNsMinus;
    }
    if (pressed(input.buttons, kPsOptions)) {
        out.buttons |= kNsPlus;
    }
    if (pressed(input.buttons, kPsL3)) {
        out.buttons |= kNsLeftStick;
    }
    if (pressed(input.buttons, kPsR3)) {
        out.buttons |= kNsRightStick;
    }
    if (pressed(input.buttons, kPsButton)) {
        out.buttons |= kNsHome;
    }
    if (pressed(input.buttons, kPsTouchpadClick)) {
        out.buttons |= kNsCapture;
    }
    add_dpad(out.buttons, input.dpad);

    if (input.has_imu) {
        out.has_imu = true;
        if (config.use_nrf5340_switch_motion_axes) {
            out.accel_x = playstation_accel_to_switch(clamp_i16(-input.accel_z));
            out.accel_y = playstation_accel_to_switch(clamp_i16(-input.accel_x));
            out.accel_z = playstation_accel_to_switch(input.accel_y);
            out.gyro_x = playstation_gyro_to_switch(clamp_i16(-input.gyro_y));
            out.gyro_y = playstation_gyro_to_switch(clamp_i16(-input.gyro_x));
            out.gyro_z = playstation_gyro_to_switch(input.gyro_z);
        } else {
            out.accel_x = playstation_accel_to_switch(input.accel_x);
            out.accel_y = playstation_accel_to_switch(input.accel_y);
            out.accel_z = playstation_accel_to_switch(input.accel_z);
            out.gyro_x = playstation_gyro_to_switch(input.gyro_x);
            out.gyro_y = playstation_gyro_to_switch(input.gyro_y);
            out.gyro_z = playstation_gyro_to_switch(input.gyro_z);
        }
    }

    return out;
}

} // namespace switchbridge

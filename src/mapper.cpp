#include "ds5onswitch/mapper.h"

#include <algorithm>
#include <cstdint>

namespace ds5onswitch {

namespace {

constexpr int32_t kSwitchAccelCountsPerG = 4096;
constexpr int32_t kDualSenseAccelCountsPerG = 8192;
constexpr uint32_t kSwitchStickAxisMax = 0x0fffu;

constexpr int16_t clamp_i16(int32_t value)
{
    return static_cast<int16_t>(
        std::clamp(value, static_cast<int32_t>(INT16_MIN),
                   static_cast<int32_t>(INT16_MAX)));
}

constexpr bool pressed(uint32_t buttons, DualSenseButton button)
{
    return (buttons & static_cast<uint32_t>(button)) != 0;
}

void add_dpad(uint32_t &buttons, DualSenseDpad dpad)
{
    switch (dpad) {
    case DualSenseDpad::Up:
        buttons |= kNsUp;
        break;
    case DualSenseDpad::UpRight:
        buttons |= kNsUp | kNsRight;
        break;
    case DualSenseDpad::Right:
        buttons |= kNsRight;
        break;
    case DualSenseDpad::DownRight:
        buttons |= kNsDown | kNsRight;
        break;
    case DualSenseDpad::Down:
        buttons |= kNsDown;
        break;
    case DualSenseDpad::DownLeft:
        buttons |= kNsDown | kNsLeft;
        break;
    case DualSenseDpad::Left:
        buttons |= kNsLeft;
        break;
    case DualSenseDpad::UpLeft:
        buttons |= kNsUp | kNsLeft;
        break;
    case DualSenseDpad::Neutral:
        break;
    }
}

} // namespace

uint16_t axis8_to_switch12(uint8_t value)
{
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(value) * kSwitchStickAxisMax + 127u) / 255u);
}

int16_t dualsense_accel_to_switch(int16_t value)
{
    const int32_t scaled =
        (static_cast<int32_t>(value) * kSwitchAccelCountsPerG) /
        kDualSenseAccelCountsPerG;
    return clamp_i16(scaled);
}

int16_t dualsense_gyro_to_switch(int16_t value)
{
    return value;
}

SwitchProState map_dualsense_to_switch(const DualSenseInput &input,
                                        MapperConfig config)
{
    SwitchProState out{};

    out.left_x = axis8_to_switch12(input.left_x);
    out.left_y = static_cast<uint16_t>(
        kSwitchStickAxisMax - axis8_to_switch12(input.left_y));
    out.right_x = axis8_to_switch12(input.right_x);
    out.right_y = static_cast<uint16_t>(
        kSwitchStickAxisMax - axis8_to_switch12(input.right_y));

    if (pressed(input.buttons, kDs5Cross)) {
        out.buttons |= kNsB;
    }
    if (pressed(input.buttons, kDs5Circle)) {
        out.buttons |= kNsA;
    }
    if (pressed(input.buttons, kDs5Square)) {
        out.buttons |= kNsY;
    }
    if (pressed(input.buttons, kDs5Triangle)) {
        out.buttons |= kNsX;
    }
    if (pressed(input.buttons, kDs5L1)) {
        out.buttons |= kNsL;
    }
    if (pressed(input.buttons, kDs5R1)) {
        out.buttons |= kNsR;
    }
    if (pressed(input.buttons, kDs5L2Button) ||
        input.l2 >= config.trigger_button_threshold) {
        out.buttons |= kNsZL;
    }
    if (pressed(input.buttons, kDs5R2Button) ||
        input.r2 >= config.trigger_button_threshold) {
        out.buttons |= kNsZR;
    }
    if (pressed(input.buttons, kDs5Create)) {
        out.buttons |= kNsMinus;
    }
    if (pressed(input.buttons, kDs5Options)) {
        out.buttons |= kNsPlus;
    }
    if (pressed(input.buttons, kDs5L3)) {
        out.buttons |= kNsLeftStick;
    }
    if (pressed(input.buttons, kDs5R3)) {
        out.buttons |= kNsRightStick;
    }
    if (pressed(input.buttons, kDs5Ps)) {
        out.buttons |= kNsHome;
    }
    if (pressed(input.buttons, kDs5TouchpadClick)) {
        out.buttons |= kNsCapture;
    }
    add_dpad(out.buttons, input.dpad);

    if (input.has_imu) {
        out.has_imu = true;
        if (config.use_nrf5340_switch_motion_axes) {
            out.accel_x = dualsense_accel_to_switch(input.accel_y);
            out.accel_y = dualsense_accel_to_switch(clamp_i16(-input.accel_x));
            out.accel_z = dualsense_accel_to_switch(input.accel_z);
            out.gyro_x = dualsense_gyro_to_switch(input.gyro_y);
            out.gyro_y = dualsense_gyro_to_switch(clamp_i16(-input.gyro_x));
            out.gyro_z = dualsense_gyro_to_switch(input.gyro_z);
        } else {
            out.accel_x = dualsense_accel_to_switch(input.accel_x);
            out.accel_y = dualsense_accel_to_switch(input.accel_y);
            out.accel_z = dualsense_accel_to_switch(input.accel_z);
            out.gyro_x = dualsense_gyro_to_switch(input.gyro_x);
            out.gyro_y = dualsense_gyro_to_switch(input.gyro_y);
            out.gyro_z = dualsense_gyro_to_switch(input.gyro_z);
        }
    }

    return out;
}

} // namespace ds5onswitch

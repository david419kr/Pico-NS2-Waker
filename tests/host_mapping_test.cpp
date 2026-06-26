#include "ds5onswitch/haptics.h"
#include "ds5onswitch/mapper.h"
#include "ds5onswitch/switch_pro.h"

#include <cassert>
#include <cstdint>

using namespace ds5onswitch;

namespace {

int16_t read_i16_le(const uint8_t *p)
{
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) |
                                (static_cast<uint16_t>(p[1]) << 8u));
}

void test_button_mapping()
{
    DualSenseInput in{};
    in.buttons = kDs5Cross | kDs5Circle | kDs5Square | kDs5Triangle |
                 kDs5L1 | kDs5R1 | kDs5Create | kDs5Options |
                 kDs5L3 | kDs5R3 | kDs5Ps | kDs5TouchpadClick;
    in.l2 = 64;
    in.r2 = 255;
    in.dpad = DualSenseDpad::UpRight;

    const SwitchProState state = map_dualsense_to_switch(in);
    assert((state.buttons & kNsB) != 0);
    assert((state.buttons & kNsA) != 0);
    assert((state.buttons & kNsY) != 0);
    assert((state.buttons & kNsX) != 0);
    assert((state.buttons & kNsL) != 0);
    assert((state.buttons & kNsR) != 0);
    assert((state.buttons & kNsZL) != 0);
    assert((state.buttons & kNsZR) != 0);
    assert((state.buttons & kNsMinus) != 0);
    assert((state.buttons & kNsPlus) != 0);
    assert((state.buttons & kNsLeftStick) != 0);
    assert((state.buttons & kNsRightStick) != 0);
    assert((state.buttons & kNsHome) != 0);
    assert((state.buttons & kNsCapture) != 0);
    assert((state.buttons & kNsUp) != 0);
    assert((state.buttons & kNsRight) != 0);

    const SwitchProInputReport report = build_switch_pro_input_report(state, 7, true);
    assert(report[0] == 0x30);
    assert(report[1] == 7);
    assert(report[2] == 0x81);
    assert((report[3] & 0x0f) == 0x0f);
    assert((report[3] & 0xc0) == 0xc0);
    assert((report[4] & 0x3f) == 0x3f);
    assert((report[5] & 0xc6) == 0xc6);
    assert(report[12] == 0x38);
}

void test_axis_and_motion_mapping()
{
    assert(axis8_to_switch12(0) == 0);
    assert(axis8_to_switch12(255) == 0x0fff);
    assert(axis8_to_switch12(128) >= 0x800);

    DualSenseInput in{};
    in.left_x = 0;
    in.left_y = 255;
    in.right_x = 128;
    in.right_y = 128;
    in.has_imu = true;
    in.accel_x = 8192;
    in.accel_y = -4096;
    in.accel_z = 8192;
    in.gyro_x = 1024;
    in.gyro_y = -2048;
    in.gyro_z = 512;

    const SwitchProState state = map_dualsense_to_switch(in);
    assert(state.left_x == 0);
    assert(state.left_y == 0x0fff);
    assert(state.has_imu);
    assert(state.accel_x == dualsense_accel_to_switch(in.accel_y));
    assert(state.accel_y == dualsense_accel_to_switch(-in.accel_x));
    assert(state.accel_z == dualsense_accel_to_switch(in.accel_z));
    assert(state.gyro_x == dualsense_gyro_to_switch(in.gyro_y));
    assert(state.gyro_y == dualsense_gyro_to_switch(-in.gyro_x));
    assert(state.gyro_z == dualsense_gyro_to_switch(in.gyro_z));

    const SwitchProInputReport report = build_switch_pro_input_report(state, 1, false);
    assert(read_i16_le(&report[13]) == state.accel_x);
    assert(read_i16_le(&report[15]) == state.accel_y);
    assert(read_i16_le(&report[17]) == state.accel_z);
    assert(read_i16_le(&report[19]) == state.gyro_x);
    assert(read_i16_le(&report[21]) == state.gyro_y);
    assert(read_i16_le(&report[23]) == state.gyro_z);
    assert(report[12] == 0x30);
}

void test_haptics()
{
    SwitchRumbleTranslator translator;
    const SwitchRumblePacket silent{0x00, 0x01, 0x40, 0x40};
    const SwitchRumblePacket impact{0x34, 0xf0, 0x62, 0xe0};

    DualSenseHapticCommand cmd = translator.translate(silent, silent);
    assert(!cmd.active);

    cmd = translator.translate(impact, silent);
    assert(cmd.active);
    assert(cmd.left_pulse > 0);

    cmd = translator.translate(silent, silent);
    assert(!cmd.active);
}

} // namespace

int main()
{
    test_button_mapping();
    test_axis_and_motion_mapping();
    test_haptics();
    return 0;
}

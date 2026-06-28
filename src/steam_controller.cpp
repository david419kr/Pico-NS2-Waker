#include "ds5onswitch/steam_controller.h"

#include <cstring>

namespace ds5onswitch {

namespace {

constexpr int16_t kSwitchAccelCountsPerG = 4096;
constexpr int16_t kSwitchAccelDeadzoneCounts = 96;
constexpr int32_t kSteamToSwitchAccelDivisor = 4;
constexpr int32_t kSteamTrackpadDpadDeadzone = 9000;
constexpr std::size_t kSteamFeatureCommandSize = 64;

uint16_t read_u16_le(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8u);
}

int16_t read_i16_le(const uint8_t *p)
{
    return static_cast<int16_t>(read_u16_le(p));
}

uint32_t read_u32_le(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

void write_u16_le(uint8_t *p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8u);
}

int16_t neg_i16(int16_t value)
{
    return value == INT16_MIN ? INT16_MAX : static_cast<int16_t>(-value);
}

int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

enum class SteamDpadDir : uint8_t {
    None,
    N,
    NE,
    E,
    SE,
    S,
    SW,
    W,
    NW,
};

int16_t switch_accel_deadzone(int16_t value)
{
    if (abs_i32(value) <= kSwitchAccelDeadzoneCounts) {
        return 0;
    }
    if (abs_i32(static_cast<int32_t>(value) - kSwitchAccelCountsPerG) <=
        kSwitchAccelDeadzoneCounts) {
        return kSwitchAccelCountsPerG;
    }
    if (abs_i32(static_cast<int32_t>(value) + kSwitchAccelCountsPerG) <=
        kSwitchAccelDeadzoneCounts) {
        return -kSwitchAccelCountsPerG;
    }
    return value;
}

void map_dpad(uint32_t steam_buttons, uint32_t &switch_buttons)
{
    if (steam_buttons & kSteamBtnDpadUp) {
        switch_buttons |= kNsUp;
    }
    if (steam_buttons & kSteamBtnDpadRight) {
        switch_buttons |= kNsRight;
    }
    if (steam_buttons & kSteamBtnDpadDown) {
        switch_buttons |= kNsDown;
    }
    if (steam_buttons & kSteamBtnDpadLeft) {
        switch_buttons |= kNsLeft;
    }
}

SteamDpadDir trackpad_dpad(int16_t x, int16_t y, bool active)
{
    const int32_t ax = abs_i32(x);
    const int32_t ay = abs_i32(y);

    if (!active ||
        (ax < kSteamTrackpadDpadDeadzone &&
         ay < kSteamTrackpadDpadDeadzone)) {
        return SteamDpadDir::None;
    }

    const bool diagonal = ax > 0 && ay > 0 &&
                          ax * 100 >= ay * 84 &&
                          ay * 100 >= ax * 84;
    if (diagonal) {
        if (x >= 0 && y >= 0) {
            return SteamDpadDir::NE;
        }
        if (x >= 0 && y < 0) {
            return SteamDpadDir::SE;
        }
        if (x < 0 && y < 0) {
            return SteamDpadDir::SW;
        }
        return SteamDpadDir::NW;
    }

    if (ax >= ay) {
        return x >= 0 ? SteamDpadDir::E : SteamDpadDir::W;
    }
    return y >= 0 ? SteamDpadDir::N : SteamDpadDir::S;
}

void map_dpad_dir(SteamDpadDir dir, uint32_t &switch_buttons)
{
    switch (dir) {
    case SteamDpadDir::N:
        switch_buttons |= kNsUp;
        break;
    case SteamDpadDir::NE:
        switch_buttons |= kNsUp | kNsRight;
        break;
    case SteamDpadDir::E:
        switch_buttons |= kNsRight;
        break;
    case SteamDpadDir::SE:
        switch_buttons |= kNsDown | kNsRight;
        break;
    case SteamDpadDir::S:
        switch_buttons |= kNsDown;
        break;
    case SteamDpadDir::SW:
        switch_buttons |= kNsDown | kNsLeft;
        break;
    case SteamDpadDir::W:
        switch_buttons |= kNsLeft;
        break;
    case SteamDpadDir::NW:
        switch_buttons |= kNsUp | kNsLeft;
        break;
    case SteamDpadDir::None:
        break;
    }
}

void map_left_trackpad_dpad(const SteamControllerInput &input,
                            uint32_t &switch_buttons)
{
    const SteamDpadDir dir = trackpad_dpad(
        input.left_pad_x, input.left_pad_y,
        (input.buttons & kSteamBtnLeftPadClick) != 0u);
    if (dir == SteamDpadDir::None) {
        return;
    }

    switch_buttons &= ~(kNsUp | kNsRight | kNsDown | kNsLeft);
    map_dpad_dir(dir, switch_buttons);
}

uint16_t motor_to_u16(uint8_t value)
{
    return static_cast<uint16_t>(value) * 257u;
}

} // namespace

bool parse_steam_controller_report(const uint8_t *data, std::size_t size,
                                   SteamControllerInput &out)
{
    if (data == nullptr) {
        return false;
    }

    const uint8_t *wire = data;
    std::size_t wire_size = size;
    if (size >= kSteamControllerStateReportSize + 1u && data[0] == 0xa1u) {
        wire = data + 1u;
        wire_size = size - 1u;
    }

    if (wire_size < kSteamControllerStateReportSize ||
        (wire[0] != kSteamControllerReportState &&
         wire[0] != kSteamControllerReportStateLegacy)) {
        return false;
    }

    SteamControllerInput parsed{};
    parsed.sequence = wire[1];
    parsed.buttons = read_u32_le(&wire[2]);
    parsed.left_trigger = read_u16_le(&wire[6]);
    parsed.right_trigger = read_u16_le(&wire[8]);
    parsed.left_stick_x = read_i16_le(&wire[10]);
    parsed.left_stick_y = read_i16_le(&wire[12]);
    parsed.right_stick_x = read_i16_le(&wire[14]);
    parsed.right_stick_y = read_i16_le(&wire[16]);
    parsed.left_pad_x = read_i16_le(&wire[18]);
    parsed.left_pad_y = read_i16_le(&wire[20]);
    parsed.left_pad_force = read_i16_le(&wire[22]);
    parsed.right_pad_x = read_i16_le(&wire[24]);
    parsed.right_pad_y = read_i16_le(&wire[26]);
    parsed.right_pad_force = read_i16_le(&wire[28]);
    parsed.imu_timestamp = read_u32_le(&wire[30]);
    parsed.accel_x = read_i16_le(&wire[34]);
    parsed.accel_y = read_i16_le(&wire[36]);
    parsed.accel_z = read_i16_le(&wire[38]);
    parsed.gyro_x = read_i16_le(&wire[40]);
    parsed.gyro_y = read_i16_le(&wire[42]);
    parsed.gyro_z = read_i16_le(&wire[44]);
    parsed.has_imu = parsed.imu_timestamp != 0u || parsed.accel_x != 0 ||
                     parsed.accel_y != 0 || parsed.accel_z != 0 ||
                     parsed.gyro_x != 0 || parsed.gyro_y != 0 ||
                     parsed.gyro_z != 0;

    out = parsed;
    return true;
}

uint8_t steam_trigger_to_u8(uint16_t raw)
{
    const uint32_t value = raw >> 7u;
    return value > 255u ? 255u : static_cast<uint8_t>(value);
}

uint16_t steam_axis_to_switch12(int16_t raw, bool invert)
{
    int32_t value = raw;
    if (invert) {
        value = value == INT16_MIN ? INT16_MAX : -value;
    }

    value = static_cast<int32_t>(
        (static_cast<int64_t>(value + 32768) * 4096) / 65536);
    if (value < 0) {
        return 0;
    }
    if (value > 0x0fff) {
        return 0x0fff;
    }
    return static_cast<uint16_t>(value);
}

int16_t steam_accel_to_switch(int16_t raw)
{
    int32_t magnitude = raw < 0 ? -static_cast<int32_t>(raw) : raw;
    magnitude = (magnitude + (kSteamToSwitchAccelDivisor / 2)) /
                kSteamToSwitchAccelDivisor;
    const int32_t scaled = raw < 0 ? -magnitude : magnitude;
    return switch_accel_deadzone(static_cast<int16_t>(scaled));
}

SwitchProState map_steam_to_switch(const SteamControllerInput &input)
{
    SwitchProState state{};
    uint32_t buttons = 0;

    map_dpad(input.buttons, buttons);
    map_left_trackpad_dpad(input, buttons);

    if (input.buttons & kSteamBtnA) {
        buttons |= kNsB;
    }
    if (input.buttons & kSteamBtnB) {
        buttons |= kNsA;
    }
    if (input.buttons & kSteamBtnX) {
        buttons |= kNsY;
    }
    if (input.buttons & kSteamBtnY) {
        buttons |= kNsX;
    }
    if (input.buttons & kSteamBtnLeftBumper) {
        buttons |= kNsL;
    }
    if (input.buttons & kSteamBtnRightBumper) {
        buttons |= kNsR;
    }
    if (input.buttons & kSteamBtnView) {
        buttons |= kNsMinus;
    }
    if (input.buttons & kSteamBtnMenu) {
        buttons |= kNsPlus;
    }
    if (input.buttons & kSteamBtnLeftStick) {
        buttons |= kNsLeftStick;
    }
    if (input.buttons & kSteamBtnRightStick) {
        buttons |= kNsRightStick;
    }
    if (input.buttons & kSteamBtnSteam) {
        buttons |= kNsHome;
    }
    if (steam_trigger_to_u8(input.left_trigger) > 0u) {
        buttons |= kNsZL;
    }
    if (steam_trigger_to_u8(input.right_trigger) > 0u) {
        buttons |= kNsZR;
    }
    if (input.buttons & kSteamBtnRightPadClick) {
        buttons |= kNsCapture;
    }

    state.buttons = buttons;
    state.left_x = steam_axis_to_switch12(input.left_stick_x);
    state.left_y = steam_axis_to_switch12(input.left_stick_y);
    state.right_x = steam_axis_to_switch12(input.right_stick_x);
    state.right_y = steam_axis_to_switch12(input.right_stick_y);

    if (input.has_imu) {
        state.has_imu = true;
        state.accel_x = steam_accel_to_switch(input.accel_y);
        state.accel_y = steam_accel_to_switch(neg_i16(input.accel_x));
        state.accel_z = steam_accel_to_switch(input.accel_z);
        state.gyro_x = input.gyro_y;
        state.gyro_y = neg_i16(input.gyro_x);
        state.gyro_z = input.gyro_z;
    }

    return state;
}

std::size_t build_steam_feature_command(uint8_t command, const uint8_t *payload,
                                        uint8_t payload_size, uint8_t *out,
                                        std::size_t out_size)
{
    if (out == nullptr || out_size < kSteamFeatureCommandSize) {
        return 0;
    }
    if (payload_size != 0u && payload == nullptr) {
        return 0;
    }

    std::memset(out, 0, kSteamFeatureCommandSize);
    out[0] = kSteamFeatureReportCommand;
    out[1] = command;
    out[2] = payload_size;
    if (payload_size != 0u) {
        std::memcpy(out + 3u, payload, payload_size);
    }
    return kSteamFeatureCommandSize;
}

std::size_t build_steam_settings_feature_command(uint8_t *out,
                                                 std::size_t out_size)
{
    const uint8_t payload[] = {
        kSteamSettingImuMode,
        static_cast<uint8_t>(kSteamImuModeRawAccelGyro & 0xffu),
        static_cast<uint8_t>(kSteamImuModeRawAccelGyro >> 8u),
        kSteamSettingLeftTrackpadMode,
        kSteamTrackpadModeNone,
        0x00,
        kSteamSettingRightTrackpadMode,
        kSteamTrackpadModeNone,
        0x00,
    };
    return build_steam_feature_command(kSteamCmdSetSettings, payload,
                                       static_cast<uint8_t>(sizeof(payload)),
                                       out, out_size);
}

std::size_t build_steam_body_rumble_output(const DualSenseHapticCommand &haptics,
                                           uint8_t *out,
                                           std::size_t out_size)
{
    if (out == nullptr || out_size < 10u) {
        return 0;
    }

    std::memset(out, 0, 10u);
    out[0] = kSteamOutHapticRumble;
    out[1] = 0x00;
    write_u16_le(out + 2u, 0x0000);
    write_u16_le(out + 4u, motor_to_u16(haptics.left_motor));
    out[6] = 0x00;
    write_u16_le(out + 7u, motor_to_u16(haptics.right_motor));
    out[9] = 0x00;
    return 10u;
}

std::size_t build_steam_trackpad_pulse_output(uint8_t side, uint16_t on_us,
                                              uint16_t off_us,
                                              uint16_t repeat_count,
                                              int16_t gain_db, uint8_t *out,
                                              std::size_t out_size)
{
    if (out == nullptr || out_size < 10u) {
        return 0;
    }

    out[0] = kSteamOutHapticPulse;
    out[1] = side;
    write_u16_le(out + 2u, on_us);
    write_u16_le(out + 4u, off_us);
    write_u16_le(out + 6u, repeat_count);
    write_u16_le(out + 8u, static_cast<uint16_t>(gain_db));
    return 10u;
}

std::size_t build_steam_trackpad_command_output(uint8_t side, uint8_t command,
                                                int8_t gain_db, uint8_t *out,
                                                std::size_t out_size)
{
    if (out == nullptr || out_size < 4u) {
        return 0;
    }

    out[0] = kSteamOutHapticCommand;
    out[1] = side;
    out[2] = command;
    out[3] = static_cast<uint8_t>(gain_db);
    return 4u;
}

std::size_t build_steam_power_off_feature_command(uint8_t feature_report_id,
                                                  uint8_t *out,
                                                  std::size_t out_size)
{
    const uint8_t payload[] = {'o', 'f', 'f', '!'};
    const std::size_t len =
        build_steam_feature_command(kSteamCmdPowerOff, payload,
                                    static_cast<uint8_t>(sizeof(payload)),
                                    out, out_size);
    if (len != 0u) {
        out[0] = feature_report_id;
    }
    return len;
}

std::size_t build_steam_power_off_legacy_feature_command(uint8_t *out,
                                                         std::size_t out_size)
{
    if (out == nullptr || out_size < kSteamLegacyFeatureReportSize) {
        return 0;
    }

    std::memset(out, 0, kSteamLegacyFeatureReportSize);
    out[0] = kSteamFeatureReportCommandLegacy;
    out[1] = kSteamCmdPowerOff;
    return kSteamLegacyFeatureReportSize;
}

std::size_t build_steam_power_off_gatt_value(uint8_t *out,
                                             std::size_t out_size)
{
    if (out == nullptr || out_size < kSteamPowerOffGattValueSize) {
        return 0;
    }

    out[0] = kSteamCmdPowerOff;
    out[1] = 4u;
    out[2] = 'o';
    out[3] = 'f';
    out[4] = 'f';
    out[5] = '!';
    return kSteamPowerOffGattValueSize;
}

} // namespace ds5onswitch

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace switchbridge {

constexpr std::size_t kSwitchProInputReportSize = 64;
constexpr uint8_t kSwitchProFullInputReportId = 0x30;
constexpr uint8_t kSwitchProSubcommandReplyReportId = 0x21;
constexpr uint8_t kSwitchProUsbResponseReportId = 0x81;

enum PlayStationButton : uint32_t {
    kPsSquare = 1u << 0,
    kPsCross = 1u << 1,
    kPsCircle = 1u << 2,
    kPsTriangle = 1u << 3,
    kPsL1 = 1u << 4,
    kPsR1 = 1u << 5,
    kPsL2Button = 1u << 6,
    kPsR2Button = 1u << 7,
    kPsCreate = 1u << 8,
    kPsOptions = 1u << 9,
    kPsL3 = 1u << 10,
    kPsR3 = 1u << 11,
    kPsButton = 1u << 12,
    kPsTouchpadClick = 1u << 13,
    kPsMute = 1u << 14,
};

enum SwitchButton : uint32_t {
    kNsB = 1u << 0,
    kNsA = 1u << 1,
    kNsY = 1u << 2,
    kNsX = 1u << 3,
    kNsR = 1u << 4,
    kNsZR = 1u << 5,
    kNsPlus = 1u << 6,
    kNsRightStick = 1u << 7,
    kNsDown = 1u << 8,
    kNsRight = 1u << 9,
    kNsLeft = 1u << 10,
    kNsUp = 1u << 11,
    kNsL = 1u << 12,
    kNsZL = 1u << 13,
    kNsMinus = 1u << 14,
    kNsLeftStick = 1u << 15,
    kNsHome = 1u << 16,
    kNsCapture = 1u << 17,
};

enum class PlayStationDpad : uint8_t {
    Up = 0,
    UpRight = 1,
    Right = 2,
    DownRight = 3,
    Down = 4,
    DownLeft = 5,
    Left = 6,
    UpLeft = 7,
    Neutral = 8,
};

struct PlayStationInput {
    uint8_t left_x = 128;
    uint8_t left_y = 128;
    uint8_t right_x = 128;
    uint8_t right_y = 128;
    uint8_t l2 = 0;
    uint8_t r2 = 0;
    uint32_t buttons = 0;
    PlayStationDpad dpad = PlayStationDpad::Neutral;

    // PlayStation Bluetooth raw IMU convention used by DualSense and DualShock 4:
    // accel ~= 8192 counts/g, gyro ~= 1024 counts/dps.
    int16_t accel_x = 0;
    int16_t accel_y = 0;
    int16_t accel_z = 8192;
    int16_t gyro_x = 0;
    int16_t gyro_y = 0;
    int16_t gyro_z = 0;
    bool has_imu = false;

    bool headphones_connected = false;
    bool mic_connected = false;
    bool mic_muted = false;
    bool external_mic_active = false;
};

struct SwitchProState {
    uint32_t buttons = 0;
    uint16_t left_x = 0x800;
    uint16_t left_y = 0x800;
    uint16_t right_x = 0x800;
    uint16_t right_y = 0x800;

    // Switch Pro IMU report order is accel XYZ, then gyro XYZ.
    // Public Linux constants put accel at 4096 counts/g and gyro at
    // about 14.247 counts/dps.
    int16_t accel_x = 0;
    int16_t accel_y = 0;
    int16_t accel_z = 4096;
    int16_t gyro_x = 0;
    int16_t gyro_y = 0;
    int16_t gyro_z = 0;
    bool has_imu = false;
};

using SwitchProInputReport = std::array<uint8_t, kSwitchProInputReportSize>;
using SwitchReport = std::array<uint8_t, kSwitchProInputReportSize>;

} // namespace switchbridge

#pragma once

#include "ds5onswitch/haptics.h"
#include "ds5onswitch/types.h"

#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

constexpr uint16_t kSteamControllerValveVid = 0x28de;
constexpr uint16_t kSteamControllerBlePid = 0x1303;

constexpr uint8_t kSteamControllerReportState = 0x45;
constexpr uint8_t kSteamControllerReportStateLegacy = 0x42;
constexpr std::size_t kSteamControllerStateReportSize = 46;
constexpr std::size_t kSteamLegacyFeatureReportSize = 65;
constexpr std::size_t kSteamPowerOffGattValueSize = 6;

constexpr uint8_t kSteamFeatureReportCommandLegacy = 0x00;
constexpr uint8_t kSteamFeatureReportCommand = 0x01;
constexpr uint8_t kSteamFeatureReportCommandFallback = 0x02;
constexpr uint8_t kSteamCmdClearDigitalMappings = 0x81;
constexpr uint8_t kSteamCmdSetSettings = 0x87;
constexpr uint8_t kSteamCmdTriggerHapticPulse = 0x8f;
constexpr uint8_t kSteamCmdPowerOff = 0x9f;
constexpr uint8_t kSteamSettingRightTrackpadMode = 0x07;
constexpr uint8_t kSteamSettingLeftTrackpadMode = 0x08;
constexpr uint8_t kSteamSettingImuMode = 0x30;
constexpr uint8_t kSteamTrackpadModeNone = 0x00;
constexpr uint16_t kSteamImuModeRawAccelGyro = 0x0018;

constexpr uint8_t kSteamOutHapticRumble = 0x80;
constexpr uint8_t kSteamOutHapticPulse = 0x81;
constexpr uint8_t kSteamOutHapticCommand = 0x82;
constexpr uint8_t kSteamTrackpadHapticLeft = 0x00;
constexpr uint8_t kSteamTrackpadHapticRight = 0x01;
constexpr uint8_t kSteamHapticCommandTick = 0x01;
constexpr uint8_t kSteamHapticCommandClick = 0x02;
constexpr uint8_t kSteamHapticPulseHighPriority = 0x01;

enum SteamControllerButton : uint32_t {
    kSteamBtnA = 0x00000001u,
    kSteamBtnB = 0x00000002u,
    kSteamBtnX = 0x00000004u,
    kSteamBtnY = 0x00000008u,
    kSteamBtnSteamMenu = 0x00000010u,
    kSteamBtnRightStick = 0x00000020u,
    kSteamBtnMenu = 0x00000040u,
    kSteamBtnR4 = 0x00000080u,
    kSteamBtnR5 = 0x00000100u,
    kSteamBtnRightBumper = 0x00000200u,
    kSteamBtnDpadDown = 0x00000400u,
    kSteamBtnDpadRight = 0x00000800u,
    kSteamBtnDpadLeft = 0x00001000u,
    kSteamBtnDpadUp = 0x00002000u,
    kSteamBtnView = 0x00004000u,
    kSteamBtnLeftStick = 0x00008000u,
    kSteamBtnSteam = 0x00010000u,
    kSteamBtnL4 = 0x00020000u,
    kSteamBtnL5 = 0x00040000u,
    kSteamBtnLeftBumper = 0x00080000u,
    kSteamBtnRightStickTouch = 0x00100000u,
    kSteamBtnRightPadTouch = 0x00200000u,
    kSteamBtnRightPadClick = 0x00400000u,
    kSteamBtnRightTriggerFull = 0x00800000u,
    kSteamBtnLeftStickTouch = 0x01000000u,
    kSteamBtnLeftPadTouch = 0x02000000u,
    kSteamBtnLeftPadClick = 0x04000000u,
    kSteamBtnGripRight = 0x10000000u,
    kSteamBtnGripLeft = 0x20000000u,
};

struct SteamControllerInput {
    uint8_t sequence = 0;
    uint32_t buttons = 0;
    uint16_t left_trigger = 0;
    uint16_t right_trigger = 0;
    int16_t left_stick_x = 0;
    int16_t left_stick_y = 0;
    int16_t right_stick_x = 0;
    int16_t right_stick_y = 0;
    int16_t left_pad_x = 0;
    int16_t left_pad_y = 0;
    int16_t left_pad_force = 0;
    int16_t right_pad_x = 0;
    int16_t right_pad_y = 0;
    int16_t right_pad_force = 0;
    uint32_t imu_timestamp = 0;
    int16_t accel_x = 0;
    int16_t accel_y = 0;
    int16_t accel_z = 0;
    int16_t gyro_x = 0;
    int16_t gyro_y = 0;
    int16_t gyro_z = 0;
    bool has_imu = false;
};

bool parse_steam_controller_report(const uint8_t *data, std::size_t size,
                                   SteamControllerInput &out);

uint8_t steam_trigger_to_u8(uint16_t raw);
uint16_t steam_axis_to_switch12(int16_t raw, bool invert = false);
int16_t steam_accel_to_switch(int16_t raw);
SwitchProState map_steam_to_switch(const SteamControllerInput &input);

std::size_t build_steam_feature_command(uint8_t command, const uint8_t *payload,
                                        uint8_t payload_size, uint8_t *out,
                                        std::size_t out_size);
std::size_t build_steam_settings_feature_command(uint8_t *out,
                                                 std::size_t out_size);
std::size_t build_steam_body_rumble_output(const DualSenseHapticCommand &haptics,
                                           uint8_t *out,
                                           std::size_t out_size);
std::size_t build_steam_trackpad_pulse_output(uint8_t side, uint16_t on_us,
                                              uint16_t off_us,
                                              uint16_t repeat_count,
                                              int16_t gain_db, uint8_t *out,
                                              std::size_t out_size);
std::size_t build_steam_trackpad_command_output(uint8_t side, uint8_t command,
                                                int8_t gain_db, uint8_t *out,
                                                std::size_t out_size);
std::size_t build_steam_power_off_feature_command(uint8_t feature_report_id,
                                                  uint8_t *out,
                                                  std::size_t out_size);
std::size_t build_steam_power_off_legacy_feature_command(uint8_t *out,
                                                         std::size_t out_size);
std::size_t build_steam_power_off_gatt_value(uint8_t *out,
                                             std::size_t out_size);

} // namespace ds5onswitch

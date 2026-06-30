#pragma once

#include "switch_bridge/haptics.h"
#include "switch_bridge/types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace switchbridge {

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
constexpr uint16_t kSteamTrackpadClickPulseUs = 5000;
// Drag/touch tick gain. SteamlessController used -45 dB on PC, which is
// effectively inaudible on this firmware path, so the drag haptic was not
// felt. Raised to a felt-but-light level; this is the perceptual tuning knob
// (more negative = softer, less negative = stronger).
constexpr int8_t kSteamTrackpadTouchGainDb = 2;
// Strong-click gains reduced to ~60% amplitude (-4.4 dB) per request:
// 9 -> 5 dB and 40 -> 36 dB each give ~63% linear amplitude.
constexpr int8_t kSteamTrackpadClickCommandGainDb = 5;
constexpr int16_t kSteamTrackpadClickPulseGainDb = 36;

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

struct SteamTrackpadHapticState {
    bool prev_left_click = false;
    bool prev_right_click = false;
    uint8_t prev_left_dpad_mask = 0;
    uint8_t prev_right_dpad_mask = 0;
    bool prev_left_touch = false;
    bool prev_right_touch = false;
    int16_t prev_left_x = 0;
    int16_t prev_left_y = 0;
    int16_t prev_right_x = 0;
    int16_t prev_right_y = 0;
    uint32_t last_left_pulse_ms = 0;
    uint32_t last_right_pulse_ms = 0;
};

struct SteamTrackpadHapticAction {
    bool left_tick = false;
    bool right_tick = false;
    bool left_click = false;
    bool right_click = false;
};

enum class SteamBackButton : uint8_t {
    L4,
    R4,
    L5,
    R5,
    Count,
};

enum class SteamBackAction : uint8_t {
    None,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    South,
    East,
    West,
    North,
    L1,
    R1,
    L2,
    R2,
    L3,
    R3,
    Back,
    Start,
    Guide,
    Capture,
};

using SteamBackMappings =
    std::array<SteamBackAction, static_cast<std::size_t>(SteamBackButton::Count)>;

struct SteamBackAssignmentState {
    bool active = false;
    bool suppress_until_release = false;
    SteamBackButton target = SteamBackButton::L4;
    uint32_t deadline_ms = 0;
    SteamControllerInput baseline{};
};

struct SteamBackAssignmentResult {
    bool consumed = false;
    bool assignment_started = false;
    bool mapping_changed = false;
};

bool parse_steam_controller_report(const uint8_t *data, std::size_t size,
                                   SteamControllerInput &out);

uint8_t steam_trigger_to_u8(uint16_t raw);
uint16_t steam_axis_to_switch12(int16_t raw, bool invert = false);
int16_t steam_accel_to_switch(int16_t raw);
SwitchProState map_steam_to_switch(const SteamControllerInput &input);
SwitchProState map_steam_to_switch(const SteamControllerInput &input,
                                   const SteamBackMappings &back_mappings);
SteamTrackpadHapticAction update_steam_trackpad_haptics(
    const SteamControllerInput &input, SteamTrackpadHapticState &state,
    uint32_t now_ms);

SteamBackMappings steam_back_default_mappings();
bool steam_back_mappings_valid(const SteamBackMappings &mappings);
void steam_back_normalize_mappings(SteamBackMappings &mappings);
void steam_back_assignment_reset(SteamBackAssignmentState &state);
SteamBackAssignmentResult steam_back_assignment_process(
    SteamControllerInput &input, const SteamControllerInput *previous_input,
    SteamBackMappings &mappings, SteamBackAssignmentState &state,
    uint32_t now_ms);

std::size_t build_steam_feature_command(uint8_t command, const uint8_t *payload,
                                        uint8_t payload_size, uint8_t *out,
                                        std::size_t out_size);
std::size_t build_steam_settings_feature_command(uint8_t *out,
                                                 std::size_t out_size);
std::size_t build_steam_body_rumble_output(const HapticCommand &haptics,
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
std::size_t build_steam_trackpad_feature_pulse_command(
    uint8_t pad, uint16_t on_us, uint16_t off_us, uint16_t repeat_count,
    int16_t gain_db, uint8_t *out, std::size_t out_size);
std::size_t build_steam_power_off_feature_command(uint8_t feature_report_id,
                                                  uint8_t *out,
                                                  std::size_t out_size);
std::size_t build_steam_power_off_legacy_feature_command(uint8_t *out,
                                                         std::size_t out_size);
std::size_t build_steam_power_off_gatt_value(uint8_t *out,
                                             std::size_t out_size);

} // namespace switchbridge

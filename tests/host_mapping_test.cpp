#include "switch_bridge/dualsense_output.h"
#include "switch_bridge/dualshock4_output.h"
#include "switch_bridge/dualshock4_report.h"
#include "switch_bridge/dualsense_report.h"
#include "switch_bridge/haptics.h"
#include "switch_bridge/mapper.h"
#include "switch_bridge/persona_combo_filter.h"
#include "switch_bridge/procon2_handshake.h"
#include "switch_bridge/steam_controller.h"
#include "switch_bridge/switch2_auto_registration.h"
#include "switch_bridge/switch_identity.h"
#include "switch_bridge/switch_pro.h"
#include "switch_bridge/switch_usb.h"
#include "switch_bridge/switch2_wake.h"
#include "switch_bridge/xbox_controller.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

using namespace switchbridge;

namespace {

int16_t read_i16_le(const uint8_t *p)
{
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) |
                                (static_cast<uint16_t>(p[1]) << 8u));
}

void test_button_mapping()
{
    PlayStationInput in{};
    in.buttons = kPsCross | kPsCircle | kPsSquare | kPsTriangle |
                 kPsL1 | kPsR1 | kPsCreate | kPsOptions |
                 kPsL3 | kPsR3 | kPsButton | kPsTouchpadClick;
    in.l2 = 64;
    in.r2 = 255;
    in.dpad = PlayStationDpad::UpRight;

    const SwitchProState state = map_playstation_to_switch(in);
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

void test_persona_combo_filter()
{
    PersonaComboFilter filter;

    PersonaComboFilterResult r = filter.process(kPsButton, 0);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kPsButton) != 0);
    r = filter.process(kPsButton, 8);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kPsButton) != 0);
    r = filter.process(0, 16);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kPsButton) == 0);

    filter.reset();
    (void)filter.process(kPsButton, 0);
    r = filter.process(kPsButton | kPsTouchpadClick, 8);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kPsButton) != 0);
    assert((r.buttons & kPsTouchpadClick) == 0);
    r = filter.process(kPsTouchpadClick, 16);
    assert(r.request == PersonaComboRequest::SwitchPro);
    assert((r.buttons & kPsButton) == 0);
    assert((r.buttons & kPsTouchpadClick) == 0);
    r = filter.process(kPsTouchpadClick, 24);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kPsTouchpadClick) == 0);

    filter.reset();
    (void)filter.process(kPsButton, 0);
    (void)filter.process(kPsButton | kPsTouchpadClick, 8);
    (void)filter.process(kPsButton, 16);
    (void)filter.process(kPsButton | kPsTouchpadClick, 24);
    (void)filter.process(kPsButton, 32);
    r = filter.process(0, 40);
    assert(r.request == PersonaComboRequest::Switch2Pro);
    assert((r.buttons & kPsButton) == 0);
    assert((r.buttons & kPsTouchpadClick) == 0);

    filter.reset();
    (void)filter.process(kPsButton, 0);
    r = filter.process(0, 8);
    assert((r.buttons & kPsButton) == 0);
    r = filter.process(kPsButton, 16);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kPsButton) != 0);
    r = filter.process(kPsButton | kPsTouchpadClick, 24);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kPsButton) != 0);
    assert((r.buttons & kPsTouchpadClick) == 0);
    r = filter.process(0, 32);
    assert(r.request == PersonaComboRequest::SwitchPro);
    assert((r.buttons & kPsButton) == 0);
    assert((r.buttons & kPsTouchpadClick) == 0);

    filter.reset();
    r = filter.process(kNsHome, kNsHome, kNsCapture, 0);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kNsHome) != 0);
    r = filter.process(kNsHome | kNsCapture, kNsHome, kNsCapture, 8);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kNsHome) != 0);
    assert((r.buttons & kNsCapture) == 0);
    r = filter.process(kNsCapture, kNsHome, kNsCapture, 16);
    assert(r.request == PersonaComboRequest::SwitchPro);
    assert((r.buttons & kNsHome) == 0);
    assert((r.buttons & kNsCapture) == 0);

    filter.reset();
    (void)filter.process(kNsHome, kNsHome, kNsCapture, 0);
    (void)filter.process(kNsHome | kNsCapture, kNsHome, kNsCapture, 8);
    (void)filter.process(kNsHome, kNsHome, kNsCapture, 16);
    (void)filter.process(kNsHome | kNsCapture, kNsHome, kNsCapture, 24);
    (void)filter.process(kNsHome, kNsHome, kNsCapture, 32);
    r = filter.process(0, kNsHome, kNsCapture, 40);
    assert(r.request == PersonaComboRequest::Switch2Pro);
    assert((r.buttons & kNsHome) == 0);
    assert((r.buttons & kNsCapture) == 0);
}

void test_axis_and_motion_mapping()
{
    assert(axis8_to_switch12(0) == 0);
    assert(axis8_to_switch12(255) == 0x0fff);
    assert(axis8_to_switch12(128) >= 0x800);
    assert(playstation_gyro_to_switch(1024) == 1024);
    assert(playstation_gyro_to_switch(-2048) == -2048);

    PlayStationInput in{};
    in.left_x = 0;
    in.left_y = 255;
    in.right_x = 128;
    in.right_y = 0;
    in.has_imu = true;
    in.accel_x = 8192;
    in.accel_y = -4096;
    in.accel_z = 8192;
    in.gyro_x = 1024;
    in.gyro_y = -2048;
    in.gyro_z = 512;

    const SwitchProState state = map_playstation_to_switch(in);
    assert(state.left_x == 0);
    assert(state.left_y == 0);
    assert(state.right_x == axis8_to_switch12(128));
    assert(state.right_y == 0x0fff);
    assert(state.has_imu);
    assert(state.accel_x == playstation_accel_to_switch(-in.accel_z));
    assert(state.accel_y == playstation_accel_to_switch(-in.accel_x));
    assert(state.accel_z == playstation_accel_to_switch(in.accel_y));
    assert(state.gyro_x == playstation_gyro_to_switch(-in.gyro_y));
    assert(state.gyro_y == playstation_gyro_to_switch(-in.gyro_x));
    assert(state.gyro_z == playstation_gyro_to_switch(in.gyro_z));

    const SwitchProInputReport report = build_switch_pro_input_report(state, 1, false);
    assert(read_i16_le(&report[13]) == state.accel_x);
    assert(read_i16_le(&report[15]) == state.accel_y);
    assert(read_i16_le(&report[17]) == state.accel_z);
    assert(read_i16_le(&report[19]) == state.gyro_x);
    assert(read_i16_le(&report[21]) == state.gyro_y);
    assert(read_i16_le(&report[23]) == state.gyro_z);
    assert(report[12] == 0x30);
}

void test_nrf5340_steam_motion_axis_parity()
{
    constexpr int16_t steam_gyro_x = 0x1234;
    constexpr int16_t steam_gyro_y = -0x0201;
    constexpr int16_t steam_gyro_z = 0x3456;
    constexpr int16_t steam_accel_x = -0x0102;
    constexpr int16_t steam_accel_y = 0x0203;
    constexpr int16_t steam_accel_z = -0x3040;

    PlayStationInput in{};
    in.has_imu = true;
    in.gyro_x = steam_gyro_x;
    in.gyro_y = static_cast<int16_t>(-steam_gyro_y);
    in.gyro_z = steam_gyro_z;
    in.accel_x = steam_accel_x;
    in.accel_y = steam_accel_z;
    in.accel_z = static_cast<int16_t>(-steam_accel_y);

    const SwitchProState state = map_playstation_to_switch(in);
    assert(state.gyro_x == steam_gyro_y);
    assert(state.gyro_y == static_cast<int16_t>(-steam_gyro_x));
    assert(state.gyro_z == steam_gyro_z);

    // nRF5340 Switch Pro scales Steam accel separately. This checks the
    // source axis/sign selection before DualSense-to-Switch scaling.
    assert(state.accel_x == playstation_accel_to_switch(steam_accel_y));
    assert(state.accel_y == playstation_accel_to_switch(-steam_accel_x));
    assert(state.accel_z == playstation_accel_to_switch(steam_accel_z));
}

void test_haptics()
{
    SwitchRumbleTranslator translator;
    const SwitchRumblePacket silent{0x00, 0x01, 0x40, 0x40};
    const SwitchRumblePacket impact{0x34, 0xf0, 0x62, 0xe0};

    HapticCommand cmd = translator.translate(silent, silent);
    assert(!cmd.active);

    cmd = translator.translate(impact, silent);
    assert(cmd.active);
    assert(cmd.left_pulse > 0);

    cmd = translator.translate(silent, silent);
    assert(!cmd.active);
}

void write_i16_le(uint8_t *p, int16_t value)
{
    const uint16_t raw = static_cast<uint16_t>(value);
    p[0] = static_cast<uint8_t>(raw);
    p[1] = static_cast<uint8_t>(raw >> 8u);
}

void write_u16_le(uint8_t *p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8u);
}

void write_u32_le(uint8_t *p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8u);
    p[2] = static_cast<uint8_t>(value >> 16u);
    p[3] = static_cast<uint8_t>(value >> 24u);
}

void test_steam_controller_input_parser_and_mapping()
{
    std::array<uint8_t, kSteamControllerStateReportSize> report{};
    report[0] = kSteamControllerReportState;
    report[1] = 0x7a;
    const uint32_t buttons =
        kSteamBtnA | kSteamBtnB | kSteamBtnX | kSteamBtnY |
        kSteamBtnLeftBumper | kSteamBtnRightBumper | kSteamBtnView |
        kSteamBtnMenu | kSteamBtnLeftStick | kSteamBtnRightStick |
        kSteamBtnSteam | kSteamBtnSteamMenu | kSteamBtnRightPadClick |
        kSteamBtnDpadUp | kSteamBtnDpadRight;
    write_u32_le(&report[2], buttons);
    write_u16_le(&report[6], 0x0080);
    write_u16_le(&report[8], 0xffff);
    write_i16_le(&report[10], -32768);
    write_i16_le(&report[12], 0);
    write_i16_le(&report[14], 32767);
    write_i16_le(&report[16], -1);
    write_i16_le(&report[24], 1234);
    write_i16_le(&report[26], -5678);
    write_u32_le(&report[30], 0x01020304);
    write_i16_le(&report[34], -0x0102);
    write_i16_le(&report[36], 0x0203);
    write_i16_le(&report[38], -0x3040);
    write_i16_le(&report[40], 0x1234);
    write_i16_le(&report[42], -0x0201);
    write_i16_le(&report[44], 0x3456);

    SteamControllerInput input{};
    assert(parse_steam_controller_report(report.data(), report.size(), input));
    assert(input.sequence == 0x7a);
    assert(input.buttons == buttons);
    assert(input.left_trigger == 0x0080);
    assert(input.right_trigger == 0xffff);
    assert(input.left_stick_x == -32768);
    assert(input.left_stick_y == 0);
    assert(input.right_stick_x == 32767);
    assert(input.right_stick_y == -1);
    assert(input.right_pad_x == 1234);
    assert(input.right_pad_y == -5678);
    assert(input.has_imu);

    const SwitchProState state = map_steam_to_switch(input);
    assert((state.buttons & kNsB) != 0);
    assert((state.buttons & kNsA) != 0);
    assert((state.buttons & kNsY) != 0);
    assert((state.buttons & kNsX) != 0);
    assert((state.buttons & kNsL) != 0);
    assert((state.buttons & kNsR) != 0);
    assert((state.buttons & kNsMinus) != 0);
    assert((state.buttons & kNsPlus) != 0);
    assert((state.buttons & kNsLeftStick) != 0);
    assert((state.buttons & kNsRightStick) != 0);
    assert((state.buttons & kNsHome) != 0);
    assert((state.buttons & kNsCapture) != 0);
    assert((state.buttons & kNsZL) != 0);
    assert((state.buttons & kNsZR) != 0);
    assert((state.buttons & kNsUp) != 0);
    assert((state.buttons & kNsRight) != 0);
    assert(state.left_x == 0);
    assert(state.left_y == 2048);
    assert(state.right_x == steam_axis_to_switch12(input.right_pad_x));
    assert(state.right_y == steam_axis_to_switch12(input.right_pad_y));
    assert(state.accel_x == steam_accel_to_switch(input.accel_y));
    assert(state.accel_y == steam_accel_to_switch(static_cast<int16_t>(-input.accel_x)));
    assert(state.accel_z == steam_accel_to_switch(input.accel_z));
    assert(state.gyro_x == input.gyro_y);
    assert(state.gyro_y == static_cast<int16_t>(-input.gyro_x));
    assert(state.gyro_z == input.gyro_z);

    SteamControllerInput no_right_pad = input;
    no_right_pad.buttons &= ~(kSteamBtnRightPadTouch | kSteamBtnRightPadClick);
    no_right_pad.right_pad_force = 0;
    const SwitchProState no_right_pad_state = map_steam_to_switch(no_right_pad);
    assert(no_right_pad_state.right_x ==
           steam_axis_to_switch12(input.right_stick_x));
    assert(no_right_pad_state.right_y ==
           steam_axis_to_switch12(input.right_stick_y));

    SteamControllerInput right_pad_click_only{};
    right_pad_click_only.buttons = kSteamBtnRightPadClick;
    const SwitchProState right_pad_click_state =
        map_steam_to_switch(right_pad_click_only);
    assert((right_pad_click_state.buttons & kNsRightStick) != 0);
    assert((right_pad_click_state.buttons & kNsCapture) == 0);

    SteamControllerInput qam_capture{};
    qam_capture.buttons = kSteamBtnSteamMenu;
    const SwitchProState qam_capture_state = map_steam_to_switch(qam_capture);
    assert((qam_capture_state.buttons & kNsCapture) != 0);

    constexpr uint32_t dpad_mask = kNsUp | kNsRight | kNsDown | kNsLeft;
    std::array<uint8_t, kSteamControllerStateReportSize> pad_report{};
    pad_report[0] = kSteamControllerReportState;
    write_u32_le(&pad_report[2], kSteamBtnLeftPadClick);
    write_i16_le(&pad_report[18], 12000);
    write_i16_le(&pad_report[20], 12000);
    SteamControllerInput pad{};
    assert(parse_steam_controller_report(pad_report.data(), pad_report.size(), pad));
    assert(pad.left_pad_x == 12000);
    assert(pad.left_pad_y == 12000);
    const SwitchProState pad_state = map_steam_to_switch(pad);
    assert((pad_state.buttons & dpad_mask) == (kNsUp | kNsRight));

    pad.left_pad_x = 8000;
    pad.left_pad_y = 0;
    const SwitchProState deadzone_state = map_steam_to_switch(pad);
    assert((deadzone_state.buttons & dpad_mask) == 0);

    pad.buttons = kSteamBtnLeftPadClick | kSteamBtnDpadDown;
    pad.left_pad_x = -12000;
    pad.left_pad_y = 0;
    const SwitchProState override_state = map_steam_to_switch(pad);
    assert((override_state.buttons & dpad_mask) == kNsLeft);

    std::array<uint8_t, kSteamControllerStateReportSize + 1u> bt_report{};
    bt_report[0] = 0xa1;
    bt_report[1] = kSteamControllerReportStateLegacy;
    std::memcpy(bt_report.data() + 2u, report.data() + 1u, report.size() - 1u);
    SteamControllerInput legacy{};
    assert(parse_steam_controller_report(bt_report.data(), bt_report.size(), legacy));
    assert(legacy.buttons == input.buttons);
    assert(legacy.gyro_z == input.gyro_z);
}

void test_steam_persona_combo_filter()
{
    PersonaComboFilter filter;

    PersonaComboFilterResult r =
        filter.process(kSteamBtnSteamMenu, kSteamBtnSteam,
                       kSteamBtnSteamMenu, 0);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kSteamBtnSteamMenu) != 0);

    filter.reset();
    (void)filter.process(kSteamBtnSteam, kSteamBtnSteam,
                         kSteamBtnSteamMenu, 0);
    r = filter.process(kSteamBtnSteam | kSteamBtnSteamMenu,
                       kSteamBtnSteam, kSteamBtnSteamMenu, 8);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kSteamBtnSteam) != 0);
    assert((r.buttons & kSteamBtnSteamMenu) == 0);
    r = filter.process(0, kSteamBtnSteam, kSteamBtnSteamMenu, 16);
    assert(r.request == PersonaComboRequest::SwitchPro);
    assert((r.buttons & kSteamBtnSteam) == 0);
    assert((r.buttons & kSteamBtnSteamMenu) == 0);

    filter.reset();
    (void)filter.process(kSteamBtnSteam, kSteamBtnSteam,
                         kSteamBtnSteamMenu, 0);
    (void)filter.process(kSteamBtnSteam | kSteamBtnSteamMenu,
                         kSteamBtnSteam, kSteamBtnSteamMenu, 8);
    (void)filter.process(kSteamBtnSteam, kSteamBtnSteam,
                         kSteamBtnSteamMenu, 16);
    (void)filter.process(kSteamBtnSteam | kSteamBtnSteamMenu,
                         kSteamBtnSteam, kSteamBtnSteamMenu, 24);
    (void)filter.process(kSteamBtnSteam, kSteamBtnSteam,
                         kSteamBtnSteamMenu, 32);
    r = filter.process(0, kSteamBtnSteam, kSteamBtnSteamMenu, 40);
    assert(r.request == PersonaComboRequest::Switch2Pro);
}

void test_steam_output_builders()
{
    std::array<uint8_t, 64> buf{};
    const std::size_t clear_len =
        build_steam_feature_command(kSteamCmdClearDigitalMappings, nullptr, 0,
                                    buf.data(), buf.size());
    assert(clear_len == 64u);
    assert(buf[0] == kSteamFeatureReportCommand);
    assert(buf[1] == kSteamCmdClearDigitalMappings);
    assert(buf[2] == 0);

    const std::size_t settings_len =
        build_steam_settings_feature_command(buf.data(), buf.size());
    assert(settings_len == 64u);
    assert(buf[0] == kSteamFeatureReportCommand);
    assert(buf[1] == kSteamCmdSetSettings);
    assert(buf[2] == 9u);
    assert(buf[3] == kSteamSettingImuMode);
    assert(buf[4] == 0x18u);
    assert(buf[5] == 0x00u);

    HapticCommand haptics{};
    haptics.left_motor = 3;
    haptics.right_motor = 4;
    std::array<uint8_t, 10> out{};
    const std::size_t rumble_len =
        build_steam_body_rumble_output(haptics, out.data(), out.size());
    assert(rumble_len == 10u);
    assert(out[0] == kSteamOutHapticRumble);
    assert(out[4] == 0x03u && out[5] == 0x03u);
    assert(out[7] == 0x04u && out[8] == 0x04u);

    const std::size_t pulse_len =
        build_steam_trackpad_pulse_output(kSteamTrackpadHapticRight, 1200, 0,
                                          1, 2, out.data(), out.size());
    assert(pulse_len == 10u);
    assert(out[0] == kSteamOutHapticPulse);
    assert(out[1] == kSteamTrackpadHapticRight);
    assert(out[2] == 0xb0u && out[3] == 0x04u);

    const std::size_t command_len =
        build_steam_trackpad_command_output(kSteamTrackpadHapticRight,
                                            kSteamHapticCommandTick,
                                            kSteamTrackpadTouchGainDb,
                                            out.data(), out.size());
    assert(command_len == 4u);
    assert(out[0] == kSteamOutHapticCommand);
    assert(out[1] == kSteamTrackpadHapticRight);
    assert(out[2] == kSteamHapticCommandTick);
    assert(out[3] == 0xd3u);

    const std::size_t feature_pulse_len =
        build_steam_trackpad_feature_pulse_command(
            kSteamTrackpadHapticRight, kSteamTrackpadClickPulseUs, 0, 1,
            kSteamTrackpadClickPulseGainDb, buf.data(), buf.size());
    assert(feature_pulse_len == 64u);
    assert(buf[0] == kSteamFeatureReportCommand);
    assert(buf[1] == kSteamCmdTriggerHapticPulse);
    assert(buf[2] == 10u);
    assert(buf[3] == kSteamTrackpadHapticRight);
    assert(buf[4] == 0x88u && buf[5] == 0x13u);
    assert(buf[8] == 0x01u && buf[9] == 0x00u);
    assert(buf[10] == 0x28u && buf[11] == 0x00u);
    assert(buf[12] == kSteamHapticPulseHighPriority);

    std::array<uint8_t, kSteamLegacyFeatureReportSize> power{};
    const std::size_t power_len =
        build_steam_power_off_feature_command(kSteamFeatureReportCommand,
                                              power.data(), power.size());
    assert(power_len == 64u);
    assert(power[0] == kSteamFeatureReportCommand);
    assert(power[1] == kSteamCmdPowerOff);
    assert(power[2] == 4u);
    assert(std::memcmp(power.data() + 3u, "off!", 4) == 0);

    const std::size_t fallback_len =
        build_steam_power_off_feature_command(kSteamFeatureReportCommandFallback,
                                              power.data(), power.size());
    assert(fallback_len == 64u);
    assert(power[0] == kSteamFeatureReportCommandFallback);
    assert(power[1] == kSteamCmdPowerOff);
    assert(power[2] == 4u);
    assert(std::memcmp(power.data() + 3u, "off!", 4) == 0);

    const std::size_t legacy_power_len =
        build_steam_power_off_legacy_feature_command(power.data(),
                                                     power.size());
    assert(legacy_power_len == kSteamLegacyFeatureReportSize);
    assert(power[0] == kSteamFeatureReportCommandLegacy);
    assert(power[1] == kSteamCmdPowerOff);

    std::array<uint8_t, kSteamPowerOffGattValueSize> gatt_power{};
    const std::size_t gatt_power_len =
        build_steam_power_off_gatt_value(gatt_power.data(), gatt_power.size());
    assert(gatt_power_len == kSteamPowerOffGattValueSize);
    assert(gatt_power[0] == kSteamCmdPowerOff);
    assert(gatt_power[1] == 4u);
    assert(std::memcmp(gatt_power.data() + 2u, "off!", 4) == 0);
}

void test_steam_trackpad_haptic_policy()
{
    SteamTrackpadHapticState state{};
    SteamControllerInput input{};

    SteamTrackpadHapticAction action =
        update_steam_trackpad_haptics(input, state, 0);
    assert(!action.left_click && !action.right_click);
    assert(!action.left_tick && !action.right_tick);

    input.buttons = kSteamBtnRightPadTouch;
    input.right_pad_x = 1000;
    input.right_pad_y = 1000;
    action = update_steam_trackpad_haptics(input, state, 1);
    assert(!action.right_tick && !action.right_click);

    input.right_pad_x = 3000;
    input.right_pad_y = 1100;
    action = update_steam_trackpad_haptics(input, state, 25);
    assert(action.right_tick && !action.right_click);

    input.right_pad_x = 5200;
    action = update_steam_trackpad_haptics(input, state, 35);
    assert(!action.right_tick && !action.right_click);

    input.buttons = kSteamBtnRightPadTouch | kSteamBtnRightPadClick;
    action = update_steam_trackpad_haptics(input, state, 60);
    assert(action.right_click && !action.right_tick);

    action = update_steam_trackpad_haptics(input, state, 90);
    assert(!action.right_click && action.right_tick);

    input.buttons = 0;
    action = update_steam_trackpad_haptics(input, state, 120);
    assert(action.right_click && !action.right_tick);

    input = {};
    input.buttons = kSteamBtnLeftPadClick;
    input.left_pad_x = 30000;
    input.left_pad_y = 0;
    action = update_steam_trackpad_haptics(input, state, 150);
    assert(action.left_click && !action.left_tick);

    input.left_pad_x = 0;
    input.left_pad_y = 30000;
    action = update_steam_trackpad_haptics(input, state, 180);
    assert(action.left_click && !action.left_tick);

    action = update_steam_trackpad_haptics(input, state, 210);
    assert(!action.left_click && !action.left_tick);

    input.buttons = 0;
    action = update_steam_trackpad_haptics(input, state, 240);
    assert(action.left_click && !action.left_tick);
}

void test_xbox_controller_input_parser_and_mapping()
{
    std::array<uint8_t, 20> report{};
    report[0] = kXboxInputReportId;
    report[1] = 0x00;
    report[2] = 0x00;
    report[3] = 0xff;
    report[4] = 0xff;
    report[5] = 0x00;
    report[6] = 0x80;
    report[7] = 0x00;
    report[8] = 0x00;
    report[9] = 0x40;
    report[10] = 0x00;
    report[11] = 0xff;
    report[12] = 0x03;
    report[13] = 2; // up-right
    report[14] = static_cast<uint8_t>((1u << 0) | (1u << 3) |
                                      (1u << 6)); // A, X, LB
    report[15] = static_cast<uint8_t>((1u << 2) | (1u << 3) |
                                      (1u << 4) | (1u << 5)); // Back, Menu, Guide, LS

    XboxControllerInput input{};
    assert(parse_xbox_bt_report(report.data(), report.size(), input));
    assert(input.left_x == 0);
    assert(input.left_y == 0xffff);
    assert(input.right_x == 0x8000);
    assert(input.right_y == 0);
    assert(input.left_trigger == 0x0040);
    assert(input.right_trigger == 0x03ff);
    assert((input.buttons & kXboxBtnA) != 0);
    assert((input.buttons & kXboxBtnX) != 0);
    assert((input.buttons & kXboxBtnLeftBumper) != 0);
    assert((input.buttons & kXboxBtnBack) != 0);
    assert((input.buttons & kXboxBtnMenu) != 0);
    assert((input.buttons & kXboxBtnGuide) != 0);
    assert((input.buttons & kXboxBtnLeftStick) != 0);
    assert((input.buttons & kXboxBtnDpadUp) != 0);
    assert((input.buttons & kXboxBtnDpadRight) != 0);

    const SwitchProState state = map_xbox_to_switch(input);
    assert(state.left_x == 0);
    assert(state.left_y == 0);
    assert(state.right_x >= 0x7ff && state.right_x <= 0x800);
    assert(state.right_y == 0x0fff);
    assert((state.buttons & kNsB) != 0);
    assert((state.buttons & kNsY) != 0);
    assert((state.buttons & kNsL) != 0);
    assert((state.buttons & kNsMinus) != 0);
    assert((state.buttons & kNsPlus) != 0);
    assert((state.buttons & kNsHome) != 0);
    assert((state.buttons & kNsLeftStick) != 0);
    assert((state.buttons & kNsUp) != 0);
    assert((state.buttons & kNsRight) != 0);
    assert((state.buttons & kNsZL) != 0);
    assert((state.buttons & kNsZR) != 0);

    std::array<uint8_t, 21> classic{};
    classic[0] = 0xa1;
    std::memcpy(classic.data() + 1u, report.data(), report.size());
    XboxControllerInput classic_input{};
    assert(parse_xbox_bt_report(classic.data(), classic.size(), classic_input));
    assert(classic_input.buttons == input.buttons);

    std::array<uint8_t, 18> classic_short{};
    classic_short[0] = 0xa1;
    std::memcpy(classic_short.data() + 1u, report.data(), 17u);
    XboxControllerInput classic_short_input{};
    assert(parse_xbox_bt_report(classic_short.data(), classic_short.size(),
                                classic_short_input));
    assert(classic_short_input.left_x == input.left_x);
    assert(classic_short_input.left_y == input.left_y);
    assert(classic_short_input.right_x == input.right_x);
    assert(classic_short_input.right_y == input.right_y);
    assert(classic_short_input.left_trigger == input.left_trigger);
    assert(classic_short_input.right_trigger == input.right_trigger);
    assert(classic_short_input.buttons == input.buttons);

    std::array<uint8_t, kXboxElite2InputPayloadSize> elite_payload{};
    std::memcpy(elite_payload.data(), report.data() + 1u,
                elite_payload.size());
    elite_payload[0] = 0x34;
    elite_payload[1] = 0x12;
    XboxControllerInput elite_input{};
    assert(parse_xbox_bt_report(elite_payload.data(), elite_payload.size(),
                                elite_input));
    assert(elite_input.left_x == 0x1234);
    assert(elite_input.left_y == input.left_y);
    assert(elite_input.right_x == input.right_x);
    assert(elite_input.right_y == input.right_y);
    assert(elite_input.left_trigger == input.left_trigger);
    assert(elite_input.right_trigger == input.right_trigger);
    assert(elite_input.buttons == input.buttons);

    std::array<uint8_t, 20> legacy_back{};
    legacy_back[0] = kXboxInputReportId;
    legacy_back[16] = 1u;
    XboxControllerInput legacy_input{};
    assert(parse_xbox_bt_report(legacy_back.data(), legacy_back.size(),
                                legacy_input));
    assert((legacy_input.buttons & kXboxBtnBack) != 0);
    const SwitchProState legacy_state = map_xbox_to_switch(legacy_input);
    assert((legacy_state.buttons & kNsMinus) != 0);
    assert((legacy_state.buttons & kNsCapture) == 0);

    std::array<uint8_t, kXboxInputReportMinSize> compact{};
    compact[0] = kXboxInputReportId;
    compact[13] = 5; // down
    compact[14] = static_cast<uint8_t>((1u << 0) | (1u << 2) |
                                       (1u << 4) | (1u << 6) |
                                       (1u << 7)); // A, X, LB, Back, Menu
    compact[15] = static_cast<uint8_t>((1u << 0) | (1u << 1) |
                                       (1u << 2)); // LS, RS, Guide
    XboxControllerInput compact_input{};
    assert(parse_xbox_bt_report(compact.data(), compact.size(),
                                compact_input));
    assert((compact_input.buttons & kXboxBtnA) != 0);
    assert((compact_input.buttons & kXboxBtnX) != 0);
    assert((compact_input.buttons & kXboxBtnLeftBumper) != 0);
    assert((compact_input.buttons & kXboxBtnBack) != 0);
    assert((compact_input.buttons & kXboxBtnMenu) != 0);
    assert((compact_input.buttons & kXboxBtnLeftStick) != 0);
    assert((compact_input.buttons & kXboxBtnRightStick) != 0);
    assert((compact_input.buttons & kXboxBtnGuide) != 0);
    assert((compact_input.buttons & kXboxBtnDpadDown) != 0);

    compact[0] = 0x00; // BLE HIDS may wrap an undefined Report ID as zero.
    XboxControllerInput undefined_id_input{};
    assert(parse_xbox_bt_report(compact.data(), compact.size(),
                                undefined_id_input));
    assert((undefined_id_input.buttons & kXboxBtnA) != 0);
    assert((undefined_id_input.buttons & kXboxBtnBack) != 0);
    assert((undefined_id_input.buttons & kXboxBtnGuide) != 0);
    compact[0] = kXboxInputReportId;

    std::array<uint8_t, 2> guide_report{kXboxGuideReportId, 0x01};
    XboxControllerInput merged_guide{};
    assert(parse_xbox_bt_report(compact.data(), compact.size(),
                                merged_guide));
    assert(parse_xbox_bt_report(guide_report.data(), guide_report.size(),
                                merged_guide));
    assert((merged_guide.buttons & kXboxBtnBack) != 0);
    assert((merged_guide.buttons & kXboxBtnGuide) != 0);
    assert(merged_guide.guide_report_active);

    compact[15] = 0;
    assert(parse_xbox_bt_report(compact.data(), compact.size(),
                                merged_guide));
    assert((merged_guide.buttons & kXboxBtnBack) != 0);
    assert((merged_guide.buttons & kXboxBtnGuide) != 0);

    guide_report[1] = 0x00;
    assert(parse_xbox_bt_report(guide_report.data(), guide_report.size(),
                                merged_guide));
    assert((merged_guide.buttons & kXboxBtnBack) != 0);
    assert((merged_guide.buttons & kXboxBtnGuide) == 0);
    assert(!merged_guide.guide_report_active);
}

void test_xbox_persona_and_capture_combos()
{
    PersonaComboFilter filter;

    (void)filter.process(kXboxBtnGuide, kXboxBtnGuide, kXboxBtnBack, 0);
    PersonaComboFilterResult r =
        filter.process(kXboxBtnGuide | kXboxBtnBack, kXboxBtnGuide,
                       kXboxBtnBack, 8);
    assert(r.request == PersonaComboRequest::None);
    assert((r.buttons & kXboxBtnGuide) != 0);
    assert((r.buttons & kXboxBtnBack) == 0);
    r = filter.process(0, kXboxBtnGuide, kXboxBtnBack, 16);
    assert(r.request == PersonaComboRequest::SwitchPro);

    filter.reset();
    (void)filter.process(kXboxBtnGuide, kXboxBtnGuide, kXboxBtnBack, 0);
    (void)filter.process(kXboxBtnGuide | kXboxBtnBack, kXboxBtnGuide,
                         kXboxBtnBack, 8);
    (void)filter.process(kXboxBtnGuide, kXboxBtnGuide, kXboxBtnBack, 16);
    (void)filter.process(kXboxBtnGuide | kXboxBtnBack, kXboxBtnGuide,
                         kXboxBtnBack, 24);
    (void)filter.process(kXboxBtnGuide, kXboxBtnGuide, kXboxBtnBack, 32);
    r = filter.process(0, kXboxBtnGuide, kXboxBtnBack, 40);
    assert(r.request == PersonaComboRequest::Switch2Pro);

    XboxControllerInput capture{};
    capture.buttons = kXboxBtnBack | kXboxBtnMenu;
    if ((capture.buttons & (kXboxBtnBack | kXboxBtnMenu)) ==
        (kXboxBtnBack | kXboxBtnMenu)) {
        capture.buttons &= ~(kXboxBtnBack | kXboxBtnMenu);
    }
    SwitchProState state = map_xbox_to_switch(capture);
    state.buttons |= kNsCapture;
    assert((state.buttons & kNsCapture) != 0);
    assert((state.buttons & kNsMinus) == 0);
    assert((state.buttons & kNsPlus) == 0);
}

void test_xbox_rumble_output_builder()
{
    HapticCommand haptics{};
    haptics.active = true;
    haptics.left_motor = 10;
    haptics.right_motor = 20;
    haptics.left_pulse = 30;
    haptics.right_pulse = 40;

    std::array<uint8_t, kXboxRumbleReportSize> out{};
    const std::size_t len =
        build_xbox_rumble_output(haptics, out.data(), out.size());
    assert(len == kXboxRumbleReportSize);
    assert(out[0] == kXboxRumbleReportId);
    assert(out[1] == 0x0f);
    assert(out[2] == 0);
    assert(out[3] == 0);
    assert(out[4] == 7);
    assert(out[5] == 14);
    assert(out[6] == 0xff);
    assert(out[8] == 0xeb);

    haptics.left_motor = 120;
    haptics.right_motor = 80;
    haptics.left_pulse = 255;
    haptics.right_pulse = 128;
    const std::size_t strong_len =
        build_xbox_rumble_output(haptics, out.data(), out.size());
    assert(strong_len == kXboxRumbleReportSize);
    assert(out[2] == 0);
    assert(out[3] == 0);
    assert(out[4] == 96);
    assert(out[5] == 66);

    haptics = {};
    const std::size_t stop_len =
        build_xbox_rumble_output(haptics, out.data(), out.size());
    assert(stop_len == kXboxRumbleReportSize);
    assert(out[0] == kXboxRumbleReportId);
    assert(out[6] == 0);
    assert(out[8] == 0);
}

void test_dualsense_report_parser()
{
    uint8_t report[kDualSenseBt31PayloadSize + 3u]{};
    report[0] = 0xa1;
    report[1] = 0x31;
    uint8_t *payload = report + 3;
    payload[0] = 1;
    payload[1] = 2;
    payload[2] = 3;
    payload[3] = 4;
    payload[4] = 64;
    payload[5] = 65;
    payload[7] = 0x20 | static_cast<uint8_t>(PlayStationDpad::DownLeft);
    payload[8] = 0x01 | 0x08 | 0x10 | 0x80;
    payload[9] = 0x01 | 0x02;
    write_i16_le(payload + 15, 1000);
    write_i16_le(payload + 17, -2000);
    write_i16_le(payload + 19, 3000);
    write_i16_le(payload + 21, 8192);
    write_i16_le(payload + 23, -4096);
    write_i16_le(payload + 25, 2048);
    payload[53] = 0x01 | 0x02 | 0x04;
    payload[54] = 0x01;

    PlayStationInput parsed{};
    assert(parse_dualsense_bt_report(report, sizeof(report), parsed));
    assert(parsed.left_x == 1);
    assert(parsed.left_y == 2);
    assert(parsed.right_x == 3);
    assert(parsed.right_y == 4);
    assert(parsed.l2 == 64);
    assert(parsed.r2 == 65);
    assert(parsed.dpad == PlayStationDpad::DownLeft);
    assert((parsed.buttons & kPsCross) != 0);
    assert((parsed.buttons & kPsL1) != 0);
    assert((parsed.buttons & kPsR2Button) != 0);
    assert((parsed.buttons & kPsCreate) != 0);
    assert((parsed.buttons & kPsR3) != 0);
    assert((parsed.buttons & kPsButton) != 0);
    assert((parsed.buttons & kPsTouchpadClick) != 0);
    assert(parsed.has_imu);
    assert(parsed.gyro_x == 1000);
    assert(parsed.gyro_z == -2000);
    assert(parsed.gyro_y == 3000);
    assert(parsed.accel_x == 8192);
    assert(parsed.accel_y == -4096);
    assert(parsed.accel_z == 2048);
    assert(parsed.headphones_connected);
    assert(parsed.mic_connected);
    assert(parsed.mic_muted);
    assert(parsed.external_mic_active);

    const SwitchProState state = map_playstation_to_switch(parsed);
    assert((state.buttons & kNsB) != 0);
    assert((state.buttons & kNsCapture) != 0);
}

void test_dualsense_output_report()
{
    DualSenseOutputState state;
    state.reset();
    assert(!state.motors_active());

    std::array<uint8_t, kDualSenseOutputStateBlockSize> state_block{};
    state.copy_state_block(state_block.data(), state_block.size());
    assert(state_block[4] == 0x78);
    assert(state_block[6] == 0x25);
    assert(state_block[8] == 0x00);
    assert((state_block[9] & 0x10) == 0);

    state.set_mic_muted(true);
    assert(state.mic_muted());
    state.copy_state_block(state_block.data(), state_block.size());
    assert((state_block[1] & 0x03) == 0x03);
    assert(state_block[8] == 0x01);
    assert((state_block[9] & 0x0f) == 0x0f);
    assert((state_block[9] & 0x10) != 0);

    state.set_mic_muted(false);
    assert(!state.mic_muted());
    state.copy_state_block(state_block.data(), state_block.size());
    assert(state_block[8] == 0x00);
    assert((state_block[9] & 0x10) == 0);

    HapticCommand command{};
    command.left_pulse = 160;
    command.right_motor = 32;
    command.active = true;
    state.apply_haptics(command);
    assert(state.motors_active());

    const DualSenseBtOutputReport report = state.build_report(3);
    assert(report[0] == 0x31);
    assert(report[1] == 0x30);
    assert(report[2] == 0x10);
    assert(report[3] & 0x01);
    assert(report[3] & 0x02);
    assert(report[3 + 2] == 32);
    assert(report[3 + 3] > 0);

    const uint32_t expected_crc =
        dualsense_output_crc32(report.data(), report.size() - 4u);
    const uint32_t actual_crc =
        static_cast<uint32_t>(report[74]) |
        (static_cast<uint32_t>(report[75]) << 8u) |
        (static_cast<uint32_t>(report[76]) << 16u) |
        (static_cast<uint32_t>(report[77]) << 24u);
    assert(actual_crc == expected_crc);

    state.clear_motors();
    assert(!state.motors_active());
}

void test_dualshock4_bt_input_parser()
{
    std::array<uint8_t, kDualShock4Bt11ReportSize + 1u> report{};
    report[0] = 0xa1;
    report[1] = 0x11;
    uint8_t *wire = report.data() + 1u;
    wire[3] = 11;
    wire[4] = 22;
    wire[5] = 33;
    wire[6] = 44;
    wire[7] = 0x10 | 0x20 | static_cast<uint8_t>(PlayStationDpad::UpRight);
    wire[8] = 0x01 | 0x02 | 0x04 | 0x08 | 0x10 | 0x20 | 0x40 | 0x80;
    wire[9] = 0x01 | 0x02;
    wire[10] = 77;
    wire[11] = 88;
    write_i16_le(wire + 15, 100);
    write_i16_le(wire + 17, -200);
    write_i16_le(wire + 19, 300);
    write_i16_le(wire + 21, 4000);
    write_i16_le(wire + 23, -5000);
    write_i16_le(wire + 25, 6000);

    PlayStationInput parsed{};
    assert(parse_dualshock4_bt_report(report.data(), report.size(), parsed));
    assert(parsed.left_x == 11);
    assert(parsed.left_y == 22);
    assert(parsed.right_x == 33);
    assert(parsed.right_y == 44);
    assert(parsed.l2 == 77);
    assert(parsed.r2 == 88);
    assert(parsed.dpad == PlayStationDpad::UpRight);
    assert((parsed.buttons & kPsSquare) != 0);
    assert((parsed.buttons & kPsCross) != 0);
    assert((parsed.buttons & kPsL1) != 0);
    assert((parsed.buttons & kPsR1) != 0);
    assert((parsed.buttons & kPsL2Button) != 0);
    assert((parsed.buttons & kPsR2Button) != 0);
    assert((parsed.buttons & kPsCreate) != 0);
    assert((parsed.buttons & kPsOptions) != 0);
    assert((parsed.buttons & kPsL3) != 0);
    assert((parsed.buttons & kPsR3) != 0);
    assert((parsed.buttons & kPsButton) != 0);
    assert((parsed.buttons & kPsTouchpadClick) != 0);
    assert(parsed.has_imu);
    assert(parsed.gyro_x == 100);
    assert(parsed.gyro_y == -200);
    assert(parsed.gyro_z == 300);
    assert(parsed.accel_x == 4000);
    assert(parsed.accel_y == -5000);
    assert(parsed.accel_z == 6000);

    const SwitchProState state = map_playstation_to_switch(parsed);
    assert((state.buttons & kNsB) != 0);
    assert((state.buttons & kNsY) != 0);
    assert((state.buttons & kNsMinus) != 0);
    assert((state.buttons & kNsPlus) != 0);
    assert((state.buttons & kNsHome) != 0);
    assert((state.buttons & kNsCapture) != 0);
    assert((state.buttons & kNsUp) != 0);
    assert((state.buttons & kNsRight) != 0);

    std::array<uint8_t, kDualShock4BtMinimalReportSize> minimal{};
    minimal[0] = 0x01;
    minimal[1] = 1;
    minimal[2] = 2;
    minimal[3] = 3;
    minimal[4] = 4;
    minimal[5] = 0x40 | static_cast<uint8_t>(PlayStationDpad::Down);
    minimal[6] = 0x80;
    minimal[7] = 0x01;
    minimal[8] = 9;
    minimal[9] = 10;

    PlayStationInput mini{};
    assert(parse_dualshock4_bt_report(minimal.data(), minimal.size(), mini));
    assert(mini.left_x == 1);
    assert(mini.right_y == 4);
    assert(mini.l2 == 9);
    assert(mini.r2 == 10);
    assert(mini.dpad == PlayStationDpad::Down);
    assert((mini.buttons & kPsCircle) != 0);
    assert((mini.buttons & kPsR3) != 0);
    assert((mini.buttons & kPsButton) != 0);
    assert(!mini.has_imu);

    std::array<uint8_t, 64> usb_shaped{};
    usb_shaped[0] = 0x01;
    usb_shaped[1] = 21;
    usb_shaped[2] = 22;
    usb_shaped[3] = 23;
    usb_shaped[4] = 24;
    usb_shaped[5] = 0x80 | static_cast<uint8_t>(PlayStationDpad::Left);
    usb_shaped[6] = 0x01 | 0x20;
    usb_shaped[7] = 0x02;
    usb_shaped[8] = 31;
    usb_shaped[9] = 32;
    write_i16_le(usb_shaped.data() + 13, -700);
    write_i16_le(usb_shaped.data() + 15, 800);
    write_i16_le(usb_shaped.data() + 17, -900);
    write_i16_le(usb_shaped.data() + 19, 1000);
    write_i16_le(usb_shaped.data() + 21, -1100);
    write_i16_le(usb_shaped.data() + 23, 1200);

    PlayStationInput usb_full{};
    assert(parse_dualshock4_bt_report(usb_shaped.data(), usb_shaped.size(),
                                      usb_full));
    assert(usb_full.left_x == 21);
    assert(usb_full.right_y == 24);
    assert(usb_full.dpad == PlayStationDpad::Left);
    assert((usb_full.buttons & kPsTriangle) != 0);
    assert((usb_full.buttons & kPsL1) != 0);
    assert((usb_full.buttons & kPsOptions) != 0);
    assert((usb_full.buttons & kPsTouchpadClick) != 0);
    assert(usb_full.l2 == 31);
    assert(usb_full.r2 == 32);
    assert(usb_full.has_imu);
    assert(usb_full.gyro_x == -700);
    assert(usb_full.gyro_y == 800);
    assert(usb_full.gyro_z == -900);
    assert(usb_full.accel_x == 1000);
    assert(usb_full.accel_y == -1100);
    assert(usb_full.accel_z == 1200);
}

void test_dualshock4_bt_output_report()
{
    DualShock4OutputState state;
    state.reset();
    assert(!state.motors_active());

    HapticCommand command{};
    command.left_motor = 32;
    command.right_pulse = 160;
    command.active = true;
    state.apply_haptics(command);
    assert(state.motors_active());

    const DualShock4BtOutputReport report = state.build_report();
    assert(report[0] == 0x11);
    assert(report[1] == 0xc4);
    assert((report[3] & 0x03u) == 0x03u);
    assert(report[6] > 0);
    assert(report[7] == 32);
    assert(report[10] == 0x10);

    const uint32_t expected_crc =
        dualshock4_output_crc32(report.data(), report.size() - 4u);
    const uint32_t actual_crc =
        static_cast<uint32_t>(report[74]) |
        (static_cast<uint32_t>(report[75]) << 8u) |
        (static_cast<uint32_t>(report[76]) << 16u) |
        (static_cast<uint32_t>(report[77]) << 24u);
    assert(actual_crc == expected_crc);

    state.clear_motors();
    assert(!state.motors_active());
}

void test_switch_usb_reports()
{
    SwitchUsbEmulator usb;
    PlayStationInput in{};
    in.has_imu = true;
    in.accel_x = 8192;
    in.accel_z = 8192;
    usb.set_state(map_playstation_to_switch(in));

    SwitchReport report{};
    assert(usb.build_usb_response(kSwitchUsbCmdHandshake, report));
    assert(report[0] == 0x81);
    assert(report[1] == 0x02);
    assert(report[3] == 0x03);

    assert(usb.build_usb_response(kSwitchUsbCmdConnStatus, report));
    assert(report[0] == 0x81);
    assert(report[1] == 0x01);
    assert(report[4] == 0x73);
    assert(report[5] == 0xdc);
    assert(report[6] == 0xe6);
    assert(report[7] == 0xe9);
    assert(report[8] == 0xb6);
    assert(report[9] == 0x98);

    assert(usb.build_subcommand_reply(kSwitchSubcmdRequestDeviceInfo,
                                      nullptr, 0, report));
    assert(report[0] == 0x21);
    assert(report[13] == 0x82);
    assert(report[14] == 0x02);
    assert(report[15] == 0x03);
    assert(report[17] == 0x03);
    assert(report[18] == 0x02);
    assert(report[19] == 0x98);
    assert(report[20] == 0xb6);
    assert(report[21] == 0xe9);
    assert(report[22] == 0xe6);
    assert(report[23] == 0xdc);
    assert(report[24] == 0x73);
    assert(report[25] == 0x01);
    assert(report[26] == 0x03);

    const uint8_t pairing_request_addr[] = {0x01};
    assert(usb.build_subcommand_reply(kSwitchSubcmdBtManualPairing,
                                      pairing_request_addr,
                                      sizeof(pairing_request_addr), report));
    assert(report[13] == 0x81);
    assert(report[14] == 0x01);
    assert(report[15] == 0x01);
    assert(report[16] == 0x73);
    assert(report[17] == 0xdc);
    assert(report[18] == 0xe6);
    assert(report[19] == 0xe9);
    assert(report[20] == 0xb6);
    assert(report[21] == 0x98);
    assert(report[23] == 0x25);
    assert(report[24] == 0x08);
    assert(std::memcmp(&report[25], "Pro Controller", 14) == 0);

    const uint8_t pairing_request_material[] = {0x02};
    assert(usb.build_subcommand_reply(kSwitchSubcmdBtManualPairing,
                                      pairing_request_material,
                                      sizeof(pairing_request_material), report));
    const uint8_t expected_material[] = {
        0xeb, 0xe0, 0xf9, 0xf6, 0xcc, 0xda, 0xd0, 0x2e,
        0x25, 0x30, 0x0f, 0x1f, 0x6f, 0x7f, 0x4c, 0x5d,
    };
    assert(report[15] == 0x02);
    assert(std::memcmp(&report[16], expected_material,
                       sizeof(expected_material)) == 0);

    const uint8_t pairing_request_done[] = {0x04};
    assert(usb.build_subcommand_reply(kSwitchSubcmdBtManualPairing,
                                      pairing_request_done,
                                      sizeof(pairing_request_done), report));
    assert(report[15] == 0x03);
    for (std::size_t i = 16; i < report.size(); ++i) {
        assert(report[i] == 0x00);
    }

    uint8_t spi_args[5] = {0x20, 0x60, 0x00, 0x00, 0x18};
    assert(usb.build_subcommand_reply(kSwitchSubcmdSpiRead, spi_args,
                                      sizeof(spi_args), report));
    assert(report[0] == 0x21);
    assert(report[13] == 0x90);
    assert(report[14] == 0x10);
    assert(report[15] == 0x20);
    assert(report[16] == 0x60);
    assert(report[19] == 0x18);
    assert(read_i16_le(&report[26]) == 0x4000);
    assert(read_i16_le(&report[38]) == 0x343b);

    spi_args[0] = 0x00;
    spi_args[1] = 0x60;
    spi_args[4] = 0x20;
    assert(usb.build_subcommand_reply(kSwitchSubcmdSpiRead, spi_args,
                                      sizeof(spi_args), report));
    assert(std::memcmp(&report[20], "000000000001", 12) == 0);
    assert(report[38] == 0x03);
    assert(report[39] == 0xa0);
    assert(report[47] == 0x01);

    uint8_t output[12] = {
        kSwitchOutputSubcommand, 0x00,
        0x34, 0xf0, 0x62, 0xe0,
        0x00, 0x01, 0x40, 0x40,
        kSwitchSubcmdSetPlayerLights, 0x0f,
    };
    const SwitchUsbOutputResult result = usb.handle_output_report(output, sizeof(output));
    assert(result.has_reply);
    assert(result.feedback.has_rumble);
    assert(result.feedback.haptics.active);
    assert(result.feedback.player_led_changed);
    assert(result.feedback.player_led_mask == 0x0f);
    assert(usb.player_led_mask() == 0x0f);

    const uint8_t pairing_output[] = {
        kSwitchOutputSubcommand, 0x08,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        kSwitchSubcmdBtManualPairing,
        0x01, 0x13, 0x64, 0xff, 0x05, 0x48, 0xc8,
        0x3c, 0x04, 0x08,
        'N', 'i', 'n', 't', 'e', 'n', 'd', 'o', ' ',
        'S', 'w', 'i', 't', 'c', 'h',
    };
    const SwitchUsbOutputResult pairing =
        usb.handle_output_report(pairing_output, sizeof(pairing_output));
    assert(pairing.has_reply);
    assert(pairing.has_switch2_console_addr);
    const uint8_t expected_addr[] = {0x13, 0x64, 0xff, 0x05, 0x48, 0xc8};
    assert(std::memcmp(pairing.switch2_console_addr_wire, expected_addr,
                       sizeof(expected_addr)) == 0);

    // Manual-pairing phase that lacks the console name must not be treated as a
    // wake target (anti false positive).
    const uint8_t pairing_no_name[] = {
        kSwitchOutputSubcommand, 0x09,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        kSwitchSubcmdBtManualPairing,
        0x01, 0x13, 0x64, 0xff, 0x05, 0x48, 0xc8,
    };
    const SwitchUsbOutputResult pairing_no_name_result =
        usb.handle_output_report(pairing_no_name, sizeof(pairing_no_name));
    assert(!pairing_no_name_result.has_switch2_console_addr);

    // A non-pairing subcommand never yields a wake target.
    const uint8_t device_info_output[] = {
        kSwitchOutputSubcommand, 0x0a,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        kSwitchSubcmdRequestDeviceInfo,
    };
    const SwitchUsbOutputResult device_info_result =
        usb.handle_output_report(device_info_output, sizeof(device_info_output));
    assert(!device_info_result.has_switch2_console_addr);
}

void test_switch2_wake_adv_builder()
{
    const uint8_t console_addr[] = {0x13, 0x64, 0xff, 0x05, 0x48, 0xc8};
    const Switch2WakeAdvData adv = switch2_wake_build_adv_data(console_addr);

    const uint8_t expected[] = {
        0x02, 0x01, 0x06,
        0x1b, 0xff, 0x53, 0x05,
        0x01, 0x00, 0x03, 0x7e, 0x05, 0x66, 0x20, 0x00, 0x01, 0x81,
        0x13, 0x64, 0xff, 0x05, 0x48, 0xc8,
        0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    static_assert(sizeof(expected) == kSwitch2WakeAdvDataSize);
    assert(std::memcmp(adv.data(), expected, sizeof(expected)) == 0);

    assert(switch2_wake_addr_valid(console_addr));
    const uint8_t all_zero[] = {0, 0, 0, 0, 0, 0};
    const uint8_t all_ff[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    assert(!switch2_wake_addr_valid(all_zero));
    assert(!switch2_wake_addr_valid(all_ff));
    assert(!switch2_wake_addr_valid(nullptr));
}

void test_switch_identity_runtime()
{
    // Default identity is the captured Pro Controller fallback.
    assert(switch_identity_bd_addr() == kSwitchProControllerBdAddrFallback);

    // A valid override (e.g. the Pico's own factory address) is accepted.
    const SwitchControllerBdAddr radio_identity{0x2c, 0xcf, 0x67, 0x12, 0x34, 0x56};
    switch_identity_set_bd_addr(radio_identity);
    assert(switch_identity_bd_addr() == radio_identity);

    // All-zero / all-ff are rejected (unusable addresses keep last value).
    switch_identity_set_bd_addr(SwitchControllerBdAddr{0, 0, 0, 0, 0, 0});
    assert(switch_identity_bd_addr() == radio_identity);
    switch_identity_set_bd_addr(
        SwitchControllerBdAddr{0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    assert(switch_identity_bd_addr() == radio_identity);

    // The USB Switch Pro persona must report the overridden identity. Device
    // info carries display order; conn-status carries reversed wire order.
    SwitchUsbEmulator usb;
    SwitchReport report{};
    assert(usb.build_subcommand_reply(kSwitchSubcmdRequestDeviceInfo, nullptr, 0,
                                      report));
    for (std::size_t i = 0; i < radio_identity.size(); ++i) {
        assert(report[19 + i] == radio_identity[i]);
    }
    assert(usb.build_usb_response(kSwitchUsbCmdConnStatus, report));
    for (std::size_t i = 0; i < radio_identity.size(); ++i) {
        assert(report[4 + i] == radio_identity[radio_identity.size() - 1 - i]);
    }

    // Restore the default so later tests that assume the fallback still pass.
    switch_identity_set_bd_addr(kSwitchProControllerBdAddrFallback);
    assert(switch_identity_bd_addr() == kSwitchProControllerBdAddrFallback);
}

void test_procon2_handshake_replies()
{
    const uint8_t ctrl_addr_wire[] = {0x56, 0x34, 0x12, 0x67, 0xcf, 0x2c};
    procon2_handshake_reset();

    // Non-handshake input yields no reply.
    const uint8_t junk[] = {0x09, 0x23, 0x00};
    assert(!procon2_is_handshake_request(junk, sizeof(junk)));
    assert(!procon2_build_reply(junk, sizeof(junk), ctrl_addr_wire).valid);

    // 0x03 console MAC announce -> ack reply (report id + device direction).
    const uint8_t mac_req[] = {0x03, 0x91, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x00,
                               0x01, 0x00, 0x13, 0x64, 0xff, 0x05, 0x48, 0xc8};
    assert(procon2_is_handshake_request(mac_req, sizeof(mac_req)));
    Procon2Reply mac_reply =
        procon2_build_reply(mac_req, sizeof(mac_req), ctrl_addr_wire);
    assert(mac_reply.valid);
    assert(!mac_reply.is_crypto_placeholder);
    assert(mac_reply.data[0] == kProcon2ReportMac);
    assert(mac_reply.data[1] == kProcon2DirDeviceReply);

    // 0x15 subcmd 0x01 business-card -> reply embeds controller's own wire addr.
    const uint8_t card_req[] = {0x15, 0x91, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x00,
                                0x00, 0x02, 0x13, 0x64, 0xff, 0x05, 0x48, 0xc8,
                                0x12, 0x64, 0xff, 0x05, 0x48, 0xc8};
    Procon2Reply card_reply =
        procon2_build_reply(card_req, sizeof(card_req), ctrl_addr_wire);
    assert(card_reply.valid);
    assert(!card_reply.is_crypto_placeholder);
    assert(card_reply.business_card_sent);
    assert(!card_reply.ltk_exchanged);
    assert(!card_reply.ltk_confirmed);
    assert(!card_reply.pairing_finalized);
    assert(card_reply.data[0] == kProcon2ReportPairing);
    assert(std::memcmp(&card_reply.data[11], ctrl_addr_wire, 6) == 0);

    // 0x15 subcmd 0x04 + 0x02 LTK challenge/response from
    // BLE-captures/procon2-usb-handshake.pcapng:
    //   A1 = 79b4dd213baee68dfa682766aa7cdbd8
    //   B1 = 5cf6ee792cdf05e1ba2b6325c41a5f10
    //   LTK = A1 XOR B1
    //   B2 = AES-128-ECB(reverse(A2), key=reverse(LTK))
    const uint8_t exchange_ltk_req[] = {
        0x15, 0x91, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00,
        0x79, 0xb4, 0xdd, 0x21, 0x3b, 0xae, 0xe6, 0x8d,
        0xfa, 0x68, 0x27, 0x66, 0xaa, 0x7c, 0xdb, 0xd8,
    };
    Procon2Reply ltk_reply =
        procon2_build_reply(exchange_ltk_req, sizeof(exchange_ltk_req), ctrl_addr_wire);
    assert(ltk_reply.valid);
    assert(!ltk_reply.is_crypto_placeholder);
    assert(!ltk_reply.business_card_sent);
    assert(ltk_reply.ltk_exchanged);
    assert(!ltk_reply.ltk_confirmed);
    assert(!ltk_reply.pairing_finalized);
    const uint8_t fixed_b1[] = {
        0x5c, 0xf6, 0xee, 0x79, 0x2c, 0xdf, 0x05, 0xe1,
        0xba, 0x2b, 0x63, 0x25, 0xc4, 0x1a, 0x5f, 0x10,
    };
    assert(std::memcmp(&ltk_reply.data[9], fixed_b1, sizeof(fixed_b1)) == 0);

    const uint8_t confirm_ltk_req[] = {
        0x15, 0x91, 0x00, 0x02, 0x00, 0x11, 0x00, 0x00, 0x00,
        0x99, 0xff, 0x1b, 0x0d, 0xa5, 0x3d, 0x73, 0xbc,
        0x65, 0x31, 0x71, 0x17, 0x2e, 0x46, 0x77, 0x4c,
    };
    Procon2Reply confirm_reply =
        procon2_build_reply(confirm_ltk_req, sizeof(confirm_ltk_req), ctrl_addr_wire);
    assert(confirm_reply.valid);
    assert(!confirm_reply.is_crypto_placeholder);
    assert(!confirm_reply.business_card_sent);
    assert(!confirm_reply.ltk_exchanged);
    assert(confirm_reply.ltk_confirmed);
    assert(!confirm_reply.pairing_finalized);
    const uint8_t expected_b2[] = {
        0xef, 0x49, 0xae, 0x18, 0xaa, 0x99, 0xc2, 0x6f,
        0x98, 0x93, 0x49, 0xc9, 0x3f, 0xca, 0x60, 0xcd,
    };
    assert(std::memcmp(&confirm_reply.data[9], expected_b2, sizeof(expected_b2)) == 0);

    const uint8_t finalize_req[] = {
        0x15, 0x91, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
    };
    Procon2Reply finalize_reply =
        procon2_build_reply(finalize_req, sizeof(finalize_req), ctrl_addr_wire);
    assert(finalize_reply.valid);
    assert(finalize_reply.len == 9u);
    assert(finalize_reply.pairing_finalized);
}

void test_switch2_pro_button_bytes()
{
    // Switch 2 Pro report 0x09 packs the kNs* bitfield verbatim into 3 bytes.
    uint8_t out[3];
    switch2_pro_button_bytes(kNsB | kNsA | kNsY | kNsX | kNsR | kNsZR, out);
    assert(out[0] == 0x3fu); // bits 0..5 set
    assert(out[1] == 0x00u);
    assert(out[2] == 0x00u);

    switch2_pro_button_bytes(kNsPlus | kNsMinus | kNsLeftStick | kNsRightStick,
                             out);
    // bit6 Plus, bit7 RightStick, bit14 Minus, bit15 LeftStick.
    assert(out[0] == 0xc0u);
    assert(out[1] == 0xc0u);
    assert(out[2] == 0x00u);

    switch2_pro_button_bytes(kNsHome | kNsCapture | kNsDown | kNsUp | kNsLeft |
                                 kNsRight | kNsL | kNsZL,
                             out);
    // byte2 holds bits 16..20: Home(16), Capture(17), Down(8)->byte2? no.
    // Down=bit8 -> byte1 bit0; Up=bit11 -> byte1 bit3; etc. Verify by formula.
    uint32_t b = kNsHome | kNsCapture | kNsDown | kNsUp | kNsLeft | kNsRight |
                 kNsL | kNsZL;
    assert(out[0] == (uint8_t)(b & 0xffu));
    assert(out[1] == (uint8_t)((b >> 8) & 0xffu));
    assert(out[2] == (uint8_t)((b >> 16) & 0x1fu));
}

void test_switch_pro_bt_input_parser()
{
    SwitchProState state{};
    state.buttons = kNsB | kNsA | kNsY | kNsX | kNsL | kNsR | kNsZL |
                    kNsZR | kNsMinus | kNsPlus | kNsLeftStick |
                    kNsRightStick | kNsHome | kNsCapture | kNsUp |
                    kNsDown | kNsLeft | kNsRight;
    state.left_x = 0x123;
    state.left_y = 0x456;
    state.right_x = 0x789;
    state.right_y = 0xabc;
    state.has_imu = true;
    state.accel_x = -100;
    state.accel_y = 200;
    state.accel_z = -300;
    state.gyro_x = 400;
    state.gyro_y = -500;
    state.gyro_z = 600;

    const SwitchProInputReport body =
        build_switch_pro_input_report(state, 0x0a, true);
    std::array<uint8_t, kSwitchProInputReportSize + 1u> bt{};
    bt[0] = 0xa1u;
    std::memcpy(bt.data() + 1, body.data(), body.size());

    SwitchProState parsed{};
    assert(parse_switch_pro_bt_report(bt.data(), bt.size(), parsed));
    assert(parsed.buttons == state.buttons);
    assert(parsed.left_x == state.left_x);
    assert(parsed.left_y == state.left_y);
    assert(parsed.right_x == state.right_x);
    assert(parsed.right_y == state.right_y);
    assert(parsed.has_imu);
    assert(parsed.accel_x == state.accel_x);
    assert(parsed.accel_y == state.accel_y);
    assert(parsed.accel_z == state.accel_z);
    assert(parsed.gyro_x == state.gyro_x);
    assert(parsed.gyro_y == state.gyro_y);
    assert(parsed.gyro_z == state.gyro_z);

    SwitchProInputReport subcmd = body;
    subcmd[0] = kSwitchProSubcommandReplyReportId;
    assert(parse_switch_pro_input_report(subcmd.data(), subcmd.size(), parsed));
    assert(parsed.buttons == state.buttons);

    SwitchReport patched = body;
    write_switch_pro_report_buttons(patched, kNsHome);
    assert(parse_switch_pro_input_report(patched.data(), patched.size(), parsed));
    assert((parsed.buttons & kNsHome) != 0);
    assert((parsed.buttons & kNsCapture) == 0);
    assert((parsed.buttons & kNsA) == 0);

    uint8_t dualsense_report[kDualSenseBt31PayloadSize + 3u]{};
    dualsense_report[0] = 0xa1u;
    dualsense_report[1] = 0x31u;
    assert(!parse_switch_pro_bt_report(dualsense_report,
                                       sizeof(dualsense_report), parsed));
}

void test_procon2_extended_handshake()
{
    const uint8_t ctrl_addr_wire[] = {0x56, 0x34, 0x12, 0x67, 0xcf, 0x2c};
    procon2_handshake_reset();

    // 0x0c/0x01 feature info reflects the requested feature flags byte.
    // Request button+stick only (0x03): IMU/rumble lanes stay zero.
    const uint8_t feat_req[] = {0x0c, 0x91, 0x00, 0x01, 0x00, 0x01,
                                0x00, 0x00, 0x03};
    Procon2Reply feat =
        procon2_build_reply(feat_req, sizeof(feat_req), ctrl_addr_wire);
    assert(feat.valid);
    assert(feat.len == 20u);
    assert(feat.data[0] == 0x0cu && feat.data[1] == 0x01u);
    assert(feat.data[12] == 0x07u); // buttons
    assert(feat.data[13] == 0x07u); // sticks
    assert(feat.data[14] == 0x00u); // no IMU
    assert(feat.data[17] == 0x00u); // no rumble

    // 0x02/0x01 SPI read of device-info block 0x13000 -> 0x50-byte reply.
    const uint8_t spi_req[] = {0x02, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x01, 0x00};
    Procon2Reply spi =
        procon2_build_reply(spi_req, sizeof(spi_req), ctrl_addr_wire);
    assert(spi.valid);
    assert(spi.len == 0x50u);
    assert(spi.data[0] == 0x02u);
    // Block 0x13000 starts with 0x01 0x00 then the "HEK..." serial.
    assert(spi.data[16] == 0x01u);
    assert(std::memcmp(&spi.data[18], "HEK", 3) == 0);

    // 0x10/0x01 firmware info -> 20-byte reply.
    const uint8_t fw_req[] = {0x10, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    Procon2Reply fw =
        procon2_build_reply(fw_req, sizeof(fw_req), ctrl_addr_wire);
    assert(fw.valid);
    assert(fw.len == 20u);
    assert(fw.data[8] == 0x02u);

    // 0x16/0x01 init exchange -> 32-byte reply.
    const uint8_t init_req[] = {0x16, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    Procon2Reply init =
        procon2_build_reply(init_req, sizeof(init_req), ctrl_addr_wire);
    assert(init.valid);
    assert(init.len == 32u);

    // 0x07/0x01 matches the genuine 9-byte ack (header + one zero byte).
    const uint8_t cmd07_req[] = {0x07, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    Procon2Reply cmd07 =
        procon2_build_reply(cmd07_req, sizeof(cmd07_req), ctrl_addr_wire);
    assert(cmd07.valid);
    assert(cmd07.len == 9u);
    assert(cmd07.data[1] == 0x01u);
    assert(cmd07.data[8] == 0x00u);

    // A host-directed command with no modelled body still gets a header-only
    // ack (e.g. 0x09/0x07 in the genuine capture is 8 bytes).
    const uint8_t ack_req[] = {0x09, 0x91, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00};
    Procon2Reply ack =
        procon2_build_reply(ack_req, sizeof(ack_req), ctrl_addr_wire);
    assert(ack.valid);
    assert(ack.len == 8u);
    assert(ack.data[1] == 0x01u);
}

void test_switch2_auto_registration_policy()
{
    Switch2AutoRegistration policy;
    Switch2AutoRegistrationInputs in{};
    in.now_ms = 1000;
    in.usb_mounted = true;
    in.controller_connected = true;

    assert(policy.service(in) == Switch2AutoRegistrationAction::None);
    assert(!policy.active());

    policy.note_switch_console_seen(in.now_ms);
    assert(policy.service(in) == Switch2AutoRegistrationAction::None);
    assert(policy.active());
    assert(policy.blocks_audio_reconnect());

    in.now_ms += 249;
    assert(policy.service(in) == Switch2AutoRegistrationAction::None);

    in.now_ms += 1;
    assert(policy.service(in) ==
           Switch2AutoRegistrationAction::EnterSwitch2RegistrationPersona);

    in.switch2_persona_active = true;
    in.usb_mounted = false;
    in.usb_reconnect_pending = true;
    in.now_ms += 100;
    assert(policy.service(in) == Switch2AutoRegistrationAction::None);

    in.usb_mounted = true;
    in.usb_reconnect_pending = false;
    in.now_ms += 100;
    assert(policy.service(in) == Switch2AutoRegistrationAction::None);

    policy.note_pairing_finalized();
    in.registration_complete = true;
    in.now_ms += 1;
    assert(policy.service(in) == Switch2AutoRegistrationAction::None);
    in.now_ms += 250;
    assert(policy.service(in) ==
           Switch2AutoRegistrationAction::RevertToSwitchProPersona);
    assert(!policy.active());

    // Cooldown prevents immediate re-entry after a completed/failed attempt.
    in.switch2_persona_active = false;
    in.registration_complete = false;
    policy.note_switch_console_seen(in.now_ms);
    in.now_ms += 1000;
    assert(policy.service(in) == Switch2AutoRegistrationAction::None);
    assert(!policy.active());

    // A persisted registration marker suppresses auto registration entirely.
    in.registration_complete = true;
    in.now_ms += 10u * 60u * 1000u;
    policy.note_switch_console_seen(in.now_ms);
    assert(policy.service(in) == Switch2AutoRegistrationAction::None);
    assert(!policy.active());

    // If enumeration never comes back after the persona switch, revert safely.
    policy.reset();
    in.registration_complete = false;
    in.usb_mounted = true;
    in.usb_reconnect_pending = false;
    in.switch2_persona_active = false;
    policy.note_switch_console_seen(in.now_ms);
    (void)policy.service(in);
    in.now_ms += 250;
    assert(policy.service(in) ==
           Switch2AutoRegistrationAction::EnterSwitch2RegistrationPersona);
    in.usb_mounted = false;
    in.usb_reconnect_pending = false;
    in.now_ms += 3000;
    assert(policy.service(in) ==
           Switch2AutoRegistrationAction::RevertToSwitchProPersona);
}

} // namespace

int main()
{
    test_button_mapping();
    test_persona_combo_filter();
    test_axis_and_motion_mapping();
    test_nrf5340_steam_motion_axis_parity();
    test_haptics();
    test_steam_controller_input_parser_and_mapping();
    test_steam_persona_combo_filter();
    test_steam_output_builders();
    test_steam_trackpad_haptic_policy();
    test_xbox_controller_input_parser_and_mapping();
    test_xbox_persona_and_capture_combos();
    test_xbox_rumble_output_builder();
    test_dualsense_report_parser();
    test_dualsense_output_report();
    test_dualshock4_bt_input_parser();
    test_dualshock4_bt_output_report();
    test_switch_usb_reports();
    test_switch2_wake_adv_builder();
    test_switch2_pro_button_bytes();
    test_switch_pro_bt_input_parser();
    test_procon2_extended_handshake();
    test_switch_identity_runtime();
    test_procon2_handshake_replies();
    test_switch2_auto_registration_policy();
    return 0;
}

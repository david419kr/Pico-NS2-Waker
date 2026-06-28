#include "ds5onswitch/dualsense_output.h"
#include "ds5onswitch/dualsense_report.h"
#include "ds5onswitch/haptics.h"
#include "ds5onswitch/mapper.h"
#include "ds5onswitch/procon2_handshake.h"
#include "ds5onswitch/switch_identity.h"
#include "ds5onswitch/switch_pro.h"
#include "ds5onswitch/switch_usb.h"
#include "ds5onswitch/switch2_wake.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

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
    assert(dualsense_gyro_to_switch(1024) == 1024);
    assert(dualsense_gyro_to_switch(-2048) == -2048);

    DualSenseInput in{};
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

    const SwitchProState state = map_dualsense_to_switch(in);
    assert(state.left_x == 0);
    assert(state.left_y == 0);
    assert(state.right_x == axis8_to_switch12(128));
    assert(state.right_y == 0x0fff);
    assert(state.has_imu);
    assert(state.accel_x == dualsense_accel_to_switch(-in.accel_z));
    assert(state.accel_y == dualsense_accel_to_switch(-in.accel_x));
    assert(state.accel_z == dualsense_accel_to_switch(in.accel_y));
    assert(state.gyro_x == dualsense_gyro_to_switch(-in.gyro_y));
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

void test_nrf5340_steam_motion_axis_parity()
{
    constexpr int16_t steam_gyro_x = 0x1234;
    constexpr int16_t steam_gyro_y = -0x0201;
    constexpr int16_t steam_gyro_z = 0x3456;
    constexpr int16_t steam_accel_x = -0x0102;
    constexpr int16_t steam_accel_y = 0x0203;
    constexpr int16_t steam_accel_z = -0x3040;

    DualSenseInput in{};
    in.has_imu = true;
    in.gyro_x = steam_gyro_x;
    in.gyro_y = static_cast<int16_t>(-steam_gyro_y);
    in.gyro_z = steam_gyro_z;
    in.accel_x = steam_accel_x;
    in.accel_y = steam_accel_z;
    in.accel_z = static_cast<int16_t>(-steam_accel_y);

    const SwitchProState state = map_dualsense_to_switch(in);
    assert(state.gyro_x == steam_gyro_y);
    assert(state.gyro_y == static_cast<int16_t>(-steam_gyro_x));
    assert(state.gyro_z == steam_gyro_z);

    // nRF5340 Switch Pro scales Steam accel separately. This checks the
    // source axis/sign selection before DualSense-to-Switch scaling.
    assert(state.accel_x == dualsense_accel_to_switch(steam_accel_y));
    assert(state.accel_y == dualsense_accel_to_switch(-steam_accel_x));
    assert(state.accel_z == dualsense_accel_to_switch(steam_accel_z));
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

void write_i16_le(uint8_t *p, int16_t value)
{
    const uint16_t raw = static_cast<uint16_t>(value);
    p[0] = static_cast<uint8_t>(raw);
    p[1] = static_cast<uint8_t>(raw >> 8u);
}

void test_dualsense_bt_input_parser()
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
    payload[7] = 0x20 | static_cast<uint8_t>(DualSenseDpad::DownLeft);
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

    DualSenseInput parsed{};
    assert(parse_dualsense_bt_report(report, sizeof(report), parsed));
    assert(parsed.left_x == 1);
    assert(parsed.left_y == 2);
    assert(parsed.right_x == 3);
    assert(parsed.right_y == 4);
    assert(parsed.l2 == 64);
    assert(parsed.r2 == 65);
    assert(parsed.dpad == DualSenseDpad::DownLeft);
    assert((parsed.buttons & kDs5Cross) != 0);
    assert((parsed.buttons & kDs5L1) != 0);
    assert((parsed.buttons & kDs5R2Button) != 0);
    assert((parsed.buttons & kDs5Create) != 0);
    assert((parsed.buttons & kDs5R3) != 0);
    assert((parsed.buttons & kDs5Ps) != 0);
    assert((parsed.buttons & kDs5TouchpadClick) != 0);
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

    const SwitchProState state = map_dualsense_to_switch(parsed);
    assert((state.buttons & kNsB) != 0);
    assert((state.buttons & kNsCapture) != 0);
}

void test_dualsense_bt_output_report()
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

    DualSenseHapticCommand command{};
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

void test_switch_usb_reports()
{
    SwitchUsbEmulator usb;
    DualSenseInput in{};
    in.has_imu = true;
    in.accel_x = 8192;
    in.accel_z = 8192;
    usb.set_state(map_dualsense_to_switch(in));

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

    // Non-handshake input yields no reply.
    const uint8_t junk[] = {0x09, 0x23, 0x00};
    assert(!procon2_is_handshake_request(junk, sizeof(junk)));
    assert(!procon2_build_reply(junk, sizeof(junk), ctrl_addr_wire).valid);

    // 0x03 console MAC announce -> ack reply (report id + device direction).
    const uint8_t mac_req[] = {0x03, 0x91, 0x0d, 0x00, 0x08, 0x00, 0x00,
                               0x01, 0x00, 0x13, 0x64, 0xff, 0x05, 0x48, 0xc8};
    assert(procon2_is_handshake_request(mac_req, sizeof(mac_req)));
    Procon2Reply mac_reply =
        procon2_build_reply(mac_req, sizeof(mac_req), ctrl_addr_wire);
    assert(mac_reply.valid);
    assert(!mac_reply.is_crypto_placeholder);
    assert(mac_reply.data[0] == kProcon2ReportMac);
    assert(mac_reply.data[1] == kProcon2DirDeviceReply);

    // 0x15 cmd 0x0001 business-card -> reply embeds controller's own wire addr.
    const uint8_t card_req[] = {0x15, 0x91, 0x01, 0x00, 0x0e, 0x00, 0x00, 0x02,
                                0x13, 0x64, 0xff, 0x05, 0x48, 0xc8,
                                0x12, 0x64, 0xff, 0x05, 0x48, 0xc8};
    Procon2Reply card_reply =
        procon2_build_reply(card_req, sizeof(card_req), ctrl_addr_wire);
    assert(card_reply.valid);
    assert(!card_reply.is_crypto_placeholder);
    assert(card_reply.data[0] == kProcon2ReportPairing);
    assert(std::memcmp(&card_reply.data[10], ctrl_addr_wire, 6) == 0);

    // 0x15 cmd 0x0004 crypto challenge -> placeholder reply, clearly flagged.
    const uint8_t crypto_req[] = {0x15, 0x91, 0x04, 0x00, 0x11, 0x00, 0x00,
                                  0x79, 0xb4, 0xdd, 0x21, 0x3b, 0xae, 0xe6, 0x8d,
                                  0xfa, 0x68, 0x27, 0x66, 0xaa, 0x7c, 0xdb, 0xd8};
    Procon2Reply crypto_reply =
        procon2_build_reply(crypto_req, sizeof(crypto_req), ctrl_addr_wire);
    assert(crypto_reply.valid);
    assert(crypto_reply.is_crypto_placeholder);
    assert(crypto_reply.data[0] == kProcon2ReportPairing);
}

} // namespace

int main()
{
    test_button_mapping();
    test_axis_and_motion_mapping();
    test_nrf5340_steam_motion_axis_parity();
    test_haptics();
    test_dualsense_bt_input_parser();
    test_dualsense_bt_output_report();
    test_switch_usb_reports();
    test_switch2_wake_adv_builder();
    test_switch_identity_runtime();
    test_procon2_handshake_replies();
    return 0;
}

#pragma once

#include "ds5onswitch/haptics.h"
#include "ds5onswitch/types.h"

#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

constexpr uint8_t kSwitchOutputSubcommand = 0x01;
constexpr uint8_t kSwitchOutputRumbleOnly = 0x10;
constexpr uint8_t kSwitchOutputUsb = 0x80;

constexpr uint8_t kSwitchSubcmdBtManualPairing = 0x01;
constexpr uint8_t kSwitchSubcmdRequestDeviceInfo = 0x02;
constexpr uint8_t kSwitchSubcmdSetReportMode = 0x03;
constexpr uint8_t kSwitchSubcmdSpiRead = 0x10;
constexpr uint8_t kSwitchSubcmdSpiWrite = 0x11;
constexpr uint8_t kSwitchSubcmdSpiErase = 0x12;
constexpr uint8_t kSwitchSubcmdSetPlayerLights = 0x30;
constexpr uint8_t kSwitchSubcmdGetPlayerLights = 0x31;
constexpr uint8_t kSwitchSubcmdStateOnly = 0x33;
constexpr uint8_t kSwitchSubcmdSetHomeLight = 0x38;
constexpr uint8_t kSwitchSubcmdEnableImu = 0x40;
constexpr uint8_t kSwitchSubcmdSetImuSensitivity = 0x41;
constexpr uint8_t kSwitchSubcmdWriteImuReg = 0x42;
constexpr uint8_t kSwitchSubcmdReadImuReg = 0x43;
constexpr uint8_t kSwitchSubcmdEnableVibration = 0x48;
constexpr uint8_t kSwitchSubcmdGetRegulatedVoltage = 0x50;

constexpr uint8_t kSwitchUsbCmdConnStatus = 0x01;
constexpr uint8_t kSwitchUsbCmdHandshake = 0x02;
constexpr uint8_t kSwitchUsbCmdBaudrate3M = 0x03;
constexpr uint8_t kSwitchUsbCmdNoTimeout = 0x04;
constexpr uint8_t kSwitchUsbCmdEnableTimeout = 0x05;
constexpr uint8_t kSwitchUsbCmdReset = 0x06;

struct SwitchUsbFeedback {
    bool has_rumble = false;
    bool player_led_changed = false;
    uint8_t player_led_mask = 0;
    SwitchRumblePacket left_rumble{};
    SwitchRumblePacket right_rumble{};
    DualSenseHapticCommand haptics{};
};

struct SwitchUsbOutputResult {
    bool has_reply = false;
    bool queue_full_report = false;
    SwitchReport reply{};
    SwitchUsbFeedback feedback{};
};

class SwitchUsbEmulator {
public:
    void set_state(const SwitchProState &state);
    const SwitchProState &state() const;

    SwitchReport build_input_report();
    bool build_usb_response(uint8_t command, SwitchReport &out) const;
    bool build_subcommand_reply(uint8_t subcommand, const uint8_t *args,
                                std::size_t args_size, SwitchReport &out);
    SwitchUsbOutputResult handle_output_report(const uint8_t *data,
                                               std::size_t size);

    bool imu_enabled() const;
    bool vibration_enabled() const;
    uint8_t player_led_mask() const;
    uint8_t active_report_id() const;

private:
    SwitchProState state_{};
    SwitchRumbleTranslator rumble_translator_{};
    uint8_t counter_ = 0;
    uint8_t player_led_mask_ = 0;
    uint8_t active_report_id_ = 0;
    bool imu_enabled_ = false;
    bool vibration_enabled_ = false;

    void handle_subcommand_state(uint8_t subcommand, const uint8_t *args,
                                 std::size_t args_size, SwitchUsbFeedback &feedback);
};

} // namespace ds5onswitch

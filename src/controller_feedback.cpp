#include "switch_bridge/controller_feedback.h"

#include "switch_bridge/dualshock4_output.h"
#include "switch_bridge/dualsense_audio.h"
#include "switch_bridge/classic_hid_bt.h"
#include "switch_bridge/dualsense_output.h"
#include "switch_bridge/steam_hog_bt.h"
#include "switch_bridge/switch_usb.h"
#include "switch_bridge/xbox_controller.h"

#include <cstddef>
#include <cstring>

namespace switchbridge {

namespace {

constexpr uint32_t kCueDurationMs = 96;
constexpr uint8_t kSwitchProRumbleReportId = kSwitchOutputRumbleOnly;
constexpr uint8_t kSwitchProWeakRumble[] = {0x00, 0x19, 0x40, 0x46};
constexpr uint8_t kSwitchProSilentRumble[] = {0x00, 0x01, 0x40, 0x40};

bool g_start_pending = false;
bool g_stop_pending = false;
ControllerFeedbackCue g_pending_cue = ControllerFeedbackCue::Connected;
uint32_t g_stop_at_ms = 0;
uint8_t g_dualsense_sequence = 0;
uint8_t g_switch_pro_rumble_counter = 0;

HapticCommand haptics_for_cue(ControllerFeedbackCue cue)
{
    HapticCommand command{};
    command.active = true;
    switch (cue) {
    case ControllerFeedbackCue::Connected:
        command.left_motor = 18;
        command.right_motor = 18;
        command.left_pulse = 36;
        command.right_pulse = 36;
        break;
    case ControllerFeedbackCue::WakeDisconnect:
        command.left_motor = 22;
        command.right_motor = 22;
        command.left_pulse = 44;
        command.right_pulse = 44;
        break;
    }
    return command;
}

SwitchUsbFeedback switch_feedback_for_haptics(
    const HapticCommand &command)
{
    SwitchUsbFeedback feedback{};
    feedback.has_rumble = true;
    feedback.haptics = command;
    return feedback;
}

bool send_dualsense_feedback(const HapticCommand &command)
{
    DualSenseOutputState state{};
    state.reset();
    state.set_mic_muted(dualsense_audio_mic_muted());
    if (command.active) {
        state.apply_haptics(command);
    } else {
        state.clear_motors();
    }
    return classic_hid_send_dualsense_output_report(
        state.build_report(g_dualsense_sequence++));
}

bool send_dualshock4_feedback(const HapticCommand &command)
{
    DualShock4OutputState state{};
    state.reset();
    if (command.active) {
        state.apply_haptics(command);
    } else {
        state.clear_motors();
    }
    const DualShock4BtOutputReport report = state.build_report();
    return classic_hid_send_raw_output_report(report.data(), report.size());
}

bool send_xbox_classic_feedback(const HapticCommand &command)
{
    uint8_t report[kXboxRumbleReportSize]{};
    const std::size_t len =
        build_xbox_rumble_output(command, report, sizeof(report));
    return len != 0 && classic_hid_send_raw_output_report(report, len);
}

bool send_switch_pro_feedback(bool active)
{
    uint8_t report[10]{};
    report[0] = kSwitchProRumbleReportId;
    report[1] = g_switch_pro_rumble_counter++;
    const uint8_t *packet = active ? kSwitchProWeakRumble
                                   : kSwitchProSilentRumble;
    std::memcpy(report + 2u, packet, sizeof(kSwitchProWeakRumble));
    std::memcpy(report + 6u, packet, sizeof(kSwitchProWeakRumble));
    return classic_hid_send_raw_output_report(report, sizeof(report));
}

bool send_classic_feedback(const HapticCommand &command)
{
    if (!classic_hid_is_connected()) {
        return false;
    }

    switch (classic_hid_controller_kind()) {
    case BtControllerKind::DualSense:
        return send_dualsense_feedback(command);
    case BtControllerKind::DualShock4:
        return send_dualshock4_feedback(command);
    case BtControllerKind::SwitchPro:
        return send_switch_pro_feedback(command.active);
    case BtControllerKind::Xbox:
        return send_xbox_classic_feedback(command);
    case BtControllerKind::Unknown:
        return false;
    }
    return false;
}

bool send_hogp_feedback(const HapticCommand &command)
{
    if (!steam_hog_bt_is_connected()) {
        return false;
    }
    return steam_hog_bt_send_switch_feedback(
        switch_feedback_for_haptics(command));
}

bool send_feedback(const HapticCommand &command)
{
    const bool classic_sent = send_classic_feedback(command);
    const bool hogp_sent = send_hogp_feedback(command);
    return classic_sent || hogp_sent;
}

} // namespace

void controller_feedback_play(ControllerFeedbackCue cue)
{
    g_pending_cue = cue;
    g_start_pending = true;
}

void controller_feedback_service(uint32_t now_ms)
{
    if (g_start_pending) {
        g_start_pending = false;
        if (send_feedback(haptics_for_cue(g_pending_cue))) {
            g_stop_pending = true;
            g_stop_at_ms = now_ms + kCueDurationMs;
        }
    }

    if (g_stop_pending &&
        static_cast<int32_t>(now_ms - g_stop_at_ms) >= 0) {
        HapticCommand stop{};
        (void)send_feedback(stop);
        g_stop_pending = false;
    }
}

} // namespace switchbridge

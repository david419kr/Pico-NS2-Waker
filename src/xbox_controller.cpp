#include "ds5onswitch/xbox_controller.h"

#include <algorithm>
#include <cstring>

namespace ds5onswitch {

namespace {

constexpr uint32_t kSwitchStickAxisMax = 0x0fffu;
constexpr uint16_t kTriggerButtonThreshold = 30;
constexpr uint16_t kXboxTriggerMask = 0x03ff;
constexpr uint8_t kXboxRumbleAllMotors = 0x0f;
constexpr uint8_t kXboxRumbleSustain10Ms = 0xff;
constexpr uint8_t kXboxRumbleLoopCount = 0xeb;
constexpr uint8_t kXboxRumblePulseFloor = 64;
constexpr uint8_t kXboxRumbleMax = 96;

uint16_t read_u16_le(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8u);
}

uint16_t axis16_to_switch12(uint16_t raw, bool invert = false)
{
    uint32_t value =
        (static_cast<uint32_t>(raw) * kSwitchStickAxisMax + 32767u) / 65535u;
    if (value > kSwitchStickAxisMax) {
        value = kSwitchStickAxisMax;
    }
    if (invert) {
        value = kSwitchStickAxisMax - value;
    }
    return static_cast<uint16_t>(value);
}

void add_hat(uint32_t &buttons, uint8_t hat)
{
    switch (hat & 0x0fu) {
    case 1:
        buttons |= kXboxBtnDpadUp;
        break;
    case 2:
        buttons |= kXboxBtnDpadUp | kXboxBtnDpadRight;
        break;
    case 3:
        buttons |= kXboxBtnDpadRight;
        break;
    case 4:
        buttons |= kXboxBtnDpadRight | kXboxBtnDpadDown;
        break;
    case 5:
        buttons |= kXboxBtnDpadDown;
        break;
    case 6:
        buttons |= kXboxBtnDpadDown | kXboxBtnDpadLeft;
        break;
    case 7:
        buttons |= kXboxBtnDpadLeft;
        break;
    case 8:
        buttons |= kXboxBtnDpadLeft | kXboxBtnDpadUp;
        break;
    default:
        break;
    }
}

void set_button(uint32_t &buttons, uint8_t byte, uint8_t bit,
                XboxControllerButton button)
{
    if ((byte & (1u << bit)) != 0) {
        buttons |= button;
    }
}

void set_button(uint32_t &buttons, uint16_t bits, uint8_t bit,
                XboxControllerButton button)
{
    if ((bits & (1u << bit)) != 0) {
        buttons |= button;
    }
}

uint8_t rumble_mix(uint8_t body, uint8_t pulse)
{
    const uint16_t body_scaled =
        (static_cast<uint16_t>(body) * 70u + 50u) / 100u;
    const uint16_t pulse_scaled = pulse > kXboxRumblePulseFloor ?
        ((static_cast<uint16_t>(pulse - kXboxRumblePulseFloor) * 15u + 50u) /
         100u) :
        0u;
    const uint16_t mixed = body_scaled + pulse_scaled;
    return static_cast<uint8_t>(
        std::min<uint16_t>(mixed, kXboxRumbleMax));
}

bool parse_prefixed_input_report(const uint8_t *wire, std::size_t wire_size,
                                 XboxControllerInput &parsed)
{
    if (wire_size < kXboxInputReportMinSize ||
        (wire[0] != kXboxInputReportId && wire[0] != 0x00u)) {
        return false;
    }

    parsed.left_x = read_u16_le(&wire[1]);
    parsed.left_y = read_u16_le(&wire[3]);
    parsed.right_x = read_u16_le(&wire[5]);
    parsed.right_y = read_u16_le(&wire[7]);
    parsed.left_trigger = read_u16_le(&wire[9]) & kXboxTriggerMask;
    parsed.right_trigger = read_u16_le(&wire[11]) & kXboxTriggerMask;

    add_hat(parsed.buttons, wire[13]);

    const uint8_t b0 = wire[14];
    const uint8_t b1 = wire[15];
    if (wire_size == kXboxInputReportMinSize) {
        set_button(parsed.buttons, b0, 0, kXboxBtnA);
        set_button(parsed.buttons, b0, 1, kXboxBtnB);
        set_button(parsed.buttons, b0, 2, kXboxBtnX);
        set_button(parsed.buttons, b0, 3, kXboxBtnY);
        set_button(parsed.buttons, b0, 4, kXboxBtnLeftBumper);
        set_button(parsed.buttons, b0, 5, kXboxBtnRightBumper);
        set_button(parsed.buttons, b0, 6, kXboxBtnBack);
        set_button(parsed.buttons, b0, 7, kXboxBtnMenu);
        set_button(parsed.buttons, b1, 0, kXboxBtnLeftStick);
        set_button(parsed.buttons, b1, 1, kXboxBtnRightStick);
        set_button(parsed.buttons, b1, 2, kXboxBtnGuide);
    } else {
        const uint8_t b2 = wire_size > 16u ? wire[16] : 0u;
        set_button(parsed.buttons, b0, 0, kXboxBtnA);
        set_button(parsed.buttons, b0, 1, kXboxBtnB);
        set_button(parsed.buttons, b0, 3, kXboxBtnX);
        set_button(parsed.buttons, b0, 4, kXboxBtnY);
        set_button(parsed.buttons, b0, 6, kXboxBtnLeftBumper);
        set_button(parsed.buttons, b0, 7, kXboxBtnRightBumper);
        if ((b1 & (1u << 2)) != 0 || (b2 & (1u << 0)) != 0) {
            parsed.buttons |= kXboxBtnBack;
        }
        set_button(parsed.buttons, b1, 3, kXboxBtnMenu);
        set_button(parsed.buttons, b1, 4, kXboxBtnGuide);
        set_button(parsed.buttons, b1, 5, kXboxBtnLeftStick);
        set_button(parsed.buttons, b1, 6, kXboxBtnRightStick);
    }
    return true;
}

bool parse_xbox_hogp_payload(const uint8_t *payload, std::size_t payload_size,
                             XboxControllerInput &parsed)
{
    if (payload == nullptr || payload_size < 15u) {
        return false;
    }

    parsed.left_x = read_u16_le(&payload[0]);
    parsed.left_y = read_u16_le(&payload[2]);
    parsed.right_x = read_u16_le(&payload[4]);
    parsed.right_y = read_u16_le(&payload[6]);
    parsed.left_trigger = read_u16_le(&payload[8]) & kXboxTriggerMask;
    parsed.right_trigger = read_u16_le(&payload[10]) & kXboxTriggerMask;

    add_hat(parsed.buttons, payload[12]);

    const uint16_t buttons = read_u16_le(&payload[13]);
    set_button(parsed.buttons, buttons, 0, kXboxBtnA);
    set_button(parsed.buttons, buttons, 1, kXboxBtnB);
    set_button(parsed.buttons, buttons, 3, kXboxBtnX);
    set_button(parsed.buttons, buttons, 4, kXboxBtnY);
    set_button(parsed.buttons, buttons, 6, kXboxBtnLeftBumper);
    set_button(parsed.buttons, buttons, 7, kXboxBtnRightBumper);
    set_button(parsed.buttons, buttons, 10, kXboxBtnBack);
    set_button(parsed.buttons, buttons, 11, kXboxBtnMenu);
    set_button(parsed.buttons, buttons, 12, kXboxBtnGuide);
    set_button(parsed.buttons, buttons, 13, kXboxBtnLeftStick);
    set_button(parsed.buttons, buttons, 14, kXboxBtnRightStick);

    // Some Xbox BLE firmware exposes Share/Record separately. For this
    // firmware's Switch mapping it is equivalent to Back/View.
    if (payload_size > 15u && (payload[15] & 0x01u) != 0) {
        parsed.buttons |= kXboxBtnBack;
    }
    return true;
}

} // namespace

bool parse_xbox_bt_report(const uint8_t *data, std::size_t size,
                          XboxControllerInput &out)
{
    if (data == nullptr || size == 0) {
        return false;
    }

    const uint8_t *wire = data;
    std::size_t wire_size = size;
    if (wire[0] == 0xa1u && wire_size != kXboxElite2InputPayloadSize) {
        if (wire_size < 2u) {
            return false;
        }
        ++wire;
        --wire_size;
    }

    if (wire_size == 2u && wire[0] == kXboxGuideReportId) {
        if (wire_size < 2u) {
            return false;
        }
        if ((wire[1] & 0x01u) != 0) {
            out.guide_report_active = true;
            out.buttons |= kXboxBtnGuide;
        } else {
            out.guide_report_active = false;
            out.buttons &= ~kXboxBtnGuide;
        }
        return true;
    }

    XboxControllerInput parsed{};
    bool parsed_ok = false;
    if (wire_size == kXboxElite2InputPayloadSize) {
        parsed_ok = parse_xbox_hogp_payload(wire, wire_size, parsed);
    } else if (wire_size >= kXboxElite2InputReportSize &&
               (wire[0] == kXboxInputReportId || wire[0] == 0x00u)) {
        parsed_ok = parse_xbox_hogp_payload(wire + 1u, wire_size - 1u, parsed);
    } else {
        parsed_ok = parse_prefixed_input_report(wire, wire_size, parsed);
    }
    if (!parsed_ok) {
        return false;
    }

    parsed.guide_report_active = out.guide_report_active;
    if (parsed.guide_report_active) {
        parsed.buttons |= kXboxBtnGuide;
    }
    out = parsed;
    return true;
}

SwitchProState map_xbox_to_switch(const XboxControllerInput &input)
{
    SwitchProState state{};
    state.left_x = axis16_to_switch12(input.left_x);
    state.left_y = axis16_to_switch12(input.left_y, true);
    state.right_x = axis16_to_switch12(input.right_x);
    state.right_y = axis16_to_switch12(input.right_y, true);

    if ((input.buttons & kXboxBtnA) != 0) {
        state.buttons |= kNsB;
    }
    if ((input.buttons & kXboxBtnB) != 0) {
        state.buttons |= kNsA;
    }
    if ((input.buttons & kXboxBtnX) != 0) {
        state.buttons |= kNsY;
    }
    if ((input.buttons & kXboxBtnY) != 0) {
        state.buttons |= kNsX;
    }
    if ((input.buttons & kXboxBtnLeftBumper) != 0) {
        state.buttons |= kNsL;
    }
    if ((input.buttons & kXboxBtnRightBumper) != 0) {
        state.buttons |= kNsR;
    }
    if (input.left_trigger > kTriggerButtonThreshold) {
        state.buttons |= kNsZL;
    }
    if (input.right_trigger > kTriggerButtonThreshold) {
        state.buttons |= kNsZR;
    }
    if ((input.buttons & kXboxBtnBack) != 0) {
        state.buttons |= kNsMinus;
    }
    if ((input.buttons & kXboxBtnMenu) != 0) {
        state.buttons |= kNsPlus;
    }
    if ((input.buttons & kXboxBtnLeftStick) != 0) {
        state.buttons |= kNsLeftStick;
    }
    if ((input.buttons & kXboxBtnRightStick) != 0) {
        state.buttons |= kNsRightStick;
    }
    if ((input.buttons & kXboxBtnGuide) != 0) {
        state.buttons |= kNsHome;
    }
    if ((input.buttons & kXboxBtnDpadUp) != 0) {
        state.buttons |= kNsUp;
    }
    if ((input.buttons & kXboxBtnDpadRight) != 0) {
        state.buttons |= kNsRight;
    }
    if ((input.buttons & kXboxBtnDpadDown) != 0) {
        state.buttons |= kNsDown;
    }
    if ((input.buttons & kXboxBtnDpadLeft) != 0) {
        state.buttons |= kNsLeft;
    }

    return state;
}

std::size_t build_xbox_rumble_output(const DualSenseHapticCommand &haptics,
                                     uint8_t *out, std::size_t out_size)
{
    if (out == nullptr || out_size < kXboxRumbleReportSize) {
        return 0;
    }

    std::memset(out, 0, kXboxRumbleReportSize);
    out[0] = kXboxRumbleReportId;
    out[1] = kXboxRumbleAllMotors;
    out[2] = 0;
    out[3] = 0;
    out[4] = rumble_mix(haptics.left_motor, haptics.left_pulse);
    out[5] = rumble_mix(haptics.right_motor, haptics.right_pulse);
    out[6] = haptics.active ? kXboxRumbleSustain10Ms : 0u;
    out[7] = 0;
    out[8] = haptics.active ? kXboxRumbleLoopCount : 0u;
    return kXboxRumbleReportSize;
}

} // namespace ds5onswitch

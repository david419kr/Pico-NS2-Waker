#include "switch_bridge/steam_controller.h"

#include <cstring>

namespace switchbridge {

namespace {

constexpr int16_t kSwitchAccelCountsPerG = 4096;
constexpr int16_t kSwitchAccelDeadzoneCounts = 96;
constexpr int32_t kSteamToSwitchAccelDivisor = 4;
constexpr int32_t kSteamTrackpadDpadDeadzone = 9000;
constexpr int32_t kSteamTrackpadMovePulseThreshold = 1800;
constexpr uint32_t kSteamTrackpadMovePulseIntervalMs = 22;
constexpr std::size_t kSteamFeatureCommandSize = 64;
constexpr uint16_t kSteamBackAssignTriggerThreshold = 0x4000;
constexpr uint32_t kSteamBackAssignTimeoutMs = 8000;
constexpr uint32_t kSteamBackButtonMask =
    kSteamBtnL4 | kSteamBtnR4 | kSteamBtnL5 | kSteamBtnR5;

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

uint8_t trackpad_dpad_mask(SteamDpadDir dir)
{
    switch (dir) {
    case SteamDpadDir::N:
        return 0x01u;
    case SteamDpadDir::NE:
        return 0x01u | 0x02u;
    case SteamDpadDir::E:
        return 0x02u;
    case SteamDpadDir::SE:
        return 0x04u | 0x02u;
    case SteamDpadDir::S:
        return 0x04u;
    case SteamDpadDir::SW:
        return 0x04u | 0x08u;
    case SteamDpadDir::W:
        return 0x08u;
    case SteamDpadDir::NW:
        return 0x01u | 0x08u;
    case SteamDpadDir::None:
        return 0;
    }
    return 0;
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

SteamDpadDir button_dpad(uint32_t buttons)
{
    const bool up = (buttons & kSteamBtnDpadUp) != 0u;
    const bool right = (buttons & kSteamBtnDpadRight) != 0u;
    const bool down = (buttons & kSteamBtnDpadDown) != 0u;
    const bool left = (buttons & kSteamBtnDpadLeft) != 0u;

    if (up && right && !down && !left) {
        return SteamDpadDir::NE;
    }
    if (right && down && !up && !left) {
        return SteamDpadDir::SE;
    }
    if (down && left && !up && !right) {
        return SteamDpadDir::SW;
    }
    if (left && up && !right && !down) {
        return SteamDpadDir::NW;
    }
    if (up && !right && !down && !left) {
        return SteamDpadDir::N;
    }
    if (right && !up && !down && !left) {
        return SteamDpadDir::E;
    }
    if (down && !up && !right && !left) {
        return SteamDpadDir::S;
    }
    if (left && !up && !right && !down) {
        return SteamDpadDir::W;
    }
    return SteamDpadDir::None;
}

SteamDpadDir selected_steam_dpad(const SteamControllerInput &input)
{
    const SteamDpadDir left_pad = trackpad_dpad(
        input.left_pad_x, input.left_pad_y,
        (input.buttons & kSteamBtnLeftPadClick) != 0u);
    return left_pad != SteamDpadDir::None ? left_pad
                                          : button_dpad(input.buttons);
}

bool dpad_action(SteamDpadDir dir, SteamBackAction &out)
{
    switch (dir) {
    case SteamDpadDir::N:
        out = SteamBackAction::DpadUp;
        return true;
    case SteamDpadDir::E:
        out = SteamBackAction::DpadRight;
        return true;
    case SteamDpadDir::S:
        out = SteamBackAction::DpadDown;
        return true;
    case SteamDpadDir::W:
        out = SteamBackAction::DpadLeft;
        return true;
    default:
        return false;
    }
}

constexpr std::size_t steam_back_index(SteamBackButton button)
{
    return static_cast<std::size_t>(button);
}

bool steam_back_action_valid(SteamBackAction action)
{
    return action >= SteamBackAction::None &&
           action <= SteamBackAction::Capture;
}

void add_assignment_candidate(SteamBackAction candidate, SteamBackAction &found,
                              uint8_t &count)
{
    if (!steam_back_action_valid(candidate)) {
        return;
    }
    if (count == 0u) {
        found = candidate;
        count = 1u;
    } else if (found != candidate) {
        ++count;
    }
}

bool steam_left_trackpad_center_modifier(const SteamControllerInput &input)
{
    return (input.buttons & kSteamBtnLeftPadClick) != 0u &&
           trackpad_dpad(input.left_pad_x, input.left_pad_y, true) ==
               SteamDpadDir::None;
}

bool steam_back_button_from_bit(uint32_t bit, SteamBackButton &out)
{
    switch (bit) {
    case kSteamBtnL4:
        out = SteamBackButton::L4;
        return true;
    case kSteamBtnR4:
        out = SteamBackButton::R4;
        return true;
    case kSteamBtnL5:
        out = SteamBackButton::L5;
        return true;
    case kSteamBtnR5:
        out = SteamBackButton::R5;
        return true;
    default:
        return false;
    }
}

bool single_back_rising(uint32_t rising, SteamBackButton &out)
{
    uint32_t selected = 0;
    uint32_t candidates = rising & kSteamBackButtonMask;
    for (uint32_t bit = 1u; bit != 0u; bit <<= 1u) {
        if ((candidates & bit) == 0u) {
            continue;
        }
        if (selected != 0u) {
            return false;
        }
        selected = bit;
    }
    return selected != 0u && steam_back_button_from_bit(selected, out);
}

bool assignment_inputs_released(const SteamControllerInput &input)
{
    constexpr uint32_t input_mask =
        kSteamBtnA | kSteamBtnB | kSteamBtnX | kSteamBtnY |
        kSteamBtnSteamMenu | kSteamBtnRightStick | kSteamBtnMenu |
        kSteamBackButtonMask | kSteamBtnRightBumper | kSteamBtnDpadDown |
        kSteamBtnDpadRight | kSteamBtnDpadLeft | kSteamBtnDpadUp |
        kSteamBtnView | kSteamBtnLeftStick | kSteamBtnSteam |
        kSteamBtnLeftBumper | kSteamBtnRightPadClick |
        kSteamBtnLeftPadClick;

    return (input.buttons & input_mask) == 0u &&
           input.left_trigger < kSteamBackAssignTriggerThreshold &&
           input.right_trigger < kSteamBackAssignTriggerThreshold;
}

bool action_from_assignment_input(const SteamControllerInput &input,
                                  const SteamControllerInput &baseline,
                                  SteamBackAction &out)
{
    SteamBackAction found = SteamBackAction::None;
    uint8_t count = 0;
    const uint32_t new_buttons = input.buttons & ~baseline.buttons;
    SteamBackAction dpad = SteamBackAction::None;
    const SteamDpadDir old_dpad = selected_steam_dpad(baseline);
    const SteamDpadDir new_dpad = selected_steam_dpad(input);

    if (new_dpad != SteamDpadDir::None && new_dpad != old_dpad &&
        dpad_action(new_dpad, dpad)) {
        add_assignment_candidate(dpad, found, count);
    }
    if ((new_buttons & kSteamBtnA) != 0u) {
        add_assignment_candidate(SteamBackAction::South, found, count);
    }
    if ((new_buttons & kSteamBtnB) != 0u) {
        add_assignment_candidate(SteamBackAction::East, found, count);
    }
    if ((new_buttons & kSteamBtnX) != 0u) {
        add_assignment_candidate(SteamBackAction::West, found, count);
    }
    if ((new_buttons & kSteamBtnY) != 0u) {
        add_assignment_candidate(SteamBackAction::North, found, count);
    }
    if ((new_buttons & kSteamBtnLeftBumper) != 0u) {
        add_assignment_candidate(SteamBackAction::L1, found, count);
    }
    if ((new_buttons & kSteamBtnRightBumper) != 0u) {
        add_assignment_candidate(SteamBackAction::R1, found, count);
    }
    if ((new_buttons & kSteamBtnLeftStick) != 0u) {
        add_assignment_candidate(SteamBackAction::L3, found, count);
    }
    if ((new_buttons & kSteamBtnRightStick) != 0u) {
        add_assignment_candidate(SteamBackAction::R3, found, count);
    }
    if ((new_buttons & kSteamBtnView) != 0u) {
        add_assignment_candidate(SteamBackAction::Back, found, count);
    }
    if ((new_buttons & kSteamBtnMenu) != 0u) {
        add_assignment_candidate(SteamBackAction::Start, found, count);
    }
    if ((new_buttons & kSteamBtnSteam) != 0u) {
        add_assignment_candidate(SteamBackAction::Guide, found, count);
    }
    if ((new_buttons & kSteamBtnSteamMenu) != 0u) {
        add_assignment_candidate(SteamBackAction::Capture, found, count);
    }
    if (input.left_trigger >= kSteamBackAssignTriggerThreshold &&
        baseline.left_trigger < kSteamBackAssignTriggerThreshold) {
        add_assignment_candidate(SteamBackAction::L2, found, count);
    }
    if (input.right_trigger >= kSteamBackAssignTriggerThreshold &&
        baseline.right_trigger < kSteamBackAssignTriggerThreshold) {
        add_assignment_candidate(SteamBackAction::R2, found, count);
    }
    if ((new_buttons & kSteamBtnRightPadClick) != 0u) {
        add_assignment_candidate(SteamBackAction::R3, found, count);
    }

    if (count != 1u) {
        return false;
    }
    out = found;
    return true;
}

bool steam_back_pressed(const SteamControllerInput &input,
                        SteamBackButton button)
{
    switch (button) {
    case SteamBackButton::L4:
        return (input.buttons & kSteamBtnL4) != 0u;
    case SteamBackButton::R4:
        return (input.buttons & kSteamBtnR4) != 0u;
    case SteamBackButton::L5:
        return (input.buttons & kSteamBtnL5) != 0u;
    case SteamBackButton::R5:
        return (input.buttons & kSteamBtnR5) != 0u;
    default:
        return false;
    }
}

void apply_steam_back_action(SteamBackAction action, uint32_t &buttons)
{
    switch (action) {
    case SteamBackAction::DpadUp:
        buttons |= kNsUp;
        break;
    case SteamBackAction::DpadDown:
        buttons |= kNsDown;
        break;
    case SteamBackAction::DpadLeft:
        buttons |= kNsLeft;
        break;
    case SteamBackAction::DpadRight:
        buttons |= kNsRight;
        break;
    case SteamBackAction::South:
        buttons |= kNsB;
        break;
    case SteamBackAction::East:
        buttons |= kNsA;
        break;
    case SteamBackAction::West:
        buttons |= kNsY;
        break;
    case SteamBackAction::North:
        buttons |= kNsX;
        break;
    case SteamBackAction::L1:
        buttons |= kNsL;
        break;
    case SteamBackAction::R1:
        buttons |= kNsR;
        break;
    case SteamBackAction::L2:
        buttons |= kNsZL;
        break;
    case SteamBackAction::R2:
        buttons |= kNsZR;
        break;
    case SteamBackAction::L3:
        buttons |= kNsLeftStick;
        break;
    case SteamBackAction::R3:
        buttons |= kNsRightStick;
        break;
    case SteamBackAction::Back:
        buttons |= kNsMinus;
        break;
    case SteamBackAction::Start:
        buttons |= kNsPlus;
        break;
    case SteamBackAction::Guide:
        buttons |= kNsHome;
        break;
    case SteamBackAction::Capture:
        buttons |= kNsCapture;
        break;
    case SteamBackAction::None:
        break;
    }
}

void apply_steam_back_mappings(const SteamControllerInput &input,
                               const SteamBackMappings &mappings,
                               uint32_t &buttons)
{
    for (std::size_t i = 0; i < mappings.size(); ++i) {
        const auto back = static_cast<SteamBackButton>(i);
        if (steam_back_pressed(input, back)) {
            apply_steam_back_action(mappings[i], buttons);
        }
    }
}

bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

bool right_trackpad_active(const SteamControllerInput &input)
{
    return (input.buttons &
            (kSteamBtnRightPadTouch | kSteamBtnRightPadClick)) != 0u ||
           input.right_pad_force != 0;
}

uint16_t motor_to_u16(uint8_t value)
{
    return static_cast<uint16_t>(value) * 257u;
}

bool pulse_interval_elapsed(uint32_t now_ms, uint32_t last_ms)
{
    return last_ms == 0u ||
           static_cast<uint32_t>(now_ms - last_ms) >=
               kSteamTrackpadMovePulseIntervalMs;
}

bool update_touch_tick(bool active, int16_t x, int16_t y, bool skip_for_click,
                       bool &prev_active, int16_t &prev_x, int16_t &prev_y,
                       uint32_t &last_pulse_ms, uint32_t now_ms)
{
    if (!active) {
        prev_active = false;
        return false;
    }

    bool moved = false;
    if (prev_active) {
        const int32_t dx = abs_i32(static_cast<int32_t>(x) - prev_x);
        const int32_t dy = abs_i32(static_cast<int32_t>(y) - prev_y);
        moved = dx + dy >= kSteamTrackpadMovePulseThreshold;
    }

    if (moved && !skip_for_click && pulse_interval_elapsed(now_ms,
                                                           last_pulse_ms)) {
        last_pulse_ms = now_ms;
        prev_x = x;
        prev_y = y;
        prev_active = true;
        return true;
    }

    if (!prev_active) {
        prev_x = x;
        prev_y = y;
    }
    prev_active = true;
    return false;
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

SteamBackMappings steam_back_default_mappings()
{
    SteamBackMappings mappings{};
    mappings[steam_back_index(SteamBackButton::L4)] = SteamBackAction::L1;
    mappings[steam_back_index(SteamBackButton::R4)] = SteamBackAction::R1;
    mappings[steam_back_index(SteamBackButton::L5)] = SteamBackAction::L3;
    mappings[steam_back_index(SteamBackButton::R5)] = SteamBackAction::R3;
    return mappings;
}

bool steam_back_mappings_valid(const SteamBackMappings &mappings)
{
    for (SteamBackAction action : mappings) {
        if (!steam_back_action_valid(action)) {
            return false;
        }
    }
    return true;
}

void steam_back_normalize_mappings(SteamBackMappings &mappings)
{
    if (!steam_back_mappings_valid(mappings)) {
        mappings = steam_back_default_mappings();
    }
}

void steam_back_assignment_reset(SteamBackAssignmentState &state)
{
    state = {};
}

SteamBackAssignmentResult steam_back_assignment_process(
    SteamControllerInput &input, const SteamControllerInput *previous_input,
    SteamBackMappings &mappings, SteamBackAssignmentState &state,
    uint32_t now_ms)
{
    SteamBackAssignmentResult result{};

    if (state.suppress_until_release) {
        if (assignment_inputs_released(input)) {
            state.suppress_until_release = false;
        }
        input = {};
        result.consumed = true;
        return result;
    }

    if (state.active) {
        SteamBackAction action = SteamBackAction::None;
        if (deadline_reached(now_ms, state.deadline_ms)) {
            steam_back_assignment_reset(state);
            state.suppress_until_release = true;
        } else if (action_from_assignment_input(input, state.baseline,
                                                action)) {
            mappings[steam_back_index(state.target)] = action;
            steam_back_normalize_mappings(mappings);
            steam_back_assignment_reset(state);
            state.suppress_until_release = true;
            result.mapping_changed = true;
        }
        input = {};
        result.consumed = true;
        return result;
    }

    if (!steam_left_trackpad_center_modifier(input)) {
        return result;
    }

    const uint32_t previous_buttons =
        previous_input == nullptr ? 0u : previous_input->buttons;
    SteamBackButton target = SteamBackButton::L4;
    if (!single_back_rising(input.buttons & ~previous_buttons, target)) {
        return result;
    }

    state.active = true;
    state.target = target;
    state.deadline_ms = now_ms + kSteamBackAssignTimeoutMs;
    state.baseline = input;
    input = {};
    result.consumed = true;
    result.assignment_started = true;
    return result;
}

SwitchProState map_steam_to_switch(const SteamControllerInput &input)
{
    return map_steam_to_switch(input, steam_back_default_mappings());
}

SwitchProState map_steam_to_switch(const SteamControllerInput &input,
                                   const SteamBackMappings &back_mappings)
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
    if (input.buttons & kSteamBtnSteamMenu) {
        buttons |= kNsCapture;
    }
    if (steam_trigger_to_u8(input.left_trigger) > 0u) {
        buttons |= kNsZL;
    }
    if (steam_trigger_to_u8(input.right_trigger) > 0u) {
        buttons |= kNsZR;
    }
    if (input.buttons & kSteamBtnRightPadClick) {
        buttons |= kNsRightStick;
    }
    apply_steam_back_mappings(input, back_mappings, buttons);

    const bool use_right_pad = right_trackpad_active(input);
    const int16_t right_x =
        use_right_pad ? input.right_pad_x : input.right_stick_x;
    const int16_t right_y =
        use_right_pad ? input.right_pad_y : input.right_stick_y;

    state.buttons = buttons;
    state.left_x = steam_axis_to_switch12(input.left_stick_x);
    state.left_y = steam_axis_to_switch12(input.left_stick_y);
    state.right_x = steam_axis_to_switch12(right_x);
    state.right_y = steam_axis_to_switch12(right_y);

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

SteamTrackpadHapticAction update_steam_trackpad_haptics(
    const SteamControllerInput &input, SteamTrackpadHapticState &state,
    uint32_t now_ms)
{
    const bool raw_left_click =
        (input.buttons & kSteamBtnLeftPadClick) != 0u;
    const bool raw_right_click =
        (input.buttons & kSteamBtnRightPadClick) != 0u;

    const bool left_click_pulse =
        raw_left_click != state.prev_left_click;
    const bool right_click_pulse =
        raw_right_click != state.prev_right_click;

    const uint8_t left_dpad_mask = trackpad_dpad_mask(trackpad_dpad(
        input.left_pad_x, input.left_pad_y, raw_left_click));
    const bool left_dpad_direction_pulse =
        raw_left_click && !left_click_pulse &&
        left_dpad_mask != state.prev_left_dpad_mask;

    state.prev_left_click = raw_left_click;
    state.prev_right_click = raw_right_click;
    state.prev_left_dpad_mask = raw_left_click ? left_dpad_mask : 0u;
    state.prev_right_dpad_mask = 0u;

    SteamTrackpadHapticAction action{};
    action.left_click = left_click_pulse || left_dpad_direction_pulse;
    action.right_click = right_click_pulse;

    if (action.left_click) {
        state.last_left_pulse_ms = now_ms;
    }
    if (action.right_click) {
        state.last_right_pulse_ms = now_ms;
    }

    // Drag/touch ticks on BOTH pads (no left/right distinction). The touch
    // bit alone is unreliable on this BT HOG path during a slow drag, so a pad
    // counts as touched when the touch bit or non-zero pad force is present.
    // A hard click is intentionally excluded: while a pad is clicked, dragging
    // must NOT emit drag ticks (only the press/release click haptic fires).
    const bool left_touch_haptic =
        !raw_left_click &&
        ((input.buttons & kSteamBtnLeftPadTouch) != 0u ||
         input.left_pad_force != 0);
    const bool right_touch_haptic =
        !raw_right_click &&
        ((input.buttons & kSteamBtnRightPadTouch) != 0u ||
         input.right_pad_force != 0);

    action.left_tick = update_touch_tick(
        left_touch_haptic, input.left_pad_x, input.left_pad_y,
        action.left_click, state.prev_left_touch, state.prev_left_x,
        state.prev_left_y, state.last_left_pulse_ms, now_ms);
    action.right_tick = update_touch_tick(
        right_touch_haptic, input.right_pad_x, input.right_pad_y,
        action.right_click, state.prev_right_touch, state.prev_right_x,
        state.prev_right_y, state.last_right_pulse_ms, now_ms);

    return action;
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

std::size_t build_steam_body_rumble_output(const HapticCommand &haptics,
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

std::size_t build_steam_trackpad_feature_pulse_command(
    uint8_t pad, uint16_t on_us, uint16_t off_us, uint16_t repeat_count,
    int16_t gain_db, uint8_t *out, std::size_t out_size)
{
    uint8_t payload[10]{};
    payload[0] = pad;
    write_u16_le(payload + 1u, on_us);
    write_u16_le(payload + 3u, off_us);
    write_u16_le(payload + 5u, repeat_count);
    write_u16_le(payload + 7u, static_cast<uint16_t>(gain_db));
    payload[9] = kSteamHapticPulseHighPriority;
    return build_steam_feature_command(
        kSteamCmdTriggerHapticPulse, payload,
        static_cast<uint8_t>(sizeof(payload)), out, out_size);
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

} // namespace switchbridge

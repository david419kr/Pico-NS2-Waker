#pragma once

#include "ds5onswitch/haptics.h"
#include "ds5onswitch/types.h"

#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

constexpr uint16_t kXboxMicrosoftVid = 0x045e;
constexpr uint8_t kXboxInputReportId = 0x01;
constexpr uint8_t kXboxGuideReportId = 0x02;
constexpr uint8_t kXboxRumbleReportId = 0x03;
constexpr std::size_t kXboxInputReportMinSize = 16;
constexpr std::size_t kXboxRumbleReportSize = 9;

enum XboxControllerButton : uint32_t {
    kXboxBtnA = 1u << 0,
    kXboxBtnB = 1u << 1,
    kXboxBtnX = 1u << 2,
    kXboxBtnY = 1u << 3,
    kXboxBtnLeftBumper = 1u << 4,
    kXboxBtnRightBumper = 1u << 5,
    kXboxBtnBack = 1u << 6,
    kXboxBtnMenu = 1u << 7,
    kXboxBtnLeftStick = 1u << 8,
    kXboxBtnRightStick = 1u << 9,
    kXboxBtnGuide = 1u << 10,
    kXboxBtnDpadUp = 1u << 12,
    kXboxBtnDpadRight = 1u << 13,
    kXboxBtnDpadDown = 1u << 14,
    kXboxBtnDpadLeft = 1u << 15,
};

struct XboxControllerInput {
    uint16_t left_x = 0x8000;
    uint16_t left_y = 0x8000;
    uint16_t right_x = 0x8000;
    uint16_t right_y = 0x8000;
    uint16_t left_trigger = 0;
    uint16_t right_trigger = 0;
    uint32_t buttons = 0;
    bool guide_report_active = false;
};

bool parse_xbox_bt_report(const uint8_t *data, std::size_t size,
                          XboxControllerInput &out);
SwitchProState map_xbox_to_switch(const XboxControllerInput &input);
std::size_t build_xbox_rumble_output(const DualSenseHapticCommand &haptics,
                                     uint8_t *out, std::size_t out_size);

} // namespace ds5onswitch

#pragma once

#include "switch_bridge/haptics.h"
#include "switch_bridge/types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace switchbridge {

constexpr uint16_t kNintendoVendorId = 0x057e;
constexpr uint16_t kSwitch2MfgCompanyId = 0x0553;
constexpr uint16_t kProcon2ProductId = 0x2069;
constexpr uint8_t kProcon2InputReport05 = 0x05;
constexpr std::size_t kProcon2Report05MinSize = 62;
constexpr std::size_t kProcon2Vibration33Size = 33;
constexpr std::size_t kProcon2Vibration42Size = 42;

struct Procon2Advertisement {
    bool valid = false;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    bool needs_pair = false;
    std::array<uint8_t, 6> reconnect_mac{};
};

struct Procon2StickAxisCalibration {
    uint16_t neutral = 0;
    uint16_t max_above = 0;
    uint16_t max_below = 0;
};

struct Procon2StickCalibration {
    Procon2StickAxisCalibration x{};
    Procon2StickAxisCalibration y{};
};

struct Procon2Calibration {
    Procon2StickCalibration left{};
    Procon2StickCalibration right{};
};

enum class Procon2BackButton : uint8_t {
    GL,
    GR,
    Count,
};

using Procon2BackMappings =
    std::array<uint32_t, static_cast<std::size_t>(Procon2BackButton::Count)>;

struct Procon2BackAssignmentState {
    bool active = false;
    bool suppress_until_release = false;
    Procon2BackButton target = Procon2BackButton::GL;
    uint32_t deadline_ms = 0;
    uint32_t baseline_buttons = 0;
};

struct Procon2BackAssignmentResult {
    bool consumed = false;
    bool assignment_started = false;
    bool mapping_changed = false;
};

bool procon2_is_supported_product(uint16_t product_id);
bool parse_procon2_manufacturer_data(const uint8_t *data, std::size_t size,
                                     Procon2Advertisement &out);

void unpack_procon2_stick12(const uint8_t in[3], uint16_t &x, uint16_t &y);
uint16_t procon2_centered_to_switch_axis(int16_t centered, bool y_axis);

bool parse_procon2_report05(const uint8_t *report, std::size_t size,
                            const Procon2Calibration &calibration,
                            SwitchProState &out);

Procon2BackMappings procon2_back_default_mappings();
bool procon2_back_mappings_valid(const Procon2BackMappings &mappings);
void procon2_back_normalize_mappings(Procon2BackMappings &mappings);
void procon2_back_assignment_reset(Procon2BackAssignmentState &state);
Procon2BackAssignmentResult procon2_back_assignment_process(
    SwitchProState &state, const SwitchProState *previous_state,
    Procon2BackMappings &mappings, Procon2BackAssignmentState &assignment,
    uint32_t now_ms);
void procon2_apply_back_mappings(SwitchProState &state,
                                 const Procon2BackMappings &mappings);

std::size_t build_procon2_vibration33(const SwitchRumblePacket &left,
                                      const SwitchRumblePacket &right,
                                      uint8_t sequence,
                                      uint8_t *out,
                                      std::size_t out_size);
std::size_t build_procon2_vibration42(const SwitchRumblePacket &left,
                                      const SwitchRumblePacket &right,
                                      uint8_t sequence,
                                      uint8_t *out,
                                      std::size_t out_size);

} // namespace switchbridge

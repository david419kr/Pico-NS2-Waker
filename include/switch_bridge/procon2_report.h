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

bool procon2_is_supported_product(uint16_t product_id);
bool parse_procon2_manufacturer_data(const uint8_t *data, std::size_t size,
                                     Procon2Advertisement &out);

void unpack_procon2_stick12(const uint8_t in[3], uint16_t &x, uint16_t &y);
uint16_t procon2_centered_to_switch_axis(int16_t centered, bool y_axis);

bool parse_procon2_report05(const uint8_t *report, std::size_t size,
                            const Procon2Calibration &calibration,
                            SwitchProState &out);

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

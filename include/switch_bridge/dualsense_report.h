#pragma once

#include "switch_bridge/types.h"

#include <cstddef>
#include <cstdint>

namespace switchbridge {

constexpr std::size_t kDualSenseBt31PayloadSize = 63;

bool parse_dualsense_bt31_payload(const uint8_t *payload, std::size_t size,
                                  PlayStationInput &out);
bool parse_dualsense_bt_report(const uint8_t *report, std::size_t size,
                               PlayStationInput &out);

} // namespace switchbridge

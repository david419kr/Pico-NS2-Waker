#pragma once

#include "ds5onswitch/types.h"

#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

constexpr std::size_t kDualSenseBt31PayloadSize = 63;

bool parse_dualsense_bt31_payload(const uint8_t *payload, std::size_t size,
                                  DualSenseInput &out);
bool parse_dualsense_bt_report(const uint8_t *report, std::size_t size,
                               DualSenseInput &out);

} // namespace ds5onswitch

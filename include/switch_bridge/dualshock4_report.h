#pragma once

#include "switch_bridge/types.h"

#include <cstddef>
#include <cstdint>

namespace switchbridge {

constexpr std::size_t kDualShock4Bt11ReportSize = 78;
constexpr std::size_t kDualShock4BtMinimalReportSize = 10;

bool parse_dualshock4_bt_report(const uint8_t *report, std::size_t size,
                                PlayStationInput &out);

} // namespace switchbridge

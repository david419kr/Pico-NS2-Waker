#pragma once

#include "ds5onswitch/types.h"

#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

constexpr std::size_t kDualShock4Bt11ReportSize = 78;
constexpr std::size_t kDualShock4BtMinimalReportSize = 10;

bool parse_dualshock4_bt_report(const uint8_t *report, std::size_t size,
                                DualSenseInput &out);

} // namespace ds5onswitch

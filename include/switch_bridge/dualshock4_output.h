#pragma once

#include "switch_bridge/haptics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace switchbridge {

constexpr std::size_t kDualShock4BtOutputReportSize = 78;

using DualShock4BtOutputReport =
    std::array<uint8_t, kDualShock4BtOutputReportSize>;

class DualShock4OutputState {
public:
    void reset();
    void apply_haptics(const HapticCommand &command);
    void clear_motors();
    bool motors_active() const;

    DualShock4BtOutputReport build_report() const;

private:
    uint8_t right_motor_ = 0;
    uint8_t left_motor_ = 0;
};

uint32_t dualshock4_output_crc32(const uint8_t *data, std::size_t size);
void fill_dualshock4_output_crc(DualShock4BtOutputReport &report);

} // namespace switchbridge

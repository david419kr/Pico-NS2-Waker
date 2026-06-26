#pragma once

#include "ds5onswitch/haptics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ds5onswitch {

constexpr std::size_t kDualSenseOutputStateSize = 47;
constexpr std::size_t kDualSenseOutputStateBlockSize = 63;
constexpr std::size_t kDualSenseBtOutputReportSize = 78;
constexpr std::size_t kDualSenseBtAudioReportSize = 398;

using DualSenseBtOutputReport =
    std::array<uint8_t, kDualSenseBtOutputReportSize>;
using DualSenseBtAudioReport =
    std::array<uint8_t, kDualSenseBtAudioReportSize>;

class DualSenseOutputState {
public:
    void reset();
    void apply_haptics(const DualSenseHapticCommand &command);
    void clear_motors();
    bool motors_active() const;
    void copy_state_block(uint8_t *out, std::size_t size) const;

    DualSenseBtOutputReport build_report(uint8_t sequence) const;

private:
    std::array<uint8_t, kDualSenseOutputStateSize> state_{};
};

uint32_t dualsense_output_crc32(const uint8_t *data, std::size_t size);
void fill_dualsense_output_crc(DualSenseBtOutputReport &report);
void fill_dualsense_output_crc(uint8_t *report, std::size_t size);

} // namespace ds5onswitch

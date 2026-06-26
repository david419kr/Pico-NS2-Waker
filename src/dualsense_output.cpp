#include "ds5onswitch/dualsense_output.h"

#include <algorithm>

namespace ds5onswitch {

namespace {

constexpr std::array<uint8_t, kDualSenseOutputStateSize> kInitialState = {
    0xfd, 0xf7, 0x00, 0x00, 0x7f, 0x64, 0xff, 0x09,
    0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x07, 0x00,
    0x00, 0x02, 0x01, 0x00, 0xff, 0xd7, 0x00,
};

constexpr std::size_t kRumbleRightOffset = 2;
constexpr std::size_t kRumbleLeftOffset = 3;
constexpr std::size_t kFlags0Offset = 0;
constexpr std::size_t kFlags38Offset = 38;

constexpr uint32_t crc32_table_entry(uint32_t index)
{
    for (unsigned bit = 0; bit < 8; ++bit) {
        index = (index >> 1u) ^ (0xedb88320u & (0u - (index & 1u)));
    }
    return index;
}

constexpr auto make_crc32_table()
{
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < table.size(); ++i) {
        table[i] = crc32_table_entry(i);
    }
    return table;
}

constexpr auto kCrc32Lookup = make_crc32_table();

uint8_t mix_motor(uint8_t body, uint8_t pulse)
{
    const uint16_t boosted_pulse = static_cast<uint16_t>(pulse) * 3u / 4u;
    return static_cast<uint8_t>(
        std::min<uint16_t>(255u, std::max<uint16_t>(body, boosted_pulse)));
}

void set_bit(uint8_t &value, uint8_t bit, bool enabled)
{
    const uint8_t mask = static_cast<uint8_t>(1u << bit);
    if (enabled) {
        value = static_cast<uint8_t>(value | mask);
    } else {
        value = static_cast<uint8_t>(value & ~mask);
    }
}

} // namespace

void DualSenseOutputState::reset()
{
    state_ = kInitialState;
}

void DualSenseOutputState::apply_haptics(const DualSenseHapticCommand &command)
{
    state_[kRumbleLeftOffset] = mix_motor(command.left_motor, command.left_pulse);
    state_[kRumbleRightOffset] = mix_motor(command.right_motor, command.right_pulse);

    const bool active = motors_active();
    set_bit(state_[kFlags0Offset], 0, true);
    set_bit(state_[kFlags0Offset], 1, active);
    set_bit(state_[kFlags38Offset], 2, true);
}

void DualSenseOutputState::clear_motors()
{
    state_[kRumbleLeftOffset] = 0;
    state_[kRumbleRightOffset] = 0;
    set_bit(state_[kFlags0Offset], 0, true);
    set_bit(state_[kFlags0Offset], 1, false);
    set_bit(state_[kFlags38Offset], 2, true);
}

bool DualSenseOutputState::motors_active() const
{
    return state_[kRumbleLeftOffset] != 0 || state_[kRumbleRightOffset] != 0;
}

void DualSenseOutputState::copy_state_block(uint8_t *out, std::size_t size) const
{
    if (out == nullptr) {
        return;
    }

    std::fill(out, out + size, 0);
    const std::size_t copy_size = std::min<std::size_t>(state_.size(), size);
    std::copy(state_.begin(), state_.begin() + copy_size, out);
}

DualSenseBtOutputReport DualSenseOutputState::build_report(uint8_t sequence) const
{
    DualSenseBtOutputReport report{};
    report[0] = 0x31;
    report[1] = static_cast<uint8_t>((sequence & 0x0fu) << 4u);
    report[2] = 0x10;
    std::copy(state_.begin(), state_.end(), report.begin() + 3);
    fill_dualsense_output_crc(report);
    return report;
}

uint32_t dualsense_output_crc32(const uint8_t *data, std::size_t size)
{
    constexpr uint32_t kSeedA2 = 0xeada2d49u;
    uint32_t crc = ~kSeedA2;

    while (size-- > 0) {
        crc = (crc >> 8u) ^ kCrc32Lookup[(crc ^ *data++) & 0xffu];
    }

    return ~crc;
}

void fill_dualsense_output_crc(DualSenseBtOutputReport &report)
{
    fill_dualsense_output_crc(report.data(), report.size());
}

void fill_dualsense_output_crc(uint8_t *report, std::size_t size)
{
    if (report == nullptr || size < 4u) {
        return;
    }

    const uint32_t crc =
        dualsense_output_crc32(report, size - 4u);
    report[size - 4u] = static_cast<uint8_t>(crc);
    report[size - 3u] = static_cast<uint8_t>(crc >> 8u);
    report[size - 2u] = static_cast<uint8_t>(crc >> 16u);
    report[size - 1u] = static_cast<uint8_t>(crc >> 24u);
}

} // namespace ds5onswitch

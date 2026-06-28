#include "ds5onswitch/dualshock4_output.h"

#include "ds5onswitch/dualsense_output.h"

#if __has_include("pico.h")
#include "pico.h"
#else
#define __not_in_flash_func(name) name
#endif

#include <algorithm>

namespace ds5onswitch {

namespace {

constexpr uint8_t kDs4BtOutputReportId = 0x11;
constexpr uint8_t kDs4BtHwControlHidCrc4ms = 0xc4;
constexpr uint8_t kDs4ValidFlag0Motor = 0x01;
constexpr uint8_t kDs4ValidFlag0Led = 0x02;
constexpr std::size_t kDs4ValidFlag0Offset = 3;
constexpr std::size_t kDs4RightMotorOffset = 6;
constexpr std::size_t kDs4LeftMotorOffset = 7;
constexpr std::size_t kDs4LedRedOffset = 8;
constexpr std::size_t kDs4LedGreenOffset = 9;
constexpr std::size_t kDs4LedBlueOffset = 10;

uint8_t __not_in_flash_func(mix_motor)(uint8_t body, uint8_t pulse)
{
    const uint16_t boosted_pulse = static_cast<uint16_t>(pulse) * 3u / 4u;
    return static_cast<uint8_t>(
        std::min<uint16_t>(255u, std::max<uint16_t>(body, boosted_pulse)));
}

} // namespace

void __not_in_flash_func(DualShock4OutputState::reset)()
{
    right_motor_ = 0;
    left_motor_ = 0;
}

void __not_in_flash_func(DualShock4OutputState::apply_haptics)(
    const DualSenseHapticCommand &command)
{
    left_motor_ = mix_motor(command.left_motor, command.left_pulse);
    right_motor_ = mix_motor(command.right_motor, command.right_pulse);
}

void __not_in_flash_func(DualShock4OutputState::clear_motors)()
{
    right_motor_ = 0;
    left_motor_ = 0;
}

bool __not_in_flash_func(DualShock4OutputState::motors_active)() const
{
    return right_motor_ != 0 || left_motor_ != 0;
}

DualShock4BtOutputReport __not_in_flash_func(DualShock4OutputState::build_report)()
    const
{
    DualShock4BtOutputReport report{};
    report[0] = kDs4BtOutputReportId;
    report[1] = kDs4BtHwControlHidCrc4ms;
    report[2] = 0x00;
    report[kDs4ValidFlag0Offset] = kDs4ValidFlag0Motor | kDs4ValidFlag0Led;
    report[kDs4RightMotorOffset] = right_motor_;
    report[kDs4LeftMotorOffset] = left_motor_;
    report[kDs4LedRedOffset] = 0x00;
    report[kDs4LedGreenOffset] = 0x00;
    report[kDs4LedBlueOffset] = 0x10;
    fill_dualshock4_output_crc(report);
    return report;
}

uint32_t __not_in_flash_func(dualshock4_output_crc32)(const uint8_t *data,
                                                      std::size_t size)
{
    return dualsense_crc32_seeded(0xa2u, data, size);
}

void __not_in_flash_func(fill_dualshock4_output_crc)(
    DualShock4BtOutputReport &report)
{
    const uint32_t crc =
        dualshock4_output_crc32(report.data(), report.size() - 4u);
    report[report.size() - 4u] = static_cast<uint8_t>(crc);
    report[report.size() - 3u] = static_cast<uint8_t>(crc >> 8u);
    report[report.size() - 2u] = static_cast<uint8_t>(crc >> 16u);
    report[report.size() - 1u] = static_cast<uint8_t>(crc >> 24u);
}

} // namespace ds5onswitch
